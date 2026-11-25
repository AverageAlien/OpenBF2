---
description: Regenerate a level's menu map previews (Info/<gamemode>_<size>_menuMap.png) from its minimap and game mode layers
argument-hint: <map_name> <layers...>  (layers: 16 | 32 | 64 | sp3 | sp3/64 | gpm_cq/16 | gpm_invasion/32 | all)
---

Regenerate the BF2 menu maps for: $ARGUMENTS

The tool is `content/tools/infomap-linux/generate_menumap.py` (Linux port of worldlife's info map generator with
X-Ray's restyled resources; see `content/SETUP.md`). It finds the game through the `BF2_GAME_DIR` environment
variable (the Battlefield 2 folder; default: the current folder); ask the user for that folder if it isn't set.
It needs Python 3 with Pillow.

It reads the installed level's `server.zip` (Init.con, Heightdata.con, GameModes/*/*/GamePlayObjects.con) and
`client.zip` (Hud/Minimap/ingameMap.dds), writes real PNG files into the level's `Info/` (`--format dds` writes
vanilla-style DXT1 DDS under the `.png` name instead; the game reads both, but normal image viewers can't open DDS),
and copies each replaced file to `<name>.bak` once (an existing `.bak` is kept, so it always holds the oldest
original). An existing file's name case is reused (`_menuMap.png` vs `_menumap.png`).

Layer arguments: a size (`16`) selects every layer of that size (e.g. both `sp1/16` and `gpm_cq/16`), a game mode
(`sp3`) selects all its sizes, `gamemode/size` selects one layer, `all` selects everything. Every `sp*` layer is
saved as `sp1_<size>_menumap.png`, because that's the name the game menu loads.

Steps:

1. If the map name or the layers are missing from the arguments, ask for them. Don't guess the layers.
2. Run from the repository root:
   `BF2_GAME_DIR="<game folder>" python3 content/tools/infomap-linux/generate_menumap.py <map> <layers...> --dry-run --preview-dir <scratchpad>/menumap-preview`
   Add `--mod <mod>` if the level is installed in several mods (for this fork usually `--mod openbf2`).
   Use `--style woodland` only if the user asks for it or the level is clearly woodland (default: desert stripes).
3. Look at every preview PNG before installing. Check that the flags sit on the right spots and that the stripes
   cover the area outside the combat area. Call out anything odd, such as: no stripes (the layer has no `team 0`
   combat area), a very blurry image (small combat area zoomed into a low-resolution `ingameMap.dds`; only a
   better minimap fixes it, see `content/downloads/perfectminimapfiles/README.md`), or flags missing (a custom
   team needs its own `maps/flags/<team>/miniMap_CP.tga`).
4. If the user keeps an editor source copy of the level elsewhere, pass its folder with `--also "<path>"` so its
   `Info/` gets the same files. Ask; don't search the disk for one.
5. Run the same command without `--dry-run` (keep `--also`, `--mod`, `--style`). If the script exits with an
   error (missing team flag, unknown layer, name collision), report it and stop.
6. Report briefly: which layers were written, where, which `.bak` files exist, and that the result wasn't
   checked in the game menu. For a level this fork ships, also copy the new files into
   `content/mods/openbf2/Levels/<map>/Info/` so `install_content.py` installs them.
