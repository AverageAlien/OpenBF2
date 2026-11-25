#!/usr/bin/env python3
"""Port a BF2 level from one mod to another (e.g. NAW -> vanilla bf2).

Usage:
  port_map.py analyze <level> <source_mod> <target_mod> --spec <spec.json> [--report <report.json>]
  port_map.py build <spec.json> [--stage <dir>] [--force]

<source_mod>/<target_mod>: a mod folder name (searched in Game/*/mods/) or a path to a mod folder.

analyze: indexes both mods' archives (ServerArchives.con/ClientArchives.con), then reports
  - teams, kits, flags and drop vehicles from Init.con, and which of them the target lacks
  - spawner templates per layer (team 1 | team 2 pairs) that the target lacks: these need a
    replacement or removal (or "port" to copy them as-is)
  - static objects, road templates and other files (textures, sounds) the target lacks: these are
    copied from the source
  - Strategies.ai files that run scripts outside the level (e.g. ESAI)
  and writes a spec skeleton to fill in.

build: reads the filled spec and
  - writes <target>/Levels/<level>/ (client.zip as-is, server.zip converted, Info/ with edited .desc)
  - adds the missing source files to <target>/<prefix>_<mount>.zip (one per mount point: objects,
    common, menu), merging with existing ones, and mounts them last in both archive lists
  - writes the level's localization keys into <target>/Localization/English/English_<PREFIX>.utxt
  With --stage, everything goes into <dir> instead of the target mod (dry run).
"""
import argparse
import json
import os
import posixpath
import re
import shutil
import sys
import zipfile

# Folder holding game installs (each with mods/), searched for mods given by name; mods can also be
# given as paths. Default: the folder above BF2_GAME_DIR.
GAMES = os.environ.get('BF2_GAMES_DIR') or os.path.dirname(os.path.abspath(os.environ.get('BF2_GAME_DIR', '.')))
SPAWN_TEAMS = ('1', '2')
TEXT_EXT = ('.con', '.tweak', '.inc', '.ai')
PATH_RE = re.compile(rb'[A-Za-z0-9_\-/\\\. ]+\.(?:dds|tga|wav|ogg|baf|ske|inc|con|tweak)', re.I)
LEVEL_PATH_RE = re.compile(rb'(?:common|objects|menu)[\\/][A-Za-z0-9_\-\\/\.]+', re.I)
TEAM_LINE_RE = re.compile(r'^\s*gameLogic\.(setTeamName|setTeamLanguage|setTeamFlag|setKit)\s+(\d)\b', re.I)


def die(msg):
    sys.exit('error: ' + msg)


def ci_path(base, rel):
    """Resolve rel under base case-insensitively, or None."""
    cur = base
    for part in rel.replace('\\', '/').split('/'):
        if not part:
            continue
        if not os.path.isdir(cur):
            return None
        hit = [e for e in os.listdir(cur) if e.lower() == part.lower()]
        if not hit:
            return None
        cur = os.path.join(cur, hit[0])
    return cur


def find_mod(arg, level=None):
    if os.path.isdir(arg):
        return os.path.abspath(arg)
    found = []
    for game in sorted(os.listdir(GAMES)):
        d = ci_path(os.path.join(GAMES, game), 'mods/' + arg)
        if d and os.path.isdir(d):
            found.append(d)
    if level:
        with_level = [d for d in found if ci_path(d, 'levels/' + level)]
        if with_level:
            found = with_level
    if not found:
        die('mod %r not found in %s/*/mods' % (arg, GAMES))
    if len(found) > 1:
        die('mod %r is ambiguous, pass a path: %s' % (arg, ', '.join(found)))
    return found[0]


def read_text(data):
    return data.decode('latin1')


class Mod:
    """File and template index of a mod's mounted archives."""

    def __init__(self, path, exclude=()):
        self.path = path
        self.archives = []  # (zip path, mount)
        self.mount_lines = {}
        for lst in ('ServerArchives.con', 'ClientArchives.con'):
            f = ci_path(path, lst)
            if not f:
                continue
            for line in read_text(open(f, 'rb').read()).splitlines():
                p = line.split()
                if len(p) >= 3 and p[0].lower() == 'filemanager.mountarchive':
                    z = ci_path(path, p[1])
                    if z and os.path.basename(z).lower() not in exclude and (z, p[2].lower()) not in self.archives:
                        self.archives.append((z, p[2].lower()))
        self.files = {}  # 'objects/path/file' -> (zip, name)
        self.templates = {}  # name -> [(key, type)]
        self._zips = {}
        for z, mount in self.archives:
            zf = self.zip(z)
            for n in zf.namelist():
                if n.endswith('/'):
                    continue
                key = (mount + '/' + n).lower().replace('\\', '/')
                if key in self.files:
                    continue
                self.files[key] = (z, n)
                if key.endswith(('.con', '.tweak')) and mount == 'objects':
                    for m in re.finditer(rb'^\s*ObjectTemplate\.create\s+(\S+)\s+(\S+)', zf.read(n), re.I | re.M):
                        self.templates.setdefault(m.group(2).decode('latin1').lower(), []).append(
                            (key, m.group(1).decode('latin1')))
        self.dirfiles = {}
        for k in self.files:
            d = posixpath.dirname(k)
            while d:
                self.dirfiles.setdefault(d, []).append(k)
                d = posixpath.dirname(d)

    def zip(self, z):
        if z not in self._zips:
            self._zips[z] = zipfile.ZipFile(z)
        return self._zips[z]

    def read(self, key):
        z, n = self.files[key]
        return self.zip(z).read(n)

    def has_path(self, p):
        p = p.lower().replace('\\', '/').lstrip('/')
        stem, ext = os.path.splitext(p)
        if ext in ('.tga', '.dds'):
            # meshes often name a .tga while the archive has the .dds; the engine takes either
            cands = [p, stem + ('.dds' if ext == '.tga' else '.tga')]
        else:
            cands = [p] if ext else [p + '.dds', p + '.tga', p + '.con', p]
        if p.split('/')[0] not in ('objects', 'common', 'menu'):
            cands += ['objects/' + c for c in cands]
        return next((c for c in cands if c in self.files), None)

    def template_dir(self, t):
        defs = self.templates[t]
        best = next((k for k, typ in defs if '_server.zip' in self.files[k][0].lower()), defs[0][0])
        return posixpath.dirname(best)

    def materials(self):
        key = self.has_path('common/material/materialmanagerdefine.con')
        out, cur = {}, None
        if not key:
            return out
        for line in read_text(self.read(key)).splitlines():
            m = re.match(r'\s*Material\.active\s+(\d+)', line, re.I)
            if m:
                cur = int(m.group(1))
            m = re.match(r'\s*Material\.name\s+"?([^"\s]+)', line, re.I)
            if m and cur is not None:
                out[cur] = m.group(1)
        return out


def game_lines(text):
    """Lines of a .con outside 'if v_arg1 == BF2Editor' branches."""
    out, stack = [], []
    for line in text.splitlines():
        s = line.strip().lower()
        if s.startswith('if '):
            stack.append('bf2editor' in s and '==' in s)
            continue
        if s == 'else' and stack:
            stack[-1] = not stack[-1]
            continue
        if s == 'endif' and stack:
            stack.pop()
            continue
        if not any(stack):
            out.append(line)
    return out


class Level:
    def __init__(self, mod, name):
        self.dir = ci_path(mod.path, 'levels/' + name)
        if not self.dir:
            die('level %s not found in %s' % (name, mod.path))
        self.name = os.path.basename(self.dir)
        self.server_zip = ci_path(self.dir, 'server.zip')
        self.client_zip = ci_path(self.dir, 'client.zip')
        self.server = zipfile.ZipFile(self.server_zip)
        self.client = zipfile.ZipFile(self.client_zip)

    def sfile(self, name):
        hit = [n for n in self.server.namelist() if n.lower() == name.lower()]
        return read_text(self.server.read(hit[0])) if hit else ''

    def layers(self):
        out = []
        for n in self.server.namelist():
            m = re.match(r'gamemodes/([^/]+)/([^/]+)/gameplayobjects\.con$', n, re.I)
            if m:
                out.append((m.group(1) + '/' + m.group(2), n))
        return sorted(out)

    def strategies(self):
        return [n for n in self.server.namelist() if re.match(r'gamemodes/[^/]+/[^/]+/ai/strategies\.ai$', n, re.I)]


def local_templates(level):
    out = set()
    for n in level.server.namelist():
        if n.lower().endswith('.con'):
            for m in re.finditer(r'^\s*ObjectTemplate\.create\s+\S+\s+(\S+)', read_text(level.server.read(n)), re.I | re.M):
                out.add(m.group(1).lower())
    return out


def spawners(text):
    """{spawner name: {team: template}} from a GamePlayObjects.con."""
    out, cur = {}, None
    for line in text.splitlines():
        p = line.split()
        if len(p) >= 3 and p[0].lower() == 'objecttemplate.create':
            cur = p[2] if p[1].lower() == 'objectspawner' else None
            if cur:
                out[cur] = {}
        elif cur and len(p) >= 3 and p[0].lower() == 'objecttemplate.setobjecttemplate':
            out[cur][p[1]] = p[2]
    return out


def level_object_refs(level):
    """Templates the level creates outside spawners: {template: [where]}."""
    refs = {}

    def add(t, where):
        refs.setdefault(t.lower(), set()).add(where)

    for line in game_lines(level.sfile('StaticObjects.con')):
        m = re.match(r'\s*Object\.create\s+(\S+)', line, re.I)
        if m:
            add(m.group(1), 'StaticObjects.con')
    for line in level.sfile('CompiledRoads.con').splitlines():
        m = re.match(r'\s*Object\.create\s+(\S+)', line, re.I)
        if m:
            add(m.group(1), 'CompiledRoads.con')
    for line in level.sfile('overgrowth/Overgrowth.con').splitlines():
        m = re.match(r'\s*OvergrowthType\.geometry\s+(\S+)', line, re.I)
        if m:
            add(m.group(1), 'Overgrowth.con')
    for layer, n in level.layers():
        for line in read_text(level.server.read(n)).splitlines():
            m = re.match(r'\s*Object\.create\s+(\S+)', line, re.I)
            if m:
                add(m.group(1), layer)
    return refs


def level_paths(level):
    """Paths to mod files (common/, objects/, menu/) named in the level's own files."""
    found = {}
    for zf, label in ((level.server, 'server'), (level.client, 'client')):
        for n in zf.namelist():
            ln = n.lower()
            if ln.endswith('/') or ln.startswith('lightmaps/') or ln.endswith(('.qtr', '.clb', '.ahm', '.dds', '.mesh')):
                continue
            if ln.endswith('.raw') and not ln.endswith('terraindata.raw'):
                continue
            data = zf.read(n)
            if ln.endswith('.con'):
                data = '\n'.join(game_lines(read_text(data))).encode('latin1')
            for m in LEVEL_PATH_RE.finditer(data):
                p = read_text(m.group(0)).strip().lower().replace('\\', '/').rstrip('/')
                found.setdefault(p, set()).add(label + ':' + n)
    return found


class Closure:
    """Files to copy from the source so that the given templates and paths work in the target."""

    def __init__(self, src, dst):
        self.src, self.dst = src, dst
        self.templates, self.files, self.unresolved, self.scanned = set(), set(), {}, set()
        self.queue = []

    def add_template(self, t):
        if t not in self.templates and t in self.src.templates and t not in self.dst.templates:
            self.queue.append(t)

    def add_path(self, p, origin):
        if self.dst.has_path(p):
            return
        hit = self.src.has_path(p)
        if hit:
            if hit not in self.files:
                self.files.add(hit)
                self.scan(hit)
        elif '/' in p:
            self.unresolved.setdefault(p, set()).add(origin)

    def scan(self, k):
        if k in self.scanned:
            return
        self.scanned.add(k)
        data = self.src.read(k)
        if k.endswith(TEXT_EXT):
            for line in read_text(data).splitlines():
                p = line.split()
                if len(p) < 2:
                    continue
                cmd = p[0].lower()
                if cmd.startswith(('rem', 'geometrytemplate', 'roadtemplate', 'objecttemplate.create', 'objecttemplate.activesafe', 'collisionmanager')):
                    continue
                if 'template' in cmd or 'effect' in cmd:
                    for w in p[1:]:
                        if not w.replace('.', '').isdigit():
                            self.add_template(w.lower())
        for m in PATH_RE.finditer(data):
            p = read_text(m.group(0)).strip().lower().replace('\\', '/').lstrip('/')
            if p.endswith(('.con', '.tweak', '.inc')) and '/' not in p:
                continue
            self.add_path(p, k)

    def run(self):
        while self.queue:
            t = self.queue.pop()
            if t in self.templates:
                continue
            self.templates.add(t)
            d = self.src.template_dir(t)
            if posixpath.basename(d) == t:
                own = self.src.dirfiles.get(d, [])
            else:
                stems = (t, t + '_compiled')
                own = [k for k in self.src.dirfiles.get(d, [])
                       if posixpath.dirname(k) in (d, d + '/meshes') and posixpath.basename(k).split('.')[0] in stems]
            for k in own:
                if k not in self.dst.files:
                    self.files.add(k)
            for k in own:
                self.scan(k)

    def materials(self):
        used = {}
        for k in self.files:
            if k.endswith(('.con', '.tweak')):
                for m in re.finditer(rb'mapMaterial\s+\d+\s+(\S+)\s+(\d+)', self.src.read(k), re.I):
                    used.setdefault(int(m.group(2)), set()).add(k)
                for m in re.finditer(rb'defaultMaterial\s+(\d+)', self.src.read(k), re.I):
                    used.setdefault(int(m.group(1)), set()).add(k)
        sm, dm = self.src.materials(), self.dst.materials()
        return {i: (sm.get(i), dm.get(i), sorted(used[i])[:3]) for i in sorted(used) if sm.get(i) != dm.get(i)}


def collect(level, src, dst, port_templates=()):
    refs = level_object_refs(level)
    local = local_templates(level)
    missing_objects = {t: sorted(w) for t, w in refs.items() if t not in local and t not in dst.templates}
    layer_spawn = {}
    missing_spawn = {}
    for layer, n in level.layers():
        sp = spawners(read_text(level.server.read(n)))
        layer_spawn[layer] = sp
        for name, teams in sp.items():
            for team, t in teams.items():
                if t.lower() not in dst.templates:
                    missing_spawn.setdefault(t.lower(), {'name': t, 'count': 0, 'layers': set()})
                    missing_spawn[t.lower()]['count'] += 1
                    missing_spawn[t.lower()]['layers'].add(layer)
    clo = Closure(src, dst)
    statics = [t for t in missing_objects if not any('/' in w for w in missing_objects[t])]
    for t in statics + [t.lower() for t in port_templates]:
        clo.add_template(t)
    for p, where in level_paths(level).items():
        for w in where:
            clo.add_path(p, w)
    clo.run()
    return refs, missing_objects, layer_spawn, missing_spawn, clo


def init_teams(text):
    teams = {}
    for line in text.splitlines():
        m = TEAM_LINE_RE.match(line)
        if m:
            teams.setdefault(m.group(2), []).append(line.strip())
    drops = [l.strip() for l in text.splitlines() if re.match(r'\s*gameLogic\.setTeamDropVehicle\b', l, re.I)]
    return teams, drops


def localization_keys(level):
    keys = {'hud_levelname_' + level.name.lower()}
    for layer, n in level.layers():
        for m in re.finditer(r'setControlPointName\s+(\S+)', read_text(level.server.read(n)), re.I):
            keys.add(m.group(1).lower())
    return keys


def source_localization(src, keys, lang='english'):
    d = ci_path(src.path, 'localization/' + lang)
    out = {}
    if not d:
        return out
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith('.utxt'):
            continue
        for line in open(os.path.join(d, f), 'rb').read().decode('utf-16').split('\r\n'):
            p = line.split(None, 1)
            if p and p[0].lower() in keys:
                out.setdefault(p[0].lower(), line)
    return out


def analyze(args):
    src = Mod(find_mod(args.source_mod, args.level))
    dst = Mod(find_mod(args.target_mod), exclude=set(a.lower() for a in args.exclude_archive))
    level = Level(src, args.level)
    if ci_path(dst.path, 'levels/' + level.name):
        print('NOTE: target already has Levels/%s (build needs --force to replace it)' % level.name)
    refs, missing_objects, layer_spawn, missing_spawn, clo = collect(level, src, dst)
    init = level.sfile('Init.con')
    teams, drops = init_teams(init)
    print('level   %s\nsource  %s\ntarget  %s\nlayers  %s' % (level.dir, src.path, dst.path, ', '.join(l for l, _ in level.layers())))
    print('\n== Init.con teams')
    for team in sorted(teams):
        for line in teams[team]:
            names = re.findall(r'"([^"]+)"', line)
            miss = [n for n in names if n.lower() not in dst.templates and not line.lower().startswith(('gamelogic.setteamname', 'gamelogic.setteamlanguage'))]
            print('  %s%s' % (line, '   <-- target lacks: ' + ', '.join(miss) if miss else ''))
    for line in drops:
        print('  %s' % line)
    print('\n== Spawner pairs per layer (team 1 | team 2), * = target lacks it')
    seen_layers = {}
    for layer, sp in layer_spawn.items():
        pairs = {}
        for name, t in sp.items():
            key = tuple(t.get(x, '-') for x in SPAWN_TEAMS)
            pairs[key] = pairs.get(key, 0) + 1
        sig = tuple(sorted(pairs.items()))
        if sig in seen_layers:
            print('  %s: same as %s' % (layer, seen_layers[sig]))
            continue
        seen_layers[sig] = layer
        print('  %s:' % layer)
        for (a, b), n in sorted(pairs.items(), key=lambda x: -x[1]):
            mark = lambda t: ('*' if t != '-' and t.lower() not in dst.templates else '') + t
            print('    %3d  %s | %s' % (n, mark(a), mark(b)))
    print('\n== Target catalogue: vehicles and stationary weapons (template  path)')
    for t, defs in sorted(dst.templates.items()):
        key, typ = defs[0]
        parts = key.split('/')
        if typ.lower() == 'playercontrolobject' and parts[-1] == t + '.con' and parts[1] in ('vehicles', 'weapons', 'staticobjects', 'freecontent'):
            print('  %-24s %s' % (t, '/'.join(parts[1:-1])))
    print('\n== Spawned templates the target lacks (need replace / remove / port); count = spawners over all layers')
    for t, info in sorted(missing_spawn.items()):
        d = src.template_dir(t) if t in src.templates else '?'
        typ = src.templates[t][0][1] if t in src.templates else '?'
        print('  %-28s %3d  %-20s %s' % (info['name'], info['count'], typ, d))
    gpo_objects = {t: w for t, w in missing_objects.items() if any('/' in x for x in w)}
    if gpo_objects:
        print('\n== Templates created directly in layers that the target lacks')
        for t, w in sorted(gpo_objects.items()):
            print('  %-28s %s' % (t, ', '.join(w)))
    print('\n== Copied from the source: %d templates, %d files, %d MB' % (
        len(clo.templates), len(clo.files), sum(src.zip(src.files[k][0]).getinfo(src.files[k][1]).file_size for k in clo.files) >> 20))
    mounts = {}
    for k in clo.files:
        mounts.setdefault(k.split('/')[0], []).append(k)
    for m, ks in sorted(mounts.items()):
        print('  %s: %d files' % (m, len(ks)))
        if m != 'objects':
            for k in sorted(ks):
                print('    ' + k)
    nosrc = [t for t, w in missing_objects.items() if t not in src.templates]
    if nosrc:
        print('  NOT FOUND in source either (editor-only objects are fine): ' + ', '.join(sorted(nosrc)))
    for p, origin in sorted(clo.unresolved.items()):
        print('  unresolved path %s (from %s)' % (p, ', '.join(sorted(origin)[:2])))
    mats = clo.materials()
    print('\n== Collision materials: ' + ('all IDs match' if not mats else 'MISMATCH'))
    for i, (s, d, ks) in mats.items():
        print('  id %d: source %s, target %s (%s)' % (i, s, d, ', '.join(ks)))
    print('\n== Strategies.ai')
    for n in level.strategies():
        runs = [l.strip() for l in read_text(level.server.read(n)).splitlines() if l.strip().lower().startswith('run ')]
        print('  %s: %s' % (n, ', '.join(runs[:2]) + (' ...' if len(runs) > 2 else '') if runs else 'self-contained'))
    desc = ci_path(level.dir, 'info/%s.desc' % level.name)
    desc_text = read_text(open(desc, 'rb').read()) if desc else ''
    name = re.search(r'<name>(.*?)</name>', desc_text, re.S)
    music = re.search(r'<music>\s*(\S+)\s*</music>', desc_text)
    print('\n== Info/.desc\n  name: %s\n  music: %s%s' % (
        name.group(1).strip() if name else '?', music.group(1) if music else '-',
        '' if not music or dst.has_path(music.group(1)) else '   <-- target lacks it'))
    if music and not dst.has_path(music.group(1)):
        print('  target music: ' + ', '.join(sorted(k for k in dst.files if k.startswith('common/sound/menu/music/'))))
    keys = localization_keys(level)
    loc = source_localization(src, keys)
    print('\n== Localization: %d of %d keys found in the source' % (len(loc), len(keys)))
    for k in sorted(keys - set(loc)):
        print('  missing: ' + k)
    spec = {
        'level': level.name,
        'source_mod': src.path,
        'target_mod': dst.path,
        'archive_prefix': os.path.basename(src.path).lower(),
        'teams': {t: {'from_level': None, 'from_team': None} for t in SPAWN_TEAMS},
        'remove_drop_vehicles': bool(drops),
        'replace': {info['name']: None for info in missing_spawn.values()},
        'remove': [],
        'port': [],
        'strategies': 'drop' if level.strategies() else 'none',
        'desc': {'name': name.group(1).strip() if name else level.name,
                 'music': music.group(1) if music and dst.has_path(music.group(1)) else None,
                 'replace': {}},
        'localization_replace': {},
        'init_replace': {},
    }
    if gpo_objects:
        spec['replace'].update({t: None for t in gpo_objects})
    json.dump(spec, open(args.spec, 'w'), indent=2)
    print('\nspec skeleton written to ' + args.spec)


def team_block(dst, spec, src_init, nl):
    lines = []
    neutral = next((l.strip() for l in src_init.splitlines() if re.match(r'\s*gameLogic\.setTeamFlag\s+0\b', l, re.I)), None)
    per = {}
    for team in SPAWN_TEAMS:
        t = spec['teams'][team]
        ref = Level(dst, t['from_level'])
        rl = [l.strip() for l in ref.sfile('Init.con').splitlines()]
        get = lambda cmd: [l for l in rl if re.match(r'gameLogic\.%s\s+%s\b' % (cmd, t['from_team']), l, re.I)]
        renum = lambda l: re.sub(r'^(gameLogic\.\S+\s+)\d', lambda m: m.group(1) + team, l, flags=re.I)
        per[team] = {c: [renum(l) for l in get(c)] for c in ('setTeamName', 'setTeamLanguage', 'setTeamFlag', 'setKit')}
        if not per[team]['setKit']:
            die('no kits for team %s in %s Init.con' % (t['from_team'], t['from_level']))
    for c in ('setTeamName', 'setTeamLanguage'):
        lines += per['1'][c] + per['2'][c] + ['']
    lines += ([neutral] if neutral else []) + per['1']['setTeamFlag'] + per['2']['setTeamFlag'] + ['']
    kits = lambda team: {re.match(r'\S+\s+\d\s+(\d+)', l).group(1): l for l in per[team]['setKit']}
    k1, k2 = kits('1'), kits('2')
    for slot in sorted(set(k1) | set(k2), key=int):
        lines += [k for k in (k1.get(slot), k2.get(slot)) if k] + ['']
    return nl.join(lines[:-1]) + nl


def convert_init(text, block, remove_drops):
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.split(nl)
    idx = [i for i, l in enumerate(lines) if TEAM_LINE_RE.match(l)]
    if not idx:
        die('Init.con has no team lines')
    first, last = idx[0], idx[-1]
    out = lines[:first] + block.rstrip(nl).split(nl) + lines[last + 1:]
    if remove_drops:
        out = [l for l in out if not re.match(r'\s*gameLogic\.setTeamDropVehicle\b', l, re.I)]
    return nl.join(out)


def convert_gpo(text, replace, remove):
    nl = '\r\n' if '\r\n' in text else '\n'
    blocks = re.split('(%s[ \t]*%s)' % (nl, nl), text)
    removed = set()
    for i in range(0, len(blocks), 2):
        b = blocks[i]
        m = re.search(r'^\s*ObjectTemplate\.create ObjectSpawner (\S+)', b, re.M | re.I)
        if not m:
            continue
        out, kept, had = [], 0, 0
        for line in b.split(nl):
            mm = re.match(r'(\s*ObjectTemplate\.setObjectTemplate \d )(\S+)\s*$', line, re.I)
            if mm:
                had += 1
                t = mm.group(2).lower()
                if t in remove:
                    continue
                line = mm.group(1) + replace.get(t, mm.group(2))
                kept += 1
            out.append(line)
        if had and not kept:
            removed.add(m.group(1).lower())
            blocks[i] = None
        else:
            blocks[i] = nl.join(out)
    for i in range(0, len(blocks), 2):
        b = blocks[i]
        if b is None or 'ObjectTemplate.create' in b:
            continue
        m = re.search(r'^(\s*Object\.create )(\S+)\s*$', b, re.M)
        if not m:
            continue
        t = m.group(2).lower()
        if t in removed or t in remove:
            blocks[i] = None
        elif t in replace:
            blocks[i] = b[:m.start(2)] + replace[t] + b[m.end(2):]
    out = []
    for i in range(0, len(blocks), 2):
        if blocks[i] is not None:
            out.append(blocks[i])
            if i + 1 < len(blocks):
                out.append(blocks[i + 1])
    return ''.join(out), removed


def merge_zip(path, entries):
    """Write entries {name: bytes} into zip path, keeping existing entries. Returns (added, kept)."""
    old = {}
    if os.path.exists(path):
        with zipfile.ZipFile(path) as z:
            old = {n: z.read(n) for n in z.namelist()}
    lower = {n.lower() for n in old}
    added = {n: d for n, d in entries.items() if n.lower() not in lower}
    if not added:
        return 0, len(old)
    tmp = path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, d in list(old.items()) + sorted(added.items()):
            z.writestr(n, d)
    os.replace(tmp, path)
    return len(added), len(old)


def add_mount(con, line, backup):
    data = open(con, 'rb').read()
    if line.encode().lower() in data.lower():
        return False
    if not os.path.exists(backup):
        shutil.copy2(con, backup)
    nl = b'\r\n' if b'\r\n' in data else b'\n'
    lines = data.split(nl)
    idx = max(i for i, l in enumerate(lines) if l.lower().startswith(b'filemanager.mountarchive'))
    lines.insert(idx + 1, line.encode())
    open(con, 'wb').write(nl.join(lines))
    return True


def build(args):
    spec = json.load(open(args.spec))
    src = Mod(spec['source_mod'])
    dst = Mod(spec['target_mod'], exclude=set(a.lower() for a in args.exclude_archive))
    level = Level(src, spec['level'])
    replace = {k.lower(): v for k, v in spec['replace'].items() if v}
    remove = {t.lower() for t in spec['remove']}
    port = [t.lower() for t in spec['port']]
    unset = [k for k, v in spec['replace'].items() if not v and k.lower() not in remove and k.lower() not in port]
    if unset:
        die('spec: no replacement, removal or port for: ' + ', '.join(unset))
    for t in set(replace.values()):
        if t.lower() not in dst.templates:
            die('replacement %s does not exist in the target' % t)
    refs, missing_objects, layer_spawn, missing_spawn, clo = collect(level, src, dst, port)
    left = [t for t in missing_spawn if t not in replace and t not in remove and t not in clo.templates]
    if left:
        die('spawned templates still missing in the target: ' + ', '.join(left))
    out_root = args.stage or dst.path
    out_level = os.path.join(out_root, 'Levels', level.name)
    if os.path.exists(out_level) and not args.force:
        die('%s exists (use --force to replace it)' % out_level)

    init = level.sfile('Init.con')
    nl = '\r\n' if '\r\n' in init else '\n'
    changed = {}
    for n in level.server.namelist():
        if n.lower() == 'init.con':
            text = convert_init(init, team_block(dst, spec, init, nl), spec.get('remove_drop_vehicles'))
            for a, b in spec.get('init_replace', {}).items():
                text = text.replace(a, b)
            changed[n] = text.encode('latin1')
    report = []
    for layer, n in level.layers():
        text, removed = convert_gpo(read_text(level.server.read(n)), replace, remove)
        changed[n] = text.encode('latin1')
        report.append('%s: removed spawners %s' % (layer, ', '.join(sorted(removed)) or '-'))
    dropped = set()
    for n in level.strategies():
        if spec['strategies'] == 'drop':
            dropped.add(n)
        elif spec['strategies'] == 'keep':
            t = read_text(level.server.read(n))
            srcname = os.path.basename(src.path)
            t2 = re.sub(r'(?i)/mods/%s/' % re.escape(srcname), '/mods/%s/' % os.path.basename(dst.path), t)
            for p in re.findall(r'(?im)^\s*run\s+/mods/[^/]+/(\S+)', t2):
                if not ci_path(dst.path, p):
                    die('Strategies.ai runs %s, which the target lacks (copy it first or use "drop")' % p)
            changed[n] = t2.encode('latin1')

    if os.path.exists(out_level):
        shutil.rmtree(out_level)
    os.makedirs(out_level)
    shutil.copy2(level.client_zip, os.path.join(out_level, 'client.zip'))
    with zipfile.ZipFile(os.path.join(out_level, 'server.zip'), 'w', zipfile.ZIP_DEFLATED) as zo:
        for info in level.server.infolist():
            if info.filename in dropped:
                continue
            data = changed.get(info.filename, level.server.read(info.filename))
            # a fresh ZipInfo: writestr() rewrites offsets in the one it gets, which would break the source zip
            zo.writestr(zipfile.ZipInfo(info.filename, info.date_time), data, zipfile.ZIP_DEFLATED)
    info_src = ci_path(level.dir, 'info')
    info_dst = os.path.join(out_level, 'Info')
    shutil.copytree(info_src, info_dst)
    desc = ci_path(info_dst, level.name + '.desc')
    if desc:
        t = read_text(open(desc, 'rb').read())
        t = re.sub(r'<name>.*?</name>', '<name> %s</name>' % spec['desc']['name'], t, count=1, flags=re.S)
        if spec['desc'].get('music'):
            t = re.sub(r'<music>.*?</music>', '<music> %s </music>' % spec['desc']['music'], t, count=1, flags=re.S)
        for a, b in spec['desc'].get('replace', {}).items():
            t = t.replace(a, b)
        open(desc, 'wb').write(t.encode('latin1'))

    prefix = spec['archive_prefix']
    by_mount = {}
    for k in clo.files:
        z, n = src.files[k]
        by_mount.setdefault(k.split('/')[0], {})[n] = src.zip(z).read(n)
    for mount, entries in sorted(by_mount.items()):
        zname = '%s_%s.zip' % (prefix, mount)
        added, kept = merge_zip(os.path.join(out_root, zname), entries)
        report.append('%s: %d files added, %d already there' % (zname, added, kept))
        mount_name = {'objects': 'Objects', 'common': 'Common', 'menu': 'Menu'}[mount]
        for lst in ('ServerArchives.con', 'ClientArchives.con'):
            con = ci_path(dst.path, lst)
            target = os.path.join(out_root, os.path.basename(con))
            if args.stage and not os.path.exists(target):
                shutil.copy2(con, target)
            if add_mount(target, 'fileManager.mountArchive %s %s' % (zname, mount_name), target + '.before-' + level.name):
                report.append('%s: mounted %s' % (lst, zname))

    keys = localization_keys(level)
    loc = source_localization(src, keys)
    if loc:
        d = os.path.join(out_root, 'Localization', 'English')
        os.makedirs(d, exist_ok=True)
        f = os.path.join(d, 'English_%s.utxt' % prefix.upper())
        lines = open(f, 'rb').read().decode('utf-16').split('\r\n') if os.path.exists(f) else []
        lines = [l for l in lines if l.strip() and l.split(None, 1)[0].lower() not in loc]
        for k in loc:
            line = loc[k]
            for a, b in spec.get('localization_replace', {}).items():
                line = line.replace(a, b)
            lines.append(line)
        open(f, 'wb').write(('\r\n'.join(lines) + '\r\n').encode('utf-16'))
        report.append('%s: %d keys' % (f, len(loc)))
    if dropped:
        report.append('Strategies.ai removed: ' + ', '.join(sorted(dropped)))
    report.append('level written to ' + out_level)
    print('\n'.join(report))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('analyze')
    a.add_argument('level')
    a.add_argument('source_mod')
    a.add_argument('target_mod')
    a.add_argument('--spec', required=True)
    a.add_argument('--exclude-archive', action='append', default=[], help='ignore this target archive (testing)')
    b = sub.add_parser('build')
    b.add_argument('spec')
    b.add_argument('--stage')
    b.add_argument('--force', action='store_true')
    b.add_argument('--exclude-archive', action='append', default=[], help='ignore this target archive (testing)')
    args = ap.parse_args()
    analyze(args) if args.cmd == 'analyze' else build(args)


if __name__ == '__main__':
    main()
