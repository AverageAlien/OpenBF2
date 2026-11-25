#!/usr/bin/env python3
"""Installs this fork's game content into a retail Battlefield 2 1.50 folder as the mod mods/openbf2.

mods/openbf2 starts as a copy of mods/bf2 (made once; mods/bf2 itself is never changed), then gets:
  - the loose files of content/mods/openbf2 (Init.con, archive lists, AI settings, Python game modes),
  - one archive per folder in ARCHIVE_DIRS (vehicle/weapon changes, CCIP HUD), written whole,
  - the files of content/mods/openbf2/Menu_client added to Menu_client.zip,
  - for each level in content/mods/openbf2/Levels: its server/ files added to that level's server.zip
    (replacing entries of the same name) and its Info/ files copied into its Info folder.
Running it again brings mods/openbf2 up to date with the content folder.

Usage: install_content.py "<Battlefield 2 folder>" [--fresh] [--shader-fix]
"""
import argparse
import os
import shutil
import sys
import zipfile

CONTENT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MOD_SRC = os.path.join(CONTENT, "mods", "openbf2")
SHADER_FIX = os.path.join(CONTENT, "downloads", "Shaders_Client_Fix.zip")

# Folders written as whole archives (mounted by ClientArchives.con / ServerArchives.con)
ARCHIVE_DIRS = ["armorbuff_objects", "armorbuff_common", "armorbuff_menu", "aircraftdamage_objects", "ccip_menu_server"]
# Folders whose files are added to an existing archive of the mod
PATCH_DIRS = {"Menu_client": "Menu_client.zip"}


def files_under(root):
    """Yields (archive name with forward slashes, path) for every file below root."""
    for base, _, names in os.walk(root):
        for name in sorted(names):
            path = os.path.join(base, name)
            yield os.path.relpath(path, root).replace(os.sep, "/"), path


def find_ci(directory, name):
    """Returns the entry of directory whose name matches name ignoring case, or None."""
    if not os.path.isdir(directory):
        return None
    for entry in os.listdir(directory):
        if entry.lower() == name.lower():
            return os.path.join(directory, entry)
    return None


def write_archive(zip_path, src_dir):
    tmp = zip_path + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as out:
        for arcname, path in files_under(src_dir):
            out.write(path, arcname)
    os.replace(tmp, zip_path)


def patch_archive(zip_path, new_files):
    """Rewrites zip_path with new_files ({archive name: bytes}) added; an existing entry of the same
    name (ignoring case) is replaced and keeps its original spelling."""
    wanted = {name.lower(): (name, data) for name, data in new_files.items()}
    tmp = zip_path + ".tmp"
    with zipfile.ZipFile(zip_path) as src, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as out:
        for info in src.infolist():
            key = info.filename.lower()
            if key in wanted:
                out.writestr(info.filename, wanted.pop(key)[1], zipfile.ZIP_DEFLATED)
            else:
                out.writestr(info, src.read(info))
        for name, data in wanted.values():
            out.writestr(name, data, zipfile.ZIP_DEFLATED)
    os.replace(tmp, zip_path)


def read_dir(src_dir):
    return {name: open(path, "rb").read() for name, path in files_under(src_dir)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game_dir", help="Battlefield 2 folder (holds BF2.exe and mods/bf2)")
    parser.add_argument("--fresh", action="store_true", help="delete mods/openbf2 first and copy it again from mods/bf2")
    parser.add_argument("--shader-fix", action="store_true",
                        help="put the terrain shader fix from content/downloads into mods/openbf2/Shaders_client.zip "
                             "(black blinking terrain patches on DXVK/Wine)")
    args = parser.parse_args()

    mods = find_ci(args.game_dir, "mods")
    vanilla = mods and find_ci(mods, "bf2")
    if not vanilla or not find_ci(vanilla, "Levels"):
        sys.exit("No mods/bf2 with Levels in %s - is this a Battlefield 2 folder?" % args.game_dir)
    target = os.path.join(mods, "openbf2")

    if args.fresh and os.path.isdir(target):
        print("Deleting %s" % target)
        shutil.rmtree(target)
    if not os.path.isdir(target):
        print("Copying %s to %s (a few GB) ..." % (vanilla, target))
        shutil.copytree(vanilla, target)

    special = set(ARCHIVE_DIRS) | set(PATCH_DIRS) | {"Levels"}
    for arcname, path in files_under(MOD_SRC):
        if arcname.split("/")[0] in special:
            continue
        dest = os.path.join(target, *arcname.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(path, dest)
        print("file     %s" % arcname)

    for name in ARCHIVE_DIRS:
        write_archive(os.path.join(target, name + ".zip"), os.path.join(MOD_SRC, name))
        print("archive  %s.zip" % name)

    for name, archive in PATCH_DIRS.items():
        zip_path = find_ci(target, archive)
        if not zip_path:
            sys.exit("%s is missing from %s" % (archive, target))
        patch_archive(zip_path, read_dir(os.path.join(MOD_SRC, name)))
        print("patched  %s" % os.path.basename(zip_path))

    levels_src = os.path.join(MOD_SRC, "Levels")
    levels_dst = find_ci(target, "Levels")
    for level in sorted(os.listdir(levels_src)):
        level_dir = find_ci(levels_dst, level)
        if not level_dir:
            sys.exit("Level %s is missing from %s" % (level, levels_dst))
        server = os.path.join(levels_src, level, "server")
        if os.path.isdir(server):
            patch_archive(find_ci(level_dir, "server.zip"), read_dir(server))
            print("patched  Levels/%s/server.zip" % level)
        info = os.path.join(levels_src, level, "Info")
        if os.path.isdir(info):
            info_dst = find_ci(level_dir, "Info") or os.path.join(level_dir, "Info")
            os.makedirs(info_dst, exist_ok=True)
            for arcname, path in files_under(info):
                shutil.copyfile(path, find_ci(info_dst, arcname) or os.path.join(info_dst, arcname))
            print("files    Levels/%s/Info" % level)

    if args.shader_fix:
        shaders = find_ci(target, "Shaders_client.zip")
        with zipfile.ZipFile(SHADER_FIX) as fix:
            patch_archive(shaders, {"TerrainShader_Hi.fx": fix.read("terrainshader_hi.fx")})
        print("patched  %s (terrain shader fix; clear the mod's shader cache)" % os.path.basename(shaders))

    print("Done: start BF2 with +modPath mods/openbf2 using the OpenBF2 client.")


if __name__ == "__main__":
    main()
