#!/usr/bin/env python3
"""Add an Invasion layer (game mode gpm_invasion) to a BF2 level, built from one of its layers.

Usage:
  make_invasion_layer.py <spec.txt> [--mod openbf2] [--force]

Needs OpenBF2's engine (mods/openbf2): per-point capture locks, ObjectTemplate.setOnlyForTeam on
spawn points, cp_setParam 'spawnEnabled', and gpm_invasion.py.

Spec keys:
  level   Strike_at_Karkand        level folder in mods/<mod>/Levels
  source  sp1/16                   layer to build from; the new layer is gpm_invasion/<size>
  attackers 2
  tickets <team> <start tickets>   (gameLogic.setDefaultNumberOfTicketsEx in the layer)
  group <bonus tickets> <cp template> [<cp template> ...]     in attack order
  attackerSpawn <cp template> <x/y/z> [<x/y/z> ...]           optional, else picked automatically

What it writes:
- server.zip: GameModes/gpm_invasion/<size>/ with GamePlayObjects.con (objective points get
  supplyGroupId = their group and enemyTicketLossWhenCaptured = the group's bonus on the first point;
  their spawn points become defender-only; new attacker-only spawn points on every objective;
  spawners on objectives lose the attackers' vehicle template), AI/StrategicAreas.ai (the source
  layer's without its areas not made from a control point, or a generated chain, plus two-way
  neighbours along the attack order) and
  AI/Strategies.ai. A copy of the original is kept as server.zip.before-invasion.
- Info/<level>.desc: a gpm_invasion mode entry (ai="1"), original kept as .desc.before-invasion.
- Info/gpm_invasion_<size>_menuMap.png: a copy of the source layer's menu map.
"""
import argparse
import math
import os
import re
import shutil
import struct
import zipfile

# Battlefield 2 folder (holds BF2.exe and mods/); the current folder when BF2_GAME_DIR is unset
GAME = os.environ.get('BF2_GAME_DIR', '.')
MODE = 'gpm_invasion'
ATT_SPAWNS = 4
ATT_SPAWN_GROUP = 7


def readSpec(path):
    spec = {'groups': [], 'attackerSpawn': {}, 'tickets': {}}
    for line in open(path).read().splitlines():
        parts = line.split('#')[0].split()
        if not parts:
            continue
        key = parts[0]
        if key == 'group':
            spec['groups'].append((int(parts[1]), parts[2:]))
        elif key == 'attackerSpawn':
            spec['attackerSpawn'][parts[1].lower()] = [vec(p) for p in parts[2:]]
        elif key == 'tickets':
            spec['tickets'][int(parts[1])] = int(parts[2])
        else:
            spec[key] = parts[1]
    return spec


def findEntry(zf, name):
    for n in zf.namelist():
        if n.replace('\\', '/').lower() == name.lower():
            return n
    raise KeyError(name)


def vec(s):
    return tuple(float(v) for v in s.split('/'))


def fmt(v):
    return '%.3f/%.3f/%.3f' % v


def dist2d(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])


def centroid(points):
    n = float(len(points))
    return tuple(sum(p[i] for p in points) / n for i in range(3))


class Layer:
    """GamePlayObjects.con parsed just enough to find control points, spawn points and spawners."""

    def __init__(self, text):
        self.lines = text.replace('\r\n', '\n').split('\n')
        self.templates = {}
        self.objects = []
        cur = obj = None
        for i, line in enumerate(self.lines):
            p = line.strip().split(None, 1)
            if not p:
                continue
            cmd = p[0].lower()
            arg = p[1] if len(p) > 1 else ''
            if cmd == 'objecttemplate.create':
                kind, name = arg.split()[:2]
                cur = {'kind': kind.lower(), 'name': name, 'line': i}
                self.templates[name.lower()] = cur
            elif cmd.startswith('objecttemplate.') and cur is not None:
                cur.setdefault(cmd[15:], []).append(arg)
            elif cmd == 'object.create':
                obj = {'template': arg.split()[0].lower()}
                self.objects.append(obj)
            elif cmd.startswith('object.') and obj is not None:
                obj[cmd[7:]] = arg

    def controlPoints(self):
        cps = {}
        for o in self.objects:
            t = self.templates.get(o['template'])
            if t and t['kind'] == 'controlpoint':
                cps[t['name'].lower()] = {
                    'name': t['name'], 'id': int(t['controlpointid'][0]),
                    'team': int(t.get('team', ['0'])[0]),
                    'base': int(t.get('unabletochangeteam', ['0'])[0]),
                    'pos': vec(o['absoluteposition'])}
        return cps

    def spawnPoints(self):
        result = []
        for o in self.objects:
            t = self.templates.get(o['template'])
            if t and t['kind'] == 'spawnpoint':
                result.append((int(t.get('setcontrolpointid', ['-1'])[0]), vec(o['absoluteposition']), t))
        return result

    def spawners(self):
        result = []
        for o in self.objects:
            t = self.templates.get(o['template'])
            if t and t['kind'] == 'objectspawner' and 'absoluteposition' in o:
                cid = int(o.get('setcontrolpointid', t.get('setcontrolpointid', ['-1'])[0]))
                result.append((cid, vec(o['absoluteposition']), t))
        return result


class Heightmap:
    def __init__(self, zf):
        text = zf.read(findEntry(zf, 'Heightdata.con')).decode('latin1')
        self.water = -1e9
        primary = True
        for line in text.splitlines():
            p = line.split()
            if len(p) < 2:
                continue
            cmd = p[0].lower()
            if cmd == 'heightmapcluster.setseawaterlevel':
                self.water = float(p[1])
            elif cmd == 'heightmapcluster.addheightmap':
                primary = p[2:4] == ['0', '0']
            elif not primary:
                continue
            elif cmd == 'heightmap.setsize':
                self.size = int(p[1])
            elif cmd == 'heightmap.setscale':
                self.scale = vec(p[1])
            elif cmd == 'heightmap.loadheightdata':
                raw = p[1].replace('\\', '/').split('/')[-1]
        data = zf.read(findEntry(zf, raw))
        self.heights = struct.unpack('<%dH' % (len(data) // 2), data)

    def height(self, x, z):
        half = (self.size - 1) / 2.0
        i = min(max(int(round(x / self.scale[0] + half)), 0), self.size - 1)
        j = min(max(int(round(z / self.scale[2] + half)), 0), self.size - 1)
        return max(self.heights[j * self.size + i] * self.scale[1], self.water)


def pickAttackerSpawns(cp, previous, defenderSpots, pool, heightmap):
    """Spots from other layers' spawn points and vehicle spawners (placed by DICE, so clear of
    buildings) 20-90 m from the objective on the attackers' side, away from the defenders' spawns.
    Falls back to a ring on the heightmap (may land inside houses)."""
    spots = []
    candidates = []
    approach = dist2d(cp['pos'], previous)
    for p in pool:
        d = dist2d(p, cp['pos'])
        if 20 <= d <= 90 and dist2d(p, previous) < approach and all(dist2d(p, s) > 25 for s in defenderSpots):
            candidates.append((dist2d(p, previous) + 0.5 * d, p))
    candidates.sort()
    for _, p in candidates:
        if all(dist2d(p, s) > 6 for s in spots):
            spots.append(p)
        if len(spots) == ATT_SPAWNS:
            return spots, 'pool'
    baseYaw = math.atan2(previous[0] - cp['pos'][0], previous[2] - cp['pos'][2])
    for k in range(ATT_SPAWNS - len(spots)):
        a = baseYaw + (k - 1.5) * 0.25
        x = cp['pos'][0] + 45 * math.sin(a)
        z = cp['pos'][2] + 45 * math.cos(a)
        spots.append((x, heightmap.height(x, z), z))
    return spots, 'pool+ring' if candidates else 'ring'


def yawTowards(src, dst):
    return math.degrees(math.atan2(dst[0] - src[0], dst[2] - src[2]))


def buildLayer(spec, layer, pool, heightmap):
    attackers = int(spec['attackers'])
    defenders = 3 - attackers
    cps = layer.controlPoints()
    groups = []
    for bonus, names in spec['groups']:
        try:
            groups.append((bonus, [cps[n.lower()] for n in names]))
        except KeyError as e:
            raise SystemExit('no control point %s in %s' % (e, spec['source']))
    objectives = [cp for _, g in groups for cp in g]
    base = [cp for cp in cps.values() if cp['base'] and cp['team'] == attackers]
    if len(base) != 1:
        raise SystemExit('expected one uncappable attacker base, found %d' % len(base))
    base = base[0]
    leftover = [cp['name'] for cp in cps.values() if cp is not base and cp not in objectives]
    if leftover:
        raise SystemExit('control points not in any group: ' + ', '.join(leftover))

    groupOf = {}
    bonusOf = {}
    for i, (bonus, g) in enumerate(groups):
        for k, cp in enumerate(g):
            groupOf[cp['name'].lower()] = i + 1
            bonusOf[cp['name'].lower()] = bonus if k == 0 else 0
    objectiveIds = set(cp['id'] for cp in objectives)
    stripSpawners = set(t['name'].lower() for cid, _, t in layer.spawners() if cid in objectiveIds)
    defenderSpawns = set(t['name'].lower() for cid, _, t in layer.spawnPoints() if cid in objectiveIds)

    out = ['gameLogic.setDefaultNumberOfTicketsEx %s %d %d' % (spec['size'], team, n)
           for team, n in sorted(spec['tickets'].items())]
    current = None
    for line in layer.lines:
        p = line.split()
        cmd = p[0].lower() if p else ''
        if cmd == 'objecttemplate.create' and len(p) >= 3:
            current = p[2].lower()
        elif cmd.startswith('object.'):
            current = None
        if current in groupOf and cmd in ('objecttemplate.supplygroupid', 'objecttemplate.enemyticketlosswhencaptured'):
            continue
        if current in stripSpawners and cmd == 'objecttemplate.setobjecttemplate' and p[1] == str(attackers):
            continue
        if current in defenderSpawns and cmd == 'objecttemplate.setonlyforteam':
            continue
        out.append(line)
        if current in groupOf and cmd == 'objecttemplate.controlpointid':
            out.append('ObjectTemplate.supplyGroupId %d' % groupOf[current])
            out.append('ObjectTemplate.enemyTicketLossWhenCaptured %d' % bonusOf[current])
        if current in defenderSpawns and cmd == 'objecttemplate.setcontrolpointid':
            out.append('ObjectTemplate.setOnlyForTeam %d' % defenders)

    report = []
    added = ['', 'rem ********** Invasion: attacker spawn points on the objectives **********']
    previous = base['pos']
    spawns = layer.spawnPoints()
    for _, group in groups:
        for cp in group:
            defSpots = [p for cid, p, _ in spawns if cid == cp['id']]
            vehicleSpots = [p for cid, p, _ in layer.spawners() if cid == cp['id']]
            spots = spec['attackerSpawn'].get(cp['name'].lower())
            how = 'spec'
            if not spots:
                spots, how = pickAttackerSpawns(cp, previous, defSpots + vehicleSpots, pool, heightmap)
            for k, spot in enumerate(spots):
                name = '%s_inv_att_%d' % (cp['name'], k + 1)
                added += [
                    'ObjectTemplate.create SpawnPoint %s' % name,
                    'ObjectTemplate.activeSafe SpawnPoint %s' % name,
                    'ObjectTemplate.isNotSaveable 1',
                    'ObjectTemplate.setSpawnPositionOffset 0/1.25/0',
                    'ObjectTemplate.setControlPointId %d' % cp['id'],
                    'ObjectTemplate.setGroup %d' % ATT_SPAWN_GROUP,
                    'ObjectTemplate.setOnlyForTeam %d' % attackers,
                    'Object.create %s' % name,
                    'Object.absolutePosition %s' % fmt(spot),
                    'Object.rotation %.3f/0.000/0.000' % yawTowards(spot, cp['pos']),
                    '']
            report.append('%-30s group %d, defender spawns %d, spawners %d, attacker spawns %d (%s): %s' % (
                cp['name'], groupOf[cp['name'].lower()], len(defSpots), len(vehicleSpots), len(spots), how,
                ' '.join(fmt(p) for p in spots)))
        previous = centroid([cp['pos'] for cp in group])
    report.append('attacker vehicle templates removed from %d spawners on objectives' % len(stripSpawners))
    return '\n'.join(out + added) + '\n', report, [g for _, g in groups], base


def dropFreeAreas(text):
    """Removes the areas not made from a control point (e.g. Karkand's USBASE_*FLANK), with their
    neighbour links and waypoints. Nothing can capture them, but the bots of both sides would still
    attack them."""
    lines = text.replace('\r\n', '\n').split('\n')
    names = set()
    for line in lines:
        words = line.split()
        if len(words) > 1 and words[0].lower() == 'aistrategicarea.create':
            names.add(words[1].lower())
    out = []
    skipping = False
    afterCreate = False
    for line in lines:
        words = line.split()
        command = words[0].lower() if words else ''
        lowered = [w.lower() for w in words]
        if command == 'aistrategicarea.create' and lowered[1] in names:
            afterCreate = True
            continue
        if afterCreate and command == 'aistrategicarea.layer':
            afterCreate = False
            continue
        afterCreate = False
        if command == 'aistrategicarea.setactive':
            skipping = len(words) > 1 and lowered[1] in names
            if skipping:
                continue
        elif skipping and (not words or (command.startswith('aistrategicarea.') and command != 'aistrategicarea.addwaypoint')):
            continue
        else:
            skipping = False
        if names.intersection(lowered[1:]) and ('aistrategicarea.addwaypoint' in lowered
                                                or command == 'aistrategicarea.addneighbour'):
            continue
        out.append(line)
    return '\n'.join(out), sorted(names)


def strategicAreas(sourceAreas, groups, base, attackers):
    chain = [[base]] + groups
    links = []
    for a, b in zip(chain, chain[1:]):
        links += ['AIStrategicArea.addTwoWayNeighbour %s %s' % (x['name'], y['name']) for x in a for y in b]
    if 'createfromcontrolpoint' in sourceAreas.lower():
        missing = [cp['name'] for g in chain for cp in g if cp['name'].lower() not in sourceAreas.lower()]
        if missing:
            raise SystemExit('source StrategicAreas.ai has no area for ' + ', '.join(missing))
        kept, dropped = dropFreeAreas(sourceAreas)
        how = 'source + attack order' + (', dropped ' + ', '.join(dropped) if dropped else '')
        return kept.rstrip('\n') + '\n\nrem *** Invasion: attack order ***\n' + '\n'.join(links) + '\n', how
    out = ['rem *** Create strategic areas ***']
    for g in chain:
        for cp in g:
            out += ['aiStrategicArea.createFromControlPoint %s %d 50' % (cp['name'], cp['id']), 'aiStrategicArea.layer 1', '']
    for g in chain:
        for cp in g:
            out += [
                'aiStrategicArea.setActive %s' % cp['name'],
                'aiStrategicArea.addObjectTypeFlag ControlPoint',
                'AIStrategicArea.setOrderPosition Infantry %s' % fmt(cp['pos']),
                'AIStrategicArea.setOrderPosition Vehicle %s' % fmt(cp['pos']),
                'aiStrategicArea.setSide %d' % (attackers if cp is base else 0),
                'aiStrategicArea.vehicleSearchRadius 14.1421',
                '']
    return '\n'.join(out + links) + '\n', 'generated'


def strategies(attackers, width):
    """width = most points in one group. The engine keeps bots off points they can't capture
    (locked or not yet open), so these only spread the squads over the open group."""
    return """rem ************************
rem ** Invasion (gpm_invasion)
rem ************************

aiStrategy.createConstantCondition invHoldsAnyCond Crisp EqualGreater Friendly ControlPoint 1
aiStrategy.setConditionStrength Required

aiStrategy.createPrerequisite invAlwaysPrereq
aiStrategy.addCondition invHoldsAnyCond 20.0

aiStrategy.createStrategy invAttack
aiStrategy.Aggression 0.75
aiStrategy.NumberOfAttacks %(width)d
aiStrategy.NumberOfDefences %(width)d
aiStrategy.setStrategicObjectsModifier ControlPoint      10.0 Owned
aiStrategy.setStrategicObjectsModifier ControlPoint      10.0 Hostile
aiStrategy.setStrategicObjectsModifier ControlPoint      12.0 Neutral
aiStrategy.setPrerequisite invAlwaysPrereq

aiStrategy.createStrategy invDefend
aiStrategy.Aggression 0.4
aiStrategy.NumberOfAttacks %(width)d
aiStrategy.NumberOfDefences %(width)d
aiStrategy.setStrategicObjectsModifier ControlPoint      10.0 Owned
aiStrategy.setStrategicObjectsModifier ControlPoint      12.0 Neutral
aiStrategy.setStrategicObjectsModifier ControlPoint      10.0 Hostile
aiStrategy.setPrerequisite invAlwaysPrereq

rem *** Add strategies ***
ai.addSAIStrategy %(defenders)d invDefend
ai.addSAIStrategy %(attackers)d invAttack
""" % {'width': width, 'attackers': attackers, 'defenders': 3 - attackers}


def addDescMode(text, size):
    """Adds (or replaces) the gpm_invasion entry in the level's .desc."""
    text = re.sub(r'\s*<mode type="%s">.*?</mode>' % MODE, '', text, flags=re.S)
    entry = ('\r\n\t\t<mode type="%s">\r\n'
             '\t\t\t<maptype ai="1" players="%s" type="assault" locid="GAMEMODE_DESCRIPTION_assault">Invasion</maptype>\r\n'
             '\t\t</mode>' % (MODE, size))
    pos = text.rfind('</modes>')
    head = text[:pos].rstrip()
    return head + entry + '\r\n\t' + text[pos:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('spec')
    ap.add_argument('--mod', default='openbf2')
    ap.add_argument('--force', action='store_true', help='replace an existing gpm_invasion layer of that size')
    args = ap.parse_args()

    spec = readSpec(args.spec)
    srcMode, size = spec['source'].split('/')
    spec['size'] = size
    levelDir = os.path.join(GAME, 'mods', args.mod, 'Levels', spec['level'])
    serverPath = os.path.join(levelDir, 'server.zip')
    layerPrefix = 'gamemodes/%s/%s/' % (MODE, size)

    with zipfile.ZipFile(serverPath) as zf:
        if any(n.replace('\\', '/').lower().startswith(layerPrefix) for n in zf.namelist()) and not args.force:
            raise SystemExit('%s already has a %s/%s layer, use --force to rebuild it' % (spec['level'], MODE, size))
        layer = Layer(zf.read(findEntry(zf, 'GameModes/%s/GamePlayObjects.con' % spec['source'])).decode('latin1'))
        pool = []
        for n in zf.namelist():
            if n.lower().endswith('gameplayobjects.con') and not n.replace('\\', '/').lower().startswith(layerPrefix):
                other = Layer(zf.read(n).decode('latin1'))
                pool += [p for _, p, _ in other.spawnPoints()] + [p for _, p, _ in other.spawners()]
        heightmap = Heightmap(zf)
        try:
            sourceAreas = zf.read(findEntry(zf, 'GameModes/%s/AI/StrategicAreas.ai' % spec['source'])).decode('latin1')
        except KeyError:
            sourceAreas = ''

        gpo, report, groups, base = buildLayer(spec, layer, pool, heightmap)
        attackers = int(spec['attackers'])
        areas, areasHow = strategicAreas(sourceAreas, groups, base, attackers)
        newFiles = {
            layerPrefix + 'GamePlayObjects.con': gpo,
            layerPrefix + 'AI/StrategicAreas.ai': areas,
            layerPrefix + 'AI/Strategies.ai': strategies(attackers, max(len(g) for g in groups)),
        }

        backup = serverPath + '.before-invasion'
        if not os.path.exists(backup):
            shutil.copyfile(serverPath, backup)
        tmpPath = serverPath + '.building'
        with zipfile.ZipFile(tmpPath, 'w', zipfile.ZIP_DEFLATED) as out:
            for info in zf.infolist():
                if info.filename.replace('\\', '/').lower().startswith(layerPrefix):
                    continue
                out.writestr(info, zf.read(info))
            for name, text in newFiles.items():
                out.writestr(name, text.replace('\r\n', '\n').replace('\n', '\r\n').encode('latin1'))
    os.replace(tmpPath, serverPath)

    info = os.path.join(levelDir, 'Info')
    descName = [f for f in os.listdir(info) if f.lower().endswith('.desc')][0]
    descPath = os.path.join(info, descName)
    if not os.path.exists(descPath + '.before-invasion'):
        shutil.copyfile(descPath, descPath + '.before-invasion')
    text = open(descPath, encoding='latin1', newline='').read()
    open(descPath, 'w', encoding='latin1', newline='').write(addDescMode(text, size))

    menuMaps = {f.lower(): f for f in os.listdir(info)}
    src = menuMaps.get('%s_%s_menumap.png' % (srcMode.lower(), size))
    dst = '%s_%s_menuMap.png' % (MODE, size)
    if src and (dst.lower() not in menuMaps or args.force):
        shutil.copyfile(os.path.join(info, src), os.path.join(info, dst))
        print('menu map: copied from', src)

    print('built %s/%s in %s (strategic areas: %s)' % (MODE, size, levelDir, areasHow))
    print('attacker base:', base['name'])
    for line in report:
        print(' ', line)


if __name__ == '__main__':
    main()
