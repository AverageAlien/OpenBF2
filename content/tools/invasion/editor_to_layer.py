#!/usr/bin/env python3
"""Pack the retail BF2 Editor's working layer (Editor/GamePlayObjects.con) back into a level's Invasion layer.

Usage:
  editor_to_layer.py <spec.txt> [--mod openbf2]

The level is edited in the retail editor with every object on the Default layer, from the loose files
unpacked next to server.zip. The editor can't write what it doesn't know, so its save loses the
Invasion settings; this puts them back from the spec (see make_invasion_layer.py for the keys):
- gameLogic.setDefaultNumberOfTicketsEx per team (spec `tickets`)
- supplyGroupId and enemyTicketLossWhenCaptured on the objective control points (spec `group`; the
  bonus goes on the group's first point, 0 on the others)
- ObjectTemplate.setOnlyForTeam on spawn points of the objectives, from their Group field
  (the retail editor has no team field): group 1 = defenders, group 2 = attackers. Spawn points of other
  control points (the attackers' main base) are left as they are.

Writes gamemodes/gpm_invasion/<size>/GamePlayObjects.con as a loose file and into server.zip (the first
run keeps server.zip.before-editorpack). Editor/AI/StrategicAreas.ai goes into the layer too when it differs.
Nothing else from the editor's save is packed.
"""
import argparse
import os
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_invasion_layer import GAME, MODE, readSpec, findEntry


def convert(text, spec):
    attackers = int(spec['attackers'])
    defenders = 3 - attackers
    teamOfGroup = {1: defenders, 2: attackers}
    groupOf, bonusOf = {}, {}
    for i, (bonus, names) in enumerate(spec['groups']):
        for k, name in enumerate(names):
            groupOf[name.lower()] = i + 1
            bonusOf[name.lower()] = bonus if k == 0 else 0

    lines = text.replace('\r\n', '\n').split('\n')
    objectiveIds, controlPoints = set(), set()
    current = None
    for line in lines:
        p = line.split()
        if len(p) >= 3 and p[0].lower() == 'objecttemplate.create':
            current = p[2].lower()
            if p[1].lower() == 'controlpoint':
                controlPoints.add(current)
        elif len(p) == 2 and p[0].lower() == 'objecttemplate.controlpointid' and current in groupOf:
            objectiveIds.add(p[1])
    missing = set(groupOf) - controlPoints
    if missing:
        raise SystemExit('control points of the spec not in the editor layer: ' + ', '.join(sorted(missing)))

    out = ['gameLogic.setDefaultNumberOfTicketsEx %s %d %d' % (spec['size'], team, n)
           for team, n in sorted(spec['tickets'].items())]
    report = {'defenders': 0, 'attackers': 0, 'unassigned': []}
    current, kind, group = None, None, None
    for line in lines:
        p = line.split()
        cmd = p[0].lower() if p else ''
        if cmd == 'objecttemplate.create' and len(p) >= 3:
            kind, current, group = p[1].lower(), p[2].lower(), None
        elif cmd.startswith(('object.', 'if', 'endif', 'combatarea')):
            current = kind = None
        if cmd == 'gamelogic.setdefaultnumberofticketsex':
            continue
        if current in groupOf and cmd in ('objecttemplate.supplygroupid', 'objecttemplate.enemyticketlosswhencaptured'):
            continue
        if kind == 'spawnpoint' and cmd == 'objecttemplate.setonlyforteam':
            continue
        if kind == 'spawnpoint' and cmd == 'objecttemplate.setgroup':
            group = int(p[1])
        out.append(line)
        if current in groupOf and cmd == 'objecttemplate.controlpointid':
            out.append('ObjectTemplate.supplyGroupId %d' % groupOf[current])
            out.append('ObjectTemplate.enemyTicketLossWhenCaptured %d' % bonusOf[current])
        if kind == 'spawnpoint' and cmd == 'objecttemplate.setcontrolpointid' and p[1] in objectiveIds:
            if group in teamOfGroup:
                out.append('ObjectTemplate.setOnlyForTeam %d' % teamOfGroup[group])
                report['defenders' if teamOfGroup[group] == defenders else 'attackers'] += 1
            else:
                report['unassigned'].append(current)
    while out and out[-1] == '':
        out.pop()
    return '\r\n'.join(out) + '\r\n', report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('spec')
    ap.add_argument('--mod', default='openbf2')
    args = ap.parse_args()

    spec = readSpec(args.spec)
    spec['size'] = spec['source'].split('/')[1]
    levelDir = os.path.join(GAME, 'mods', args.mod, 'Levels', spec['level'])
    editorGpo = open(os.path.join(levelDir, 'Editor', 'GamePlayObjects.con'), encoding='latin1', newline='').read()
    gpo, report = convert(editorGpo, spec)

    layerPrefix = 'gamemodes/%s/%s/' % (MODE, spec['size'])
    serverPath = os.path.join(levelDir, 'server.zip')
    newFiles = {layerPrefix + 'GamePlayObjects.con': gpo.encode('latin1')}
    with zipfile.ZipFile(serverPath) as zf:
        areasPath = os.path.join(levelDir, 'Editor', 'AI', 'StrategicAreas.ai')
        if os.path.exists(areasPath):
            areas = open(areasPath, 'rb').read()
            if areas != zf.read(findEntry(zf, layerPrefix + 'AI/StrategicAreas.ai')):
                newFiles[layerPrefix + 'AI/StrategicAreas.ai'] = areas
        backup = serverPath + '.before-editorpack'
        if not os.path.exists(backup):
            shutil.copyfile(serverPath, backup)
        tmpPath = serverPath + '.building'
        written = {}
        with zipfile.ZipFile(tmpPath, 'w', zipfile.ZIP_DEFLATED) as out:
            for info in zf.infolist():
                key = info.filename.replace('\\', '/').lower()
                match = [n for n in newFiles if n.lower() == key]
                if match:
                    out.writestr(info.filename, newFiles[match[0]])
                    written[match[0]] = info.filename
                else:
                    out.writestr(info, zf.read(info))
            for name, data in newFiles.items():
                if name not in written:
                    out.writestr(name, data)
                    written[name] = name
    os.replace(tmpPath, serverPath)
    for name, entry in written.items():
        path = os.path.join(levelDir, entry.replace('\\', '/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(newFiles[name])

    print('packed into %s: %s' % (serverPath, ', '.join(sorted(written.values()))))
    print('tickets: %s; capture bonus per group: %s' % (
        ', '.join('team %d %d' % kv for kv in sorted(spec['tickets'].items())),
        ', '.join('%d' % b for b, _ in spec['groups'])))
    print('objective spawn points: %d defender-only, %d attacker-only' % (report['defenders'], report['attackers']))
    if report['unassigned']:
        print('WARNING: objective spawn points without group 1/2 (both teams can use them):', ', '.join(report['unassigned']))


if __name__ == '__main__':
    main()
