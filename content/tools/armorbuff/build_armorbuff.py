#!/usr/bin/env python3
"""Builds the tank/APC rebalance archives for mods/openbf2.

Reads the mod's own archives (never writes them) and produces:
  armorbuff_objects.zip  all vehicle tweaks of aircraftdamage_objects.zip (tank/APC
                         rebalance, engine mobility kill), AI files, ATGM missiles,
                         HE explosion effects, 5 s reload sounds, emplacement AI as Infantry
  armorbuff_common.zip   Material/materialManagerDefine.con + materialManagerSettings.con
  armorbuff_menu.zip     tank driver HUD (HE crosshair page, shell labels) + label textures
All three are mounted first in ServerArchives.con and ClientArchives.con.

Rerun after changing aircraftdamage_objects.zip: armorbuff_objects.zip shadows all its tweaks.
"""

import argparse
import io
import os
import re
import struct
import zipfile

from PIL import Image, ImageDraw, ImageFont

MOD_DEFAULT = os.path.join(os.environ.get("BF2_GAME_DIR", "."), "mods", "openbf2")
FONT = "/usr/share/fonts/noto/NotoSans-Bold.ttf"

ATGM_MAT = 114
HE_MAT = 115
HE_SCALE = 0.8          # HE explosion effects vs artillery
SELECT_ICON_PATH = r"Ingame\Weapons\Icons\Hud\Selection\{}.tga"

TANKS = [
    # dir, tweak, con, prefix, projectile, ai main, ai HE, HE gui index, HUD file, HUD prefix, gui index
    dict(dir="USTNK_M1A2", tweak="USTNK_M1A2.tweak", con="ustnk_m1a2.con", prefix="USTNK_M1A2",
         ai_main="M1A2MainGun", ai_he="M1A2HEGun", he_gui=91,
         hud="HudElementsM1A2Driver.con", hud_prefix="M1A2", gui=14),
    dict(dir="RUTNK_T90", tweak="RUTNK_T90.tweak", con="RUTNK_T90.con", prefix="RUTNK_T90",
         ai_main="T90MainGun", ai_he="T90HEGun", he_gui=92,
         hud="HudElementsT90Driver.con", hud_prefix="T90", gui=7),
    dict(dir="tnk_type98", tweak="tnk_type98.tweak", con="tnk_type98.con", prefix="TNK_Type98",
         ai_main="TnkType98MainGun", ai_he="TnkType98HEGun", he_gui=93,
         hud="HudElementsType98Driver.con", hud_prefix="Type98", gui=37),
]
APCS = [("USAPC_LAV25", "USAPC_LAV25.tweak"), ("apc_btr90", "apc_btr90.tweak"), ("apc_wz551", "apc_wz551.tweak")]

# (projectile material, target material): damage modifier
DAMAGE_MODS = {
    # APC 25 mm vs tank armor: halved, side moved 60% of the way from front to rear
    (42, 28): 0.3, (42, 29): 0.21, (42, 30): 0.075, (42, 86): 0.5,
    # SRAW/Eryx vs tank and APC/AA armor: halved, tank side moved like the 25 mm
    (52, 28): 0.465, (52, 29): 0.375, (52, 30): 0.2375, (52, 86): 0.5,
    (52, 27): 0.465, (52, 102): 0.465,
    # ATGM (copy of 52): tank side moved, otherwise unchanged
    (ATGM_MAT, 29): 0.75,
    # hand grenade / rifle GL blast vs heavy vehicles: halved
    (69, 72): 0.2,
    # tank HE direct hits on heavy armor
    (HE_MAT, 28): 0.45, (HE_MAT, 29): 0.33, (HE_MAT, 30): 0.24, (HE_MAT, 86): 0.45,
    (HE_MAT, 27): 0.6, (HE_MAT, 102): 0.6,
}

# DefaultSoundComp's per-weapon sound templates, S_<weapon>_<name>
WEAPON_SOUNDS = ("Fire1P", "Fire1P_Outdoor", "Fire3P", "BoltClick", "TriggerClick", "SwitchFireRate",
                 "Reload1P", "Reload3P", "Deploy1P", "Deploy3P", "Zoom")

HEADER = re.compile(r"^ObjectTemplate\.(create|activeSafe)\s+(\S+)\s+(\S+)")


# ---------------------------------------------------------------- text helpers

def read_text(zf, name):
    return zf.read(name).decode("latin-1")


def to_lines(text):
    """Splits CRLF text; remembers whether it ended with a line break."""
    ends = text.endswith("\n")
    lines = text.replace("\r\n", "\n").split("\n")
    if ends:
        lines.pop()
    return lines, ends


def from_lines(lines, ends):
    return "\r\n".join(lines) + ("\r\n" if ends else "")


def find_member(zf, suffix):
    suffix = suffix.lower()
    hits = [n for n in zf.namelist() if n.lower().endswith(suffix)]
    if len(hits) != 1:
        raise SystemExit(f"{zf.filename}: expected one member ending {suffix}, found {hits}")
    return hits[0]


class Tweak:
    """A .tweak/.con file as blocks, one per ObjectTemplate.create/activeSafe header."""

    def __init__(self, text):
        lines, self.ends = to_lines(text)
        self.head = []
        self.blocks = []  # [kind, type, name, lines]
        for line in lines:
            m = HEADER.match(line)
            if m:
                self.blocks.append([m.group(1), m.group(2), m.group(3), [line]])
            elif self.blocks:
                self.blocks[-1][3].append(line)
            else:
                self.head.append(line)

    def text(self):
        out = list(self.head)
        for b in self.blocks:
            out.extend(b[3])
        return from_lines(out, self.ends)

    def block(self, name, type_=None):
        hits = [b for b in self.blocks if b[2].lower() == name.lower() and (type_ is None or b[1] == type_)]
        if len(hits) != 1:
            raise SystemExit(f"expected one block {type_} {name}, found {len(hits)}")
        return hits[0]


def prop_value(block, prop):
    for line in block[3]:
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "ObjectTemplate." + prop:
            return parts[1]
    return None


def set_prop(block, prop, value, after_prefix=None, all_=False):
    """Replaces ObjectTemplate.<prop>; inserts it after the last line starting with
    after_prefix (or at the block's end) when it is missing."""
    key = "ObjectTemplate." + prop
    lines = block[3]
    found = False
    for i, line in enumerate(lines):
        parts = line.split()
        if parts and parts[0] == key:
            lines[i] = f"{key} {value}"
            found = True
            if not all_:
                break
    if found:
        return
    at = None
    if after_prefix:
        for i, line in enumerate(lines):
            if line.startswith("ObjectTemplate." + after_prefix):
                at = i + 1
    if at is None:
        at = len(lines)
        while at > 1 and not lines[at - 1].strip():
            at -= 1
    lines.insert(at, f"{key} {value}")


def append_lines(block, new):
    lines = block[3]
    at = len(lines)
    while at > 1 and not lines[at - 1].strip():
        at -= 1
    lines[at:at] = new


# ---------------------------------------------------------------- vehicles

def con_child_placement(con_text, parent, child):
    """The setPosition/setRotation lines that follow `addTemplate child` in the parent's block."""
    lines = Tweak(con_text).block(parent)[3]
    for i, line in enumerate(lines):
        if line.split()[:2] == ["ObjectTemplate.addTemplate", child]:
            out = []
            for nxt in lines[i + 1:]:
                if nxt.split()[:1] not in (["ObjectTemplate.setPosition"], ["ObjectTemplate.setRotation"]):
                    break
                out.append(nxt.strip())
            return out
    raise SystemExit(f"{child} is not a child of {parent}")


def build_tank(text, con_text, cfg):
    t = Tweak(text)
    p = cfg["prefix"]
    barrel_name = f"{p}_Barrel"
    he_name = f"{p}_BarrelHE"
    barrel = t.block(barrel_name, "GenericFireArm")
    proj_name = prop_value(barrel, "projectileTemplate")
    he_proj = f"{p}_HE_Projectile"

    for name in (f"{p}_Turret",):
        set_prop(t.block(name, "RotationalBundle"), "setMaxSpeed", "20/0/0")
    for b in t.blocks:
        if b[1] == "RotationalBundle" and b[2].lower() in (f"{p}_barrel_base".lower(), f"{p}_hud_spring".lower()):
            set_prop(b, "setMinRotation", "0/-20/0")
            set_prop(b, "setMaxRotation", "0/5/0")
            set_prop(b, "setMaxSpeed", "0/20/0")

    # shared by both shells: 4 s between shots, then the default 2 s reload (with its sound
    # and progress bar) = 6 s per shot
    set_prop(barrel, "fire.roundsPerMinute", "15")
    # OpenBF2 engine extension: both shells share one breech, so switching shells loads the
    # other one (GenericFireArm::takeBreech); the HE copy below inherits it
    set_prop(barrel, "breechGroup", "1", after_prefix="itemIndex")

    # HE: copy of the barrel, its projectile and their sounds under new names
    sounds = {f"S_{barrel_name}_{s}".lower() for s in WEAPON_SOUNDS} | {f"S_{proj_name}_Looping".lower()}
    copied = [b for b in t.blocks
              if b is barrel or b[2] == proj_name or (b[1] == "Sound" and b[2].lower() in sounds)]
    last = max(t.blocks.index(b) for b in copied)
    he_blocks = []
    for b in copied:
        body = "\n".join(b[3]).replace(proj_name, "\x00P").replace(barrel_name, "\x00B")
        body = body.replace("\x00P", he_proj).replace("\x00B", he_name)
        lines = body.split("\n")
        if b is barrel:
            lines[0] = f"ObjectTemplate.create GenericFireArm {he_name}"
        m = HEADER.match(lines[0])
        he_blocks.append([m.group(1), m.group(2), m.group(3), lines])
    t.blocks[last + 1:last + 1] = he_blocks

    he = t.block(he_name, "GenericFireArm")
    set_prop(he, "weaponHud.hudName", '"HE"')
    set_prop(he, "weaponHud.selectIcon", SELECT_ICON_PATH.format("tank_he"), after_prefix="weaponHud.")
    set_prop(he, "weaponHud.guiIndex", str(cfg["he_gui"]))
    # Not 2: the AI files a vehicle weapon under (itemIndex - 1) + (1 for alt fire), and the
    # coax MG (alt fire, no item index) already holds slot 1 (AIObjectArmament).
    set_prop(he, "itemIndex", "3")
    set_prop(he, "velocity", "130")
    set_prop(he, "ammo.nrOfMags", "12")
    set_prop(he, "aiTemplate", cfg["ai_he"])
    hep = t.block(he_proj, "GenericProjectile")
    set_prop(hep, "damage", "600")
    set_prop(hep, "material", str(HE_MAT), all_=True)
    set_prop(hep, "detonation.explosionMaterial", str(HE_MAT))
    set_prop(hep, "detonation.explosionRadius", "12")
    set_prop(hep, "detonation.explosionDamage", "750")
    set_prop(hep, "detonation.explosionForce", "24")
    set_prop(hep, "detonation.endEffectTemplate", "e_exp_he")
    set_prop(hep, "timeToLive", "CRD_NONE/7/0/0")

    set_prop(barrel, "weaponHud.hudName", '"APFSDS"')
    set_prop(barrel, "weaponHud.selectIcon", SELECT_ICON_PATH.format("tank_apfsds"), after_prefix="weaponHud.")
    set_prop(barrel, "itemIndex", "1")
    set_prop(barrel, "velocity", "450")
    set_prop(barrel, "ammo.nrOfMags", "20")
    proj = t.block(proj_name, "GenericProjectile")
    set_prop(proj, "damage", "750")
    set_prop(proj, "detonation.explosionRadius", "4")
    set_prop(proj, "timeToLive", "CRD_NONE/6/0/0")

    base = t.block(f"{p}_Barrel_Base", "RotationalBundle")
    placement = con_child_placement(con_text, f"{p}_Barrel_Base", barrel_name)
    append_lines(base, [f"ObjectTemplate.addTemplate {he_name}"] + placement)
    return t.text()


def build_apc(text, objc):
    """Returns the tweak and the retimed cannon sounds {zip path: bytes}."""
    t = Tweak(text)
    for b in t.blocks:
        if b[1] == "RotationalBundle":
            v = prop_value(b, "setMaxSpeed")
            if v == "45/0/0":
                set_prop(b, "setMaxSpeed", "30/0/0")
            elif v == "0/45/0":
                set_prop(b, "setMaxSpeed", "0/30/0")
    guns = [b for b in t.blocks if b[1] == "GenericFireArm" and b[2].endswith("_Barrel")]
    atgms = [b for b in t.blocks if b[1] == "GenericFireArm" and prop_value(b, "projectileTemplate") == "tow_missile"]
    if len(guns) != 1 or len(atgms) != 1:
        raise SystemExit("APC: expected one cannon and one ATGM launcher")
    gun = guns[0]
    set_prop(gun, "fire.roundsPerMinute", "420")
    set_prop(gun, "ammo.nrOfMags", "2")
    set_prop(gun, "velocity", "400")
    set_prop(gun, "heatAddWhenFire", "0.033")
    set_prop(gun, "coolDownPerSec", "0.2")
    proj = t.block(prop_value(gun, "projectileTemplate"), "GenericProjectile")
    set_prop(proj, "damage", "40")
    set_prop(proj, "detonation.explosionRadius", "3.5")
    set_prop(proj, "detonation.explosionDamage", "40")
    set_prop(atgms[0], "ammo.minimumTimeUntilReload", "10")

    wavs = {}
    for view in ("Fire1P", "Fire3P"):
        sound = t.block(f"S_{gun[2]}_{view}", "Sound")
        old = prop_value(sound, "soundFilename").strip('"')
        member = find_member(objc, old[len("objects/"):])
        new = member[:-4] + "_420.wav"
        wavs[new] = retime_cannon_loop(objc.read(member), 420)
        set_prop(sound, "soundFilename", f'"objects/{new.lower()}"')
    return t.text(), wavs


def build_missile(text):
    t = Tweak(text)
    projs = [b for b in t.blocks if b[1] == "GenericProjectile"]
    if len(projs) != 1:
        raise SystemExit("missile: expected one projectile")
    set_prop(projs[0], "damage", "750")
    set_prop(projs[0], "material", str(ATGM_MAT), all_=True)
    return t.text()


def build_weapons_ai(text, cfg):
    lines, ends = to_lines(text)
    out, in_main = [], False
    for line in lines:
        parts = line.split()
        if parts[:1] == ["weaponTemplate.create"]:
            in_main = parts[1] == cfg["ai_main"]
        if in_main and parts[:1] == ["weaponTemplate.setStrength"]:
            strength = {"Infantery": "1.0", "Infantry": "1.0", "LightArmour": "3.0", "HeavyArmour": "9.0",
                        "Helicopter": "2.0"}.get(parts[1])
            target = "Infantry" if parts[1] == "Infantery" else parts[1]
            line = f"weaponTemplate.setStrength {target:<12}{strength or parts[2]}"
        if in_main and parts[:1] == ["weaponTemplate.setExplosionRadius"]:
            line = "weaponTemplate.setExplosionRadius 4.0"
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    out += ["", "rem *** HE shell ***",
            f"weaponTemplate.create {cfg['ai_he']}",
            "weaponTemplate.indirect 0",
            "weaponTemplate.minRange 0.0",
            "weaponTemplate.maxRange 150.0",
            "weaponTemplate.setStrength Infantry    9.0",
            "weaponTemplate.setStrength LightArmour 9.0",
            "weaponTemplate.setStrength HeavyArmour 1.0",
            "weaponTemplate.setStrength NavalArmour 0.0",
            "weaponTemplate.setStrength Helicopter  1.0",
            "weaponTemplate.setStrength AirPlane    0.0",
            "weaponTemplate.allowedDeviation 3.0",
            "weaponTemplate.setExplosionRadius 12.0",
            "weaponTemplate.setFiresThroughTransparent 0"]
    return from_lines(out, True)


def build_objects_ai(text):
    """Driver camera limits follow the new -20/+5 elevation (first plugin only)."""
    lines, ends = to_lines(text)
    done = set()
    for i, line in enumerate(lines):
        parts = line.split()
        if parts[:1] == ["aiTemplatePlugIn.setCameraRelativeMinRotationDeg"] and "min" not in done:
            lines[i] = "aiTemplatePlugIn.setCameraRelativeMinRotationDeg -360/-20/0"
            done.add("min")
        if parts[:1] == ["aiTemplatePlugIn.setCameraRelativeMaxRotationDeg"] and "max" not in done:
            lines[i] = "aiTemplatePlugIn.setCameraRelativeMaxRotationDeg 360/5/0"
            done.add("max")
    return from_lines(lines, ends)


# ---------------------------------------------------------------- effects

def scale_vec(value, k):
    return "/".join(f"{float(x) * k:g}" for x in value.split("/"))


def scale_crd(value, k):
    parts = value.split("/")
    return "/".join([parts[0]] + [f"{float(x) * k:g}" for x in parts[1:3]] + parts[3:])


SCALE_SCALAR = ("particleMaxSize", "randomSize", "emitSpeed", "randomSpeed", "gravity",
                "attenuationRange1", "attenuationRange2")


def scale_effect(tweak_text, con_text, rename, k):
    """Copies an effect bundle under new names with its spatial values scaled by k."""
    t = Tweak(tweak_text)
    names = {b[2] for b in t.blocks}

    def ren(s):
        for old in sorted(names, key=len, reverse=True):
            s = re.sub(rf"(?<![\w]){re.escape(old)}(?![\w])", rename(old), s, flags=re.I)
        return s

    for b in t.blocks:
        new = []
        for i, line in enumerate(b[3]):
            parts = line.split()
            if len(parts) >= 2 and parts[0].startswith("ObjectTemplate."):
                prop = parts[0][len("ObjectTemplate."):]
                if b[1] != "Sound" and b[1] != "EffectBundle" or prop == "setPosition":
                    if prop in SCALE_SCALAR:
                        line = f"ObjectTemplate.{prop} {float(parts[1]) * k:g}"
                    elif prop in ("emitRadius", "setPosition"):
                        line = f"ObjectTemplate.{prop} {scale_vec(parts[1], k)}"
                    elif prop in ("emitRangeX", "emitRangeY", "emitRangeZ"):
                        line = f"ObjectTemplate.{prop} {scale_crd(parts[1], k)}"
            new.append(ren(line))
        b[3] = new
    t.head = [ren(x) for x in t.head]
    con_lines, ends = to_lines(con_text)
    con_lines = [ren(x) for x in con_lines if not x.startswith("GeometryTemplate.")]
    return t.text(), from_lines(con_lines, ends)


# ---------------------------------------------------------------- materials

def build_material_define(text):
    lines, ends = to_lines(text)
    blocks, cur = {}, None
    for line in lines:
        if line.startswith("Material.active "):
            cur = int(line.split()[1])
            blocks[cur] = []
        if cur is not None:
            if line.strip() == "" and blocks[cur] and blocks[cur][-1].strip() == "":
                continue
            blocks[cur].append(line)
    out = list(lines)
    while out and not out[-1].strip():
        out.pop()
    for src, dst, name in ((52, ATGM_MAT, "Missile_ATGM"), (44, HE_MAT, "Shell_tank_HE")):
        if dst in blocks:
            raise SystemExit(f"material {dst} already defined")
        body = [x for x in blocks[src] if x.strip()]
        body[0] = f"Material.active {dst}"
        body = [f'Material.name "{name}"' if x.startswith("Material.name ") else x for x in body]
        out += [""] + body
    out.append("")
    return from_lines(out, ends)


def build_material_settings(text, he_effects):
    lines, ends = to_lines(text)
    head, cells = [], []  # cells: [a, b, [lines]]
    for line in lines:
        parts = line.split()
        if parts[:1] == ["MaterialManager.createCell"]:
            cells.append([int(parts[1]), int(parts[2]), [line]])
        elif cells:
            cells[-1][2].append(line)
        else:
            head.append(line)
    index = {(c[0], c[1]): c for c in cells}
    if any(a in (ATGM_MAT, HE_MAT) or b in (ATGM_MAT, HE_MAT) for a, b in index):
        raise SystemExit("material cells for 114/115 already exist")

    def clone(a, b, body):
        new = [f"MaterialManager.createCell {a} {b}"]
        for x in body[1:]:
            parts = x.split()
            if a == HE_MAT and parts[:1] == ["MaterialManager.setEffectTemplate"] and parts[2] in he_effects:
                x = f"MaterialManager.setEffectTemplate {parts[1]} {he_effects[parts[2]]}"
            new.append(x)
        return [a, b, new]

    remap = {52: ATGM_MAT, 44: HE_MAT}
    clones = []
    for a, b, body in cells:
        if a in remap:
            clones.append(clone(remap[a], b, body))
        if b in remap:
            clones.append(clone(a, remap[b], body))
        if a in remap and b in remap:
            clones.append(clone(remap[a], remap[b], body))
    for c in clones:
        index[(c[0], c[1])] = c
    cells += clones

    for (a, b), mod in DAMAGE_MODS.items():
        cell = index.get((a, b))
        if cell is None:
            cell = [a, b, [f"MaterialManager.createCell {a} {b}"]]
            cells.append(cell)
            index[(a, b)] = cell
        body = cell[2]
        for i, x in enumerate(body):
            if x.startswith("MaterialManager.damageMod"):
                body[i] = f"MaterialManager.damageMod {mod:g}"
                break
        else:
            body.insert(1, f"MaterialManager.damageMod {mod:g}")

    out = list(head)
    for c in cells:
        out.extend(c[2])
    return from_lines(out, ends)


# ---------------------------------------------------------------- HUD

def label_texture(text):
    w, h, s = 128, 32, 4
    big = Image.new("L", (w * s, h * s), 0)
    d = ImageDraw.Draw(big)
    font = ImageFont.truetype(FONT, 26 * s)
    d.text((2 * s, h * s // 2), text, font=font, fill=255, anchor="lm")
    alpha = big.resize((w, h), Image.LANCZOS)
    img = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    img.putalpha(alpha)
    return img


def select_icon(kind):
    """140x40 weapon-select icon in the vanilla Selection/ style: shaded side view, nose left."""
    s = 4
    w, h = 140 * s, 40 * s
    cy = 20 * s
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def body(x0, x1, half, color):
        d.rectangle([x0 * s, cy - half * s, x1 * s, cy + half * s], fill=color)

    if kind == "apfsds":
        rod, fin, sabot = (70, 74, 78, 255), (58, 60, 62, 255), (150, 140, 96, 255)
        d.polygon([(6 * s, cy), (16 * s, cy - 2 * s), (16 * s, cy + 2 * s)], fill=(120, 124, 128, 255))
        body(16, 122, 2, rod)
        for x0 in (36, 47, 58):                       # sabot petals
            d.polygon([(x0 * s, cy - 6 * s), (x0 * s + 9 * s, cy - 9 * s), (x0 * s + 9 * s, cy + 9 * s),
                       (x0 * s, cy + 6 * s)], fill=sabot)
        d.polygon([(104 * s, cy - 2 * s), (124 * s, cy - 9 * s), (132 * s, cy - 9 * s), (128 * s, cy - 2 * s)], fill=fin)
        d.polygon([(104 * s, cy + 2 * s), (124 * s, cy + 9 * s), (132 * s, cy + 9 * s), (128 * s, cy + 2 * s)], fill=fin)
    else:
        olive, band, brass = (84, 96, 52, 255), (214, 186, 40, 255), (176, 140, 64, 255)
        d.polygon([(6 * s, cy), (12 * s, cy - 2 * s), (12 * s, cy + 2 * s)], fill=(150, 150, 150, 255))   # fuze
        d.polygon([(12 * s, cy - 2 * s), (34 * s, cy - 8 * s), (34 * s, cy + 8 * s), (12 * s, cy + 2 * s)], fill=olive)
        body(34, 82, 8, olive)
        body(44, 50, 8, band)
        body(80, 84, 9, (120, 100, 50, 255))          # driving band
        body(84, 132, 9, brass)
        body(130, 134, 10, brass)

    # cylindrical shading: bright just above the axis, darker toward the edges
    px = img.load()
    for y in range(h):
        f = max(0.35, 1.25 - 0.85 * abs(y - (cy - 2 * s)) / (10 * s))
        for x in range(w):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f)), a)
    return img.resize((140, 40), Image.LANCZOS)


def texture_files(path, img):
    """The .tga the HUD scripts name, plus a .dds twin: outside the menu atlas the rebuilt
    texture manager only loads .dds (it drops the extension and appends .dds)."""
    out = {}
    for ext, fmt in (("tga", "TGA"), ("dds", "DDS")):
        buf = io.BytesIO()
        img.save(buf, format=fmt)
        out[f"{path}.{ext}"] = buf.getvalue()
    return out


def build_tank_hud(text, cfg):
    lines, ends = to_lines(text)
    nodes = set()
    for line in lines:
        parts = line.split()
        if parts and parts[0].startswith("hudBuilder.create") and len(parts) >= 3:
            nodes.add(parts[2])
    copy = []
    for line in lines:
        for n in sorted(nodes, key=len, reverse=True):
            line = re.sub(rf"(?<![\w]){re.escape(n)}(?![\w])", n + "HE", line)
        line = re.sub(rf"EQUAL GuiIndex {cfg['gui']}\b", f"EQUAL GuiIndex {cfg['he_gui']}", line)
        copy.append(line)
    p = cfg["hud_prefix"]
    out = list(lines)
    while out and not out[-1].strip():
        out.pop()
    out += ["", "rem ------------HE shell page (copy of the page above)-------------", ""] + copy
    while out and not out[-1].strip():
        out.pop()
    for suffix, gui, tex in (("APFSDS", cfg["gui"], "shell_apfsds.tga"), ("HE", cfg["he_gui"], "shell_he.tga")):
        out += ["", f"rem ------------shell label {suffix}-------------",
                f"hudBuilder.createSplitNode\t\tBottomRightAnimateHud {p}ShellLabel{suffix}",
                f"hudBuilder.setNodeLogicShowVariable \tEQUAL GuiIndex {gui}",
                f"hudBuilder.createPictureNode\t\t{p}ShellLabel{suffix} {p}ShellLabel{suffix}Icon 325 61 40 10",
                f"hudBuilder.setPictureNodeTexture \tIngame/Weapons/Icons/Hud/{tex}",
                "hudBuilder.setNodeColor\t\t\t1 1 1 1"]
    return from_lines(out, True)


# ---------------------------------------------------------------- sound

def retime_cannon_loop(data, rounds_per_minute):
    """Full-auto fire loops the cue region in the wav's adtl/ltxt chunk (one shot period at the
    original fire rate); shortening that region to 60/rpm makes the loop match the new rate."""
    pos = 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            rate = struct.unpack_from("<I", data, pos + 12)[0]
        if cid == b"LIST" and data[pos + 8:pos + 12] == b"adtl":
            sub = pos + 12
            while sub < pos + 8 + size:
                sid = data[sub:sub + 4]
                ssize = struct.unpack_from("<I", data, sub + 4)[0]
                if sid == b"ltxt":
                    length = round(rate * 60 / rounds_per_minute)
                    out = bytearray(data)
                    struct.pack_into("<I", out, sub + 12, length)
                    return bytes(out)
                sub += 8 + ssize + (ssize & 1)
        pos += 8 + size + (size & 1)
    raise SystemExit("no ltxt loop region in cannon sound")


# ---------------------------------------------------------------- mobility kill

LAND_ENGINES = ("c_ETCar", "c_ETNewCar", "c_ETNewCar2", "c_ETTank", "c_ETNewTank", "c_ETShip")
AIR_ENGINES = ("c_ETPlane", "c_ETHelicopter")
AIR_POWER_LOSS = "5"    # seconds for jets and rotors to lose all power
TAIL_ROTOR_SPIN = "180" # deg/s once the tail rotor is dead


def build_mobility(text, name):
    """Engine power loss while the vehicle burns (OpenBF2 engine: criticalDamagePowerLoss/Spin)."""
    t = Tweak(text)
    for b in t.blocks:
        if b[1] != "Engine":
            continue
        et = prop_value(b, "setEngineType")
        if et in LAND_ENGINES:
            set_prop(b, "criticalDamagePowerLoss", "0", after_prefix="setEngineType")
        elif et in AIR_ENGINES:
            set_prop(b, "criticalDamagePowerLoss", AIR_POWER_LOSS, after_prefix="setEngineType")
            yaw = "PIYaw" in (prop_value(b, "setInputToRoll"), prop_value(b, "setInputToYaw"))
            if et == "c_ETHelicopter" and prop_value(b, "pureRotational") == "1" and yaw:
                set_prop(b, "criticalDamageSpin", TAIL_ROTOR_SPIN, after_prefix="criticalDamagePowerLoss")
        else:
            print(f"  {name}: engine {b[2]} type {et} left alone")
            continue
        spin = prop_value(b, "criticalDamageSpin")
        print(f"  {os.path.basename(name)}: {b[2]} {et} loss {prop_value(b, 'criticalDamagePowerLoss')}"
              + (f" spin {spin}" if spin else ""))
    return t.text()


# ---------------------------------------------------------------- main

def write_zip(path, files):
    tmp = path + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(files):
            data = files[name]
            z.writestr(name, data.encode("latin-1") if isinstance(data, str) else data)
    os.replace(tmp, path)
    print(f"{os.path.basename(path)}: {len(files)} files")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mod", default=MOD_DEFAULT)
    ap.add_argument("--out", help="output folder (default: the mod folder)")
    args = ap.parse_args()
    mod = args.mod
    out = args.out or mod

    ad = zipfile.ZipFile(os.path.join(mod, "aircraftdamage_objects.zip"))
    objs = zipfile.ZipFile(os.path.join(mod, "Objects_server.zip"))
    objc = zipfile.ZipFile(os.path.join(mod, "Objects_client.zip"))
    common = zipfile.ZipFile(os.path.join(mod, "Common_server.zip"))
    menu = zipfile.ZipFile(os.path.join(mod, "Menu_server.zip"))

    objects, menu_files = {}, {}

    for cfg in TANKS:
        base = f"Vehicles/Land/{cfg['dir']}/"
        tweak_name = find_member(ad, base + cfg["tweak"])
        con_text = read_text(objs, find_member(objs, base + cfg["con"]))
        tweak_text = read_text(ad, tweak_name)
        objects[tweak_name] = build_tank(tweak_text, con_text, cfg)
        wai = find_member(objs, base + "ai/Weapons.ai")
        objects[wai] = build_weapons_ai(read_text(objs, wai), cfg)
        oai = find_member(objs, base + "ai/Objects.ai")
        objects[oai] = build_objects_ai(read_text(objs, oai))
        hud = find_member(menu, "HUD/HudSetup/Vehicles/" + cfg["hud"])
        menu_files[hud] = build_tank_hud(read_text(menu, hud), cfg)

    for d, tweak in APCS:
        name = find_member(ad, f"Vehicles/Land/{d}/{tweak}")
        text, wavs = build_apc(read_text(ad, name), objc)
        objects[name] = text
        objects.update(wavs)

    for name in ad.namelist():
        if name.lower().endswith(".tweak"):
            objects[name] = build_mobility(objects.get(name) or read_text(ad, name), name)

    for missile in ("tow_missile", "tow_missile_stationary"):
        name = find_member(objs, f"Weapons/Armament/missiles/{missile}/{missile}.tweak")
        objects[name] = build_missile(read_text(objs, name))

    # commander artillery: 25 m spread, 5 min between strikes (enforced per team by the server)
    for gun in ("ars_d30/ars_d30.tweak", "USART_LW155/USART_LW155.tweak"):
        name = find_member(objs, "Weapons/stationary/" + gun)
        t = Tweak(read_text(objs, name))
        set_prop(next(b for b in t.blocks if b[1] == "GenericFireArm"), "deviation.radius", "25")
        objects[name] = t.text()
    for side in ("US", "MEC"):
        name = find_member(objs, f"Weapons/common/RemoteControlledObjects/ArtilleryControlObject{side}.con")
        t = Tweak(read_text(objs, name))
        set_prop(t.blocks[0], "minTimeBetweenActivation", "300")
        objects[name] = t.text()

    # emplacements (TOW/HJ-8, MG bipods, AA, WASP): retail rates them LightArmour, so bots fired
    # rockets and ATGMs at a manned one; as Infantry they fight its exposed gunner with small arms
    for name in objs.namelist():
        if not re.match(r"(?i)weapons/stationary/[^/]+/ai/objects\.ai$", name):
            continue
        lines, ends = to_lines(read_text(objs, name))
        if not any(l.split()[:2] == ["aiTemplatePlugIn.setHasExposedSoldier", "1"] for l in lines):
            continue
        hits = [i for i, l in enumerate(lines) if l.split() == ["aiTemplatePlugIn.setStrType", "LightArmour"]]
        for i in hits:
            lines[i] = "aiTemplatePlugIn.setStrType Infantry"
        if hits:
            objects[name] = from_lines(lines, ends)

    settings = read_text(common, "Material/materialManagerSettings.con")
    xl = sorted({m for m in re.findall(r"setEffectTemplate \d+ (\S+)", settings) if m.lower().startswith("e_mexp_xl_")})
    he_effects = {}
    for eff in xl + ["e_exp_Xlarge"]:
        new = re.sub("(?i)^e_mexp_xl_", "e_mexp_he_", eff) if eff != "e_exp_Xlarge" else "e_exp_he"
        tw = find_member(objs, f"/{eff}.tweak")
        cn = find_member(objs, f"/{eff}.con")

        def rename(old, eff=eff):
            return re.sub("(?i)mexp_xl_", "mexp_he_", re.sub("(?i)exp_xlarge", "exp_he", old))

        tweak_out, con_out = scale_effect(read_text(objs, tw), read_text(objs, cn), rename, HE_SCALE)
        folder = "Effects/impacts/explosions/he/"
        objects[folder + new + ".tweak"] = tweak_out
        objects[folder + new + ".con"] = con_out.replace(f"{eff}.tweak", f"{new}.tweak")
        he_effects[eff] = new

    common_files = {
        "Material/materialManagerDefine.con": build_material_define(read_text(common, "Material/materialManagerDefine.con")),
        "Material/materialManagerSettings.con": build_material_settings(settings, he_effects),
    }

    icons = "HUD/Texture/Ingame/Weapons/Icons/Hud/"
    menu_files.update(texture_files(icons + "shell_apfsds", label_texture("APFSDS")))
    menu_files.update(texture_files(icons + "shell_he", label_texture("HE")))
    menu_files.update(texture_files(icons + "Selection/tank_apfsds", select_icon("apfsds")))
    menu_files.update(texture_files(icons + "Selection/tank_he", select_icon("he")))

    write_zip(os.path.join(out, "armorbuff_objects.zip"), objects)
    write_zip(os.path.join(out, "armorbuff_common.zip"), common_files)
    write_zip(os.path.join(out, "armorbuff_menu.zip"), menu_files)


if __name__ == "__main__":
    main()
