#!/usr/bin/env python3
"""Generate BF2 menu maps (Info/<gamemode>_<size>_menumap.png) for an installed level.

Linux port of worldlife's "vBF2 style info map generator" v1.1, with X-Ray's restyled
resources (maps/areas) and the original tool's conParser.py and utct.tga.
Reads everything from the level's server.zip / client.zip, writes real PNG files (like NAW;
--format dds writes DXT1 DDS under the .png name, like vanilla BF2; the game reads both)
and keeps a .bak of each replaced file.

Usage:
  generate_menumap.py <map> <layer> [<layer> ...] [--mod naw] [--also DIR ...]
                      [--style desert|woodland] [--format png|dds] [--preview-dir DIR] [--dry-run]

Layers: "64" (every layer of that size), "sp3" (every size of that gamemode),
"sp3/64" (one layer) or "all".
"""
import argparse
import io
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter

import conParser

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
GAME_MODS_DIR = os.path.join(os.environ.get("BF2_GAME_DIR", "."), "mods")

MAPS_DIR = os.path.join(TOOL_DIR, "maps")
COMBATAREA_IMAGES = {
    "desert": os.path.join(MAPS_DIR, "areas", "CombatAreaDesert.png"),
    "woodland": os.path.join(MAPS_DIR, "areas", "CombatAreaWoodland.png"),
}
COVER_IMAGE = os.path.join(MAPS_DIR, "areas", "Cover.png")
MINIMAPBORDER_IMAGE = os.path.join(MAPS_DIR, "areas", "MinimapBorder.png")
FLAGS_DIR = os.path.join(MAPS_DIR, "flags")
UTCT_IMAGE = os.path.join(FLAGS_DIR, "utct.tga")

OUTPUT_SIZE = (1024, 1024)
BBOX_EXPAND_SCALE = 1.25
MINIMAP_POSITION = (833, 832)
MINIMAP_SIZE = (164, 164)
MINIMAP_LINEWIDTH = 5
MINIMAP_BOUNDINGBOX_COLOR = (255, 0, 0, 255)
PROJECTION_VECTOR = (-4, 4)
PROJECTION_BLUR = 2
PROJECTION_SHADOWCOLOR = (20, 20, 20, 225)

BILINEAR = Image.Resampling.BILINEAR


class ControlPoint:
    def __init__(self):
        self.team = 0
        self.utct = 0
        self.showOnMinimap = 1
        self.position = (0.0, 0.0, 0.0)


def find_ci(directory, name):
    """Case-insensitive lookup of a direct child of directory."""
    if not os.path.isdir(directory):
        return None
    for entry in os.listdir(directory):
        if entry.lower() == name.lower():
            return os.path.join(directory, entry)
    return None


class LevelZips:
    def __init__(self, level_dir):
        self.server = zipfile.ZipFile(os.path.join(level_dir, "server.zip"))
        self.client = zipfile.ZipFile(os.path.join(level_dir, "client.zip"))

    @staticmethod
    def _read(zf, path):
        for name in zf.namelist():
            if name.lower() == path.lower():
                return zf.read(name)
        raise FileNotFoundError("%s not found in %s" % (path, zf.filename))

    def con(self, path):
        with tempfile.NamedTemporaryFile(suffix=".con", delete=False) as tmp:
            tmp.write(self._read(self.server, path))
        try:
            return conParser.readCon(tmp.name)
        finally:
            os.unlink(tmp.name)

    def layers(self):
        found = set()
        for name in self.server.namelist():
            parts = name.split("/")
            if len(parts) == 4 and parts[0].lower() == "gamemodes" and parts[3].lower() == "gameplayobjects.con":
                found.add((parts[1], parts[2]))
        return sorted(found)

    def ingame_map(self):
        data = self._read(self.client, "Hud/Minimap/ingameMap.dds")
        try:
            return Image.open(io.BytesIO(data)).convert("RGBA")
        except Exception:
            png = subprocess.run(["magick", "dds:-", "png:-"], input=data, capture_output=True, check=True).stdout
            return Image.open(io.BytesIO(png)).convert("RGBA")


def level_info(zips):
    mapsize = 1024
    teamnames = ["Neutral", "Mec", "Us"]
    for con in zips.con("Init.con"):
        if con.get("gameLogic.setTeamName") and int(con["gameLogic.setTeamName"][0]) > 0:
            teamnames[int(con["gameLogic.setTeamName"][0])] = con["gameLogic.setTeamName"][1].strip('"')
    for con in zips.con("Heightdata.con"):
        if con.get("heightmapcluster.setHeightmapSize"):
            mapsize = int(con["heightmapcluster.setHeightmapSize"][0])
    return mapsize, teamnames


def gpo_info(cons):
    cps = {}
    combat_areas = {}
    active_ca = ""
    active_cp_template = ""
    active_cp = ""
    for con in cons:
        if active_cp_template and con.get("ObjectTemplate.team"):
            cps[active_cp_template].team = int(con["ObjectTemplate.team"][0])
            continue
        if active_cp_template and con.get("ObjectTemplate.unableToChangeTeam"):
            cps[active_cp_template].utct = int(con["ObjectTemplate.unableToChangeTeam"][0])
            continue
        if active_cp_template and con.get("ObjectTemplate.showOnMinimap"):
            cps[active_cp_template].showOnMinimap = int(con["ObjectTemplate.showOnMinimap"][0])
            continue
        if con.get("ObjectTemplate.create"):
            if con["ObjectTemplate.create"][0] == "ControlPoint":
                active_cp_template = con["ObjectTemplate.create"][1]
                cps[active_cp_template] = ControlPoint()
            continue
        if con.get("Object.create") and con["Object.create"][0] in cps:
            active_cp = con["Object.create"][0]
            continue
        if active_cp and con.get("Object.absolutePosition"):
            cps[active_cp].position = tuple(float(a) for a in con["Object.absolutePosition"][0].split("/"))
            continue
        if con.get("CombatArea.create"):
            active_ca = con["CombatArea.create"][0]
            combat_areas[active_ca] = {"points": [], "team": 0, "vehicles": 0}
        if active_ca and con.get("CombatArea.addAreaPoint"):
            combat_areas[active_ca]["points"].append(tuple(float(a) for a in con["CombatArea.addAreaPoint"][0].split("/")))
        if active_ca and con.get("CombatArea.team"):
            combat_areas[active_ca]["team"] = int(con["CombatArea.team"][0])
        if active_ca and con.get("CombatArea.vehicles"):
            combat_areas[active_ca]["vehicles"] = int(con["CombatArea.vehicles"][0])

    # Only the first vBF2-style area (team 0, vehicles 4) is drawn, like the original tool.
    primary = [ca["points"] for ca in combat_areas.values() if ca["points"] and ca["team"] == 0 and ca["vehicles"] == 4]
    secondary = [ca["points"] for ca in combat_areas.values() if ca["points"] and ca["team"] == 0 and ca["vehicles"] != 4]
    points = primary[0] if primary else (secondary[0] if secondary else [])
    return list(cps.values()), points


def to_image(coord, imgsize, mapsize):
    z = coord[1] if len(coord) == 2 else coord[2]
    return (int(coord[0] / mapsize * imgsize + imgsize / 2), int(-z / mapsize * imgsize + imgsize / 2))


def to_cropped(coord, imgsize, mapsize, box):
    x, y = to_image(coord, imgsize, mapsize)
    return (int((x - box[0]) / (box[2] - box[0]) * OUTPUT_SIZE[0]),
            int((y - box[1]) / (box[3] - box[1]) * OUTPUT_SIZE[1]))


def bounding_box(points, imgsize, mapsize):
    if not points:
        return 0, 0, imgsize, imgsize
    xs, ys = zip(*(to_image(p, imgsize, mapsize) for p in points))
    left, right, top, bottom = min(xs), max(xs), min(ys), max(ys)
    size = int(max(bottom - top, right - left) * BBOX_EXPAND_SCALE)
    if size > imgsize:
        return 0, 0, imgsize, imgsize
    center = [(right + left) / 2, (top + bottom) / 2]
    if size > center[0] * 2: center[0] = size / 2
    if size > (imgsize - center[0]) * 2: center[0] = imgsize - size / 2
    if size > center[1] * 2: center[1] = size / 2
    if size > (imgsize - center[1]) * 2: center[1] = imgsize - size / 2
    return center[0] - size / 2, center[1] - size / 2, center[0] + size / 2, center[1] + size / 2


def draw_rect(dc, box, fill, width):
    dc.line([(box[0], box[1]), (box[2], box[1]), (box[2], box[3]), (box[0], box[3]), (box[0], box[1])], fill, width)


def projection(canvas):
    alpha = ImageChops.offset(canvas.split()[3], *PROJECTION_VECTOR).filter(ImageFilter.GaussianBlur(PROJECTION_BLUR))
    bands = list(Image.new("RGBA", canvas.size, PROJECTION_SHADOWCOLOR).split())
    bands[3] = ImageChops.multiply(alpha, bands[3])
    return Image.merge("RGBA", bands)


def flag_image(teamname):
    folder = find_ci(FLAGS_DIR, teamname)
    path = folder and find_ci(folder, "miniMap_CP.tga")
    if not path:
        sys.exit("No flag for team '%s': add %s/<Team>/miniMap_CP.tga (72x72, see the original tool's .psd)" % (teamname, FLAGS_DIR))
    return Image.open(path).convert("RGBA")


def render(baseimg, mapsize, teamnames, cons, style):
    cps, ca_points = gpo_info(cons)
    base_size = baseimg.size[0]
    flags = [flag_image(name) for name in teamnames]
    utct = Image.open(UTCT_IMAGE).convert("RGBA")

    box = bounding_box(ca_points, base_size, mapsize)
    img = baseimg.crop(box).resize(OUTPUT_SIZE, resample=BILINEAR)
    img = Image.alpha_composite(img, Image.open(COVER_IMAGE).convert("RGBA").resize(img.size, resample=BILINEAR))

    ca_img = Image.open(COMBATAREA_IMAGES[style]).convert("RGBA").resize(OUTPUT_SIZE, resample=BILINEAR)
    poly = [to_cropped(p, base_size, mapsize, box) for p in ca_points]
    if len(poly) > 2:
        ImageDraw.Draw(ca_img).polygon(poly, fill=(0, 0, 0, 0))
        ca_img = ca_img.filter(ImageFilter.SMOOTH)
    img = Image.alpha_composite(img, ca_img)

    cp_canvas = Image.new("RGBA", img.size, (255, 255, 255, 0))
    for cp in cps:
        if cp.showOnMinimap == 0:
            continue
        icon = flags[cp.team]
        if cp.utct:
            icon = Image.alpha_composite(utct, icon)
        x, y = to_cropped(cp.position, base_size, mapsize, box)
        layer = Image.new("RGBA", img.size, (255, 255, 255, 0))
        layer.paste(icon, (int(x - icon.size[0] / 2), int(y - icon.size[1] / 2)))
        cp_canvas = Image.alpha_composite(cp_canvas, layer)
    img = Image.alpha_composite(img, projection(cp_canvas))
    img = Image.alpha_composite(img, cp_canvas)

    minimap = baseimg.resize(MINIMAP_SIZE, resample=BILINEAR)
    img = Image.alpha_composite(img, Image.open(MINIMAPBORDER_IMAGE).convert("RGBA").resize(img.size, resample=BILINEAR))
    draw_rect(ImageDraw.Draw(minimap), [int(c / base_size * MINIMAP_SIZE[0]) for c in box], MINIMAP_BOUNDINGBOX_COLOR, MINIMAP_LINEWIDTH)
    mm_canvas = Image.new("RGBA", img.size, (255, 255, 255, 0))
    mm_canvas.paste(minimap, MINIMAP_POSITION)
    return Image.alpha_composite(img, mm_canvas).convert("RGB")


def find_level(map_name, mod):
    mods = [mod] if mod else sorted(os.listdir(GAME_MODS_DIR))
    hits = []
    for m in mods:
        levels = find_ci(os.path.join(GAME_MODS_DIR, m), "Levels")
        level = levels and find_ci(levels, map_name)
        if level and os.path.isfile(os.path.join(level, "server.zip")):
            hits.append(level)
    if not hits:
        sys.exit("Level '%s' not found in %s" % (map_name, ", ".join("mods/" + m for m in mods)))
    if len(hits) > 1:
        sys.exit("Level '%s' is installed in several mods, pick one with --mod: %s" % (map_name, ", ".join(hits)))
    return hits[0]


def select_layers(available, requested):
    chosen = []
    for req in requested:
        if req == "all":
            match = available
        elif "/" in req:
            gm, size = req.split("/", 1)
            match = [l for l in available if l[0].lower() == gm.lower() and l[1] == size]
        elif req.isdigit():
            match = [l for l in available if l[1] == req]
        else:
            match = [l for l in available if l[0].lower() == req.lower()]
        if not match:
            sys.exit("No layer matches '%s'. Layers in server.zip: %s" % (req, " ".join("/".join(l) for l in available)))
        chosen += [l for l in match if l not in chosen]
    return chosen


def output_name(gamemode, size):
    # The game menu looks up every singleplayer layer as sp1_<size>.
    return "%s_%s_menumap.png" % ("sp1" if gamemode.lower().startswith("sp") else gamemode, size)


def install(source, info_dir, name):
    # Reuse an existing file's spelling (vanilla writes _menuMap.png): Wine looks names up without
    # case, so a second spelling would leave the game picking either file.
    target = find_ci(info_dir, name) or os.path.join(info_dir, name)
    backup = target + ".bak"
    if os.path.exists(target) and not os.path.exists(backup):
        shutil.copy2(target, backup)
    shutil.copyfile(source, target)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map")
    parser.add_argument("layers", nargs="+")
    parser.add_argument("--mod", help="mod folder under $BF2_GAME_DIR/mods (default: search all)")
    parser.add_argument("--also", action="append", default=[], help="extra level folder whose Info/ also gets the files (editor source)")
    parser.add_argument("--style", choices=sorted(COMBATAREA_IMAGES), default="desert", help="combat area stripe texture")
    parser.add_argument("--format", choices=["png", "dds"], default="png", help="png: real PNG (default); dds: DXT1 DDS like vanilla")
    parser.add_argument("--preview-dir", help="also save each result as a real PNG here")
    parser.add_argument("--dry-run", action="store_true", help="render previews only, install nothing")
    args = parser.parse_args()

    level_dir = find_level(args.map, args.mod)
    zips = LevelZips(level_dir)
    mapsize, teamnames = level_info(zips)
    layers = select_layers(zips.layers(), args.layers)
    baseimg = zips.ingame_map()
    print("Level: %s (map size %d m, teams %s, minimap %dx%d)" % (level_dir, mapsize, "/".join(teamnames[1:]), *baseimg.size))

    info_dirs = [os.path.join(level_dir, "Info")]
    for extra in args.also:
        info = find_ci(extra, "Info")
        if not info:
            sys.exit("No Info folder in %s" % extra)
        info_dirs.append(info)

    names = [output_name(*l) for l in layers]
    for name in set(names):
        if names.count(name) > 1:
            sys.exit("Layers %s would all be saved as %s; pick one" % (
                " ".join("/".join(l) for l, n in zip(layers, names) if n == name), name))

    with tempfile.TemporaryDirectory() as tmp:
        for (gamemode, size), name in zip(layers, names):
            cons = zips.con("GameModes/%s/%s/GamePlayObjects.con" % (gamemode, size))
            img = render(baseimg, mapsize, teamnames, cons, args.style)
            png = os.path.join(tmp, name)
            img.save(png, format="PNG")
            if args.preview_dir:
                os.makedirs(args.preview_dir, exist_ok=True)
                preview = os.path.join(args.preview_dir, name)
                shutil.copyfile(png, preview)
                print("%s/%s: preview %s" % (gamemode, size, preview))
            if args.dry_run:
                continue
            output = png
            if args.format == "dds":
                output = png + ".dds"
                subprocess.run(["magick", png, "-define", "dds:compression=dxt1", "-define", "dds:mipmaps=0", "dds:" + output], check=True)
            for info in info_dirs:
                print("%s/%s: installed %s" % (gamemode, size, install(output, info, name)))


if __name__ == "__main__":
    main()
