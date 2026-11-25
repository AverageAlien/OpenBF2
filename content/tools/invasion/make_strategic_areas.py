#!/usr/bin/env python3
"""Drafts AI/StrategicAreas.ai (+ Strategies.ai) for an Invasion layer from its GamePlayObjects.con.

One area per control point (createFromControlPoint), order positions on the flag, the attackers' main
base (a point without supplyGroupId) gets side = its team; an area whose point has object spawners gets
vehicleSearchRadius = distance from the flag to its farthest spawner + 2 m (bots look for free vehicles that
far round the area's position; the engine default comes from the point's radius). Neighbours, all two-way:
  - main base <-> the points of the first group,
  - each point of group n+1 <-> the points of group n that lie close to it (nearest, and any other
    within NEAR_FACTOR x that distance or NEAR_SLACK m more),
  - points of one group within SAME_GROUP_RANGE m of each other.
Links are written as addNeighbour lines in both areas' blocks, the stock layout the OpenBF2 editor reads; it
does not read addTwoWayNeighbour, so such a line would keep a link the editor no longer shows.
No waypoints: add them in the editor's AI strategic areas panel if bots take bad routes.

Usage: make_strategic_areas.py <level dir> <mode> <size> [--force]
"""
import math
import os
import re
import shutil
import sys

NEAR_FACTOR = 1.6
NEAR_SLACK = 40.0
SAME_GROUP_RANGE = 120.0


def read(path):
    with open(path, newline='') as f:
        return f.read()


def control_points(text):
    points, current = {}, None
    for line in text.splitlines():
        words = line.strip().split()
        if len(words) >= 3 and words[0] == 'ObjectTemplate.create':
            current = words[2] if words[1] == 'ControlPoint' else None
            if current:
                points[current] = {'name': current, 'team': 0, 'id': None, 'group': 0}
        elif current and len(words) >= 2:
            key = words[0].split('.', 1)[-1]
            if key == 'team':
                points[current]['team'] = int(words[1])
            elif key == 'controlPointId':
                points[current]['id'] = int(words[1])
            elif key == 'supplyGroupId':
                points[current]['group'] = int(words[1])
    placed = None
    for line in text.splitlines():
        words = line.strip().split()
        if len(words) >= 2 and words[0] == 'Object.create':
            placed = words[1] if words[1] in points else None
        elif placed and len(words) >= 2 and words[0] == 'Object.absolutePosition':
            points[placed]['pos'] = [float(v) for v in words[1].split('/')]
            placed = None
    return [p for p in points.values() if 'pos' in p and p['id'] is not None]


def spawner_positions(text):
    """(controlPointId, position) of every placed object spawner."""
    spawners, out, placed, pos = set(), [], False, None
    for line in text.splitlines():
        words = line.strip().split()
        if len(words) >= 3 and words[0] == 'ObjectTemplate.create' and words[1] == 'ObjectSpawner':
            spawners.add(words[2])
        elif len(words) >= 2 and words[0] == 'Object.create':
            placed, pos = words[1] in spawners, None
        elif placed and len(words) >= 2 and words[0] == 'Object.absolutePosition':
            pos = [float(v) for v in words[1].split('/')]
        elif placed and pos and len(words) >= 2 and words[0] == 'Object.setControlPointId':
            out.append((int(words[1]), pos))
    return out


def distance(a, b):
    return math.hypot(a['pos'][0] - b['pos'][0], a['pos'][2] - b['pos'][2])


def fmt(pos):
    return '/'.join('%g' % round(v, 3) for v in pos)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 3:
        sys.exit(__doc__)
    level, mode, size = args
    modes = 'gamemodes' if os.path.isdir(os.path.join(level, 'gamemodes')) else 'GameModes'
    layer = os.path.join(level, modes, mode, size)
    text = read(os.path.join(layer, 'GamePlayObjects.con'))
    nl = '\r\n' if '\r\n' in text else '\n'
    points = control_points(text)
    spawners = spawner_positions(text)
    mains = [p for p in points if p['group'] <= 0]
    groups = sorted({p['group'] for p in points if p['group'] > 0})
    by_group = [[p for p in points if p['group'] == g] for g in groups]
    if not by_group:
        sys.exit('no control point has a supplyGroupId')

    pairs = []
    def link(a, b):
        if a is not b and (a['name'], b['name']) not in pairs and (b['name'], a['name']) not in pairs:
            pairs.append((a['name'], b['name']))
    for m in mains:
        for p in by_group[0]:
            link(m, p)
    for earlier, later in zip(by_group, by_group[1:]):
        for p in later:
            nearest = min(distance(p, q) for q in earlier)
            for q in earlier:
                d = distance(p, q)
                if d <= max(nearest * NEAR_FACTOR, nearest + NEAR_SLACK):
                    link(q, p)
    for group in by_group:
        for i, p in enumerate(group):
            for q in group[i + 1:]:
                if distance(p, q) <= SAME_GROUP_RANGE:
                    link(p, q)

    out = ['rem *** Create strategic areas ***']
    for p in mains + [p for g in by_group for p in g]:
        out += ['aiStrategicArea.createFromControlPoint %s %d 50' % (p['name'], p['id']), 'aiStrategicArea.layer 1', '']
    out.append('')
    for p in mains + [p for g in by_group for p in g]:
        out.append('aiStrategicArea.setActive %s' % p['name'])
        out += ['AIStrategicArea.addNeighbour %s' % (b if a == p['name'] else a) for a, b in pairs if p['name'] in (a, b)]
        out += ['aiStrategicArea.addObjectTypeFlag ControlPoint',
                'AIStrategicArea.setOrderPosition Infantry %s' % fmt(p['pos']),
                'AIStrategicArea.setOrderPosition Vehicle %s' % fmt(p['pos']),
                'aiStrategicArea.setSide %d' % (p['team'] if p in mains else 0)]
        reach = [math.hypot(pos[0] - p['pos'][0], pos[2] - p['pos'][2]) for cp, pos in spawners if cp == p['id']]
        if reach:
            out.append('aiStrategicArea.vehicleSearchRadius %g' % round(max(reach) + 2, 1))
        out.append('')

    ai = next((os.path.join(layer, d) for d in os.listdir(layer)
               if d.lower() == 'ai' and os.path.isdir(os.path.join(layer, d))), os.path.join(layer, 'AI'))
    target = os.path.join(ai, 'StrategicAreas.ai')
    if os.path.exists(target) and '--force' not in sys.argv:
        sys.exit(target + ' exists (use --force; a .bak copy is kept)')
    os.makedirs(ai, exist_ok=True)
    if os.path.exists(target):
        shutil.copy2(target, target + '.bak')
    with open(target, 'w', newline='') as f:
        f.write(nl.join(out) + nl)
    strategies = os.path.join(ai, 'Strategies.ai')
    if not os.path.exists(strategies):
        source = next((os.path.join(level, modes, mode, s, 'AI', 'Strategies.ai') for s in ('16', '32', '64')
                       if os.path.exists(os.path.join(level, modes, mode, s, 'AI', 'Strategies.ai'))), None)
        if source:
            shutil.copy2(source, strategies)
            print('copied', source)
    print('wrote', target)
    for g, group in zip(groups, by_group):
        print('group %d: %s' % (g, ', '.join(p['name'] for p in group)))
    for a, b in pairs:
        print('  %s <-> %s' % (a, b))


if __name__ == '__main__':
    main()
