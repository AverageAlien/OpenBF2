# Setting up this OpenBF2 fork

This fork adds engine changes and a game mod (`mods/openbf2`) on top of upstream
[OpenBF2](https://github.com/B2BF/OpenBF2). You build the client from this repository, install the
mod into a retail Battlefield 2 1.50 folder with one script, and start the game with the client you
built. Nothing here is a prebuilt binary or a full game: you need your own Battlefield 2.

The steps are written so that a person or a coding agent can follow them in order. Agents changing
engine source must also follow [AGENTS.md](../AGENTS.md); this file only covers setup.

Status: the author plays it on Linux under Wine. The Windows build and launch steps follow upstream's
documented path but haven't been tried on Windows.

## What the fork adds

- **Invasion game mode** (`gpm_invasion`, single player with bots): attackers push from a main base
  through groups of control points taken in order; only the open group can be captured. Layers:
  Strike at Karkand 16 and 32, Sharqi Peninsula 16. Shown in the single player map list as
  "<map> (Invasion) - <size>".
- **Bot AI**: Invasion-aware squad planning, defence/attack split, vehicle use and rear-spawn rules,
  AI commander artillery, bots kept inside the layer's combat area, tap fire at range.
- **Native CCIP** bomb and rocket impact point for jets and attack helicopters.
- **Mobility kill**: burning aircraft and vehicles lose engine power (helicopters spin), and get it
  back after a repair.
- **Tank and APC rebalance**: APFSDS and HE shells sharing one breech (keys 1 and 3), slower turrets,
  6 s reload, 25 mm autocannon changes, ATGM and damage table changes, HE impact effects.
- **Commander fixes** for a listen server (artillery, UAV, scan, supply and vehicle drops, overhead
  view) and many other listen-server fixes.
- **Editor**: Invasion layer tools, AI strategic area editing, navmesh/spawn/vehicle overlays,
  combat area drawing.
- **Single player settings**: 95 bots (48 vs 48 with you), bot skill 0.95, 1000 tickets per team in
  conquest, FPS cap 144, intro movies skipped.

## Requirements

- Battlefield 2 installed and patched to 1.50 (the author's install also has Special Forces).
- Python 3 for the installer. The author tools in `content/tools` also need Pillow (`pip install pillow`).
- About 3 GB free next to the game: the mod starts as a copy of `mods/bf2`.
- To build on Windows: Visual Studio 2022 with "Desktop development with C++" (MSVC x86 tools and a
  Windows 10/11 SDK), CMake 3.21 or newer and Ninja.
- To build on Linux: Wine, `msitools`, Python 3 and git; the MSVC compiler runs under Wine.

Clone with Git's line-ending conversion off, so files keep the line endings they were committed with:

```sh
git clone -c core.autocrlf=false <this repository's URL> OpenBF2
```

## 1. Build the client

`bf2_client` builds `BF2.exe`, the rebuilt renderer, Flash menu and sound plugins, the Python runtime
link and the AI library (`aidll.dll`, staged beside `BF2.exe`). Without that AI library there are no
bots: the retail `AIDLL.dll` doesn't work with the rebuilt engine.

Always configure with `/O2`. The `msvc-x86` preset's `Reconstruction` build type has no compiler flags,
so without them the client is unoptimized and drops to 60-70 FPS with 95 bots.

### Windows

In an "x86 Native Tools Command Prompt for VS 2022", from the repository root:

```bat
cmd /c Code\Build\python\build_python.cmd x86
cmake --preset msvc-x86 -B out/build/release-x86 -DCMAKE_CXX_FLAGS_RECONSTRUCTION=/O2 -DCMAKE_C_FLAGS_RECONSTRUCTION=/O2 -DBUILD_TESTING=OFF
cmake --build out/build/release-x86 --target bf2_client
```

Optional, the editor: `cmake --build out/build/release-x86 --target bf2_editor`.
If the batch helpers can't find Visual Studio, set `BF2_VSDEVCMD` to the full path of `VsDevCmd.bat`
(see [docs/building.md](../docs/building.md)).

### Linux (Wine + msvc-wine)

The author's toolchain: MSVC from [msvc-wine](https://github.com/mstorsjo/msvc-wine), the Windows
builds of CMake and Ninja, all run under Wine in their own prefix. The wrapper
`content/tools/openbf2/winbuild.sh` runs one command in that environment from the repository root.

1. Install the packages (Arch: `sudo pacman -S wine msitools python git`).
2. Download MSVC 2022 and the Windows SDK:

   ```sh
   git clone https://github.com/mstorsjo/msvc-wine ~/src/msvc-wine
   ~/src/msvc-wine/vsdownload.py --major 17 --accept-license --with-devcmd yes --architecture x86 x64 --dest ~/src/msvc
   ~/src/msvc-wine/install.sh ~/src/msvc
   ```

3. Get the Windows builds of CMake 3.31.8 (`cmake-3.31.8-windows-x86_64.zip` from
   https://github.com/Kitware/CMake/releases) and Ninja 1.12.1 (`ninja-win.zip` from
   https://github.com/ninja-build/ninja/releases). Unpack them so these exist:
   `~/src/tools/cmake-3.31.8-windows-x86_64/bin/cmake.exe` and `~/src/tools/ninja/ninja.exe`.
4. Create the build prefix: `WINEPREFIX=~/src/wine-msvc wineboot -i`.
5. Build, from the repository root:

   ```sh
   W=content/tools/openbf2/winbuild.sh
   $W 'Code\Build\python\build_python.cmd' x86
   $W cmake --preset msvc-x86 -B out/build/release-x86 -DCMAKE_CXX_FLAGS_RECONSTRUCTION=/O2 -DCMAKE_C_FLAGS_RECONSTRUCTION=/O2 -DBUILD_TESTING=OFF
   $W cmake --build out/build/release-x86 --target bf2_client
   $W cmake --build out/build/release-x86 --target bf2_editor   # optional
   ```

   The Python step takes under a minute, the client a few minutes. Other locations can be given with
   `MSVC_WINEPREFIX`, `MSVC_DIR` and `WIN_TOOLS_DIR` (see the top of `winbuild.sh`).

Output: `out/build/release-x86/bin_client/` (and `bin_editor/`). Don't rebuild while the game runs
from that folder: the linker rewrites files the running game has loaded.

## 2. Linux only: Battlefield 2 under Wine

Skip this on Windows. The author's setup, with plain Wine (not Proton) and DXVK:

1. Make a prefix for the game, for example `~/Games/bf2`, and always run its programs with
   `WINEPREFIX=~/Games/bf2`.
2. Install from the DVD (mount the image, run `setup.exe` with Wine), then `BF2_Patch_1.41.exe` and
   `BF2_Patch_1.50.exe`.
3. `WINEPREFIX=~/Games/bf2 winetricks d3dx9 dxvk`.
4. The resolution in `Documents/Battlefield 2/Profiles/<profile>/Video.con` (inside the prefix) must be
   a display mode that exists, refresh rate included, or the game hits a breakpoint at start
   (`Unknown DynamicOption value` in `RendDX9.dll`). The default `800x600@60Hz` fails on displays
   that only offer other refresh rates; use your monitor's native mode, e.g. `2560x1440@144Hz`.
5. With DXVK keep antialiasing at 8 samples or less (16 crashes at start).
6. BF2 asks you to update the video driver on every start. Silence it by reporting an AMD GPU: put
   `d3d9.customVendorId = 1002` and `d3d9.customDeviceId = 73bf` in a `dxvk.conf` in the game folder.
7. Install the content with `--shader-fix` (step 3): without it DXVK shows black blinking terrain
   patches near the camera when terrain quality is above Low.

From inside Wine, the Linux filesystem is drive `Z:`.

## 3. Install the mod

```sh
python3 content/tools/install_content.py "<Battlefield 2 folder>" --shader-fix
```

On Windows: `py content\tools\install_content.py "C:\...\Battlefield 2"` (the shader fix is for
DXVK; it doesn't hurt on Windows).

It creates `mods/openbf2` as a copy of `mods/bf2` (once; `mods/bf2` is never changed) and puts the
fork's files into it: loose files, five new archives, the Invasion entry in `Menu_client.zip`, and the
level files into the levels' `server.zip` and `Info` folders. Run it again after pulling changes;
`--fresh` copies the mod from `mods/bf2` again first. After `--shader-fix`, delete the shader cache
`Documents/Battlefield 2/mods/openbf2/cache` if it exists.

## 4. Play

Linux:

```sh
BF2_GAME_DIR="<Battlefield 2 folder>" BF2_WINEPREFIX=~/Games/bf2 content/tools/openbf2/run-openbf2.sh
```

Windows:

```bat
content\tools\openbf2\run-openbf2.cmd "C:\...\Battlefield 2"
```

Both start `BF2.exe +menu 1 +fullscreen 1 +modPath mods/openbf2` with the game folder as working
directory (the engine needs that). On Linux the log goes to `<prefix>/debug/openbf2-<time>.log`, and
DXVK shows the frame rate (`DXVK_HUD=` turns it off).

- Single player: pick an Invasion map, e.g. "Strike at Karkand (Invasion) - 32". The menu still
  shows 16 players; `AI/AIDefault.ai` overrides it with 95 bots.
- Use fullscreen: under Wine (seen on Hyprland) the windowed mode draws no terrain.
- Quitting ends with exit code 3: a known unfinished shutdown path, harmless.
- The editor: `run-openbf2-editor.sh +loadMod openbf2 +loadLevel <level> +gameMode <mode> +layerSize <size>`
  (same settings). It only opens unpacked levels: extract the level's `server.zip` and `client.zip`
  into its folder, next to the zips. The game reads the zips, so pack edited files back into them.

## Repository layout of the content

| Path | Contents |
| --- | --- |
| `content/mods/openbf2/` | Loose mod files: `Init.con`, archive lists, `Mod.desc`, `AI/AIDefault.ai` (bot count and skill), `AI/AIBehaviours.ai`, Python game modes (`gpm_invasion.py`, `gpm_cq.py`) |
| `content/mods/openbf2/armorbuff_objects/`, `armorbuff_common/`, `armorbuff_menu/` | Tank/APC rebalance: changed vehicle and weapon tweaks, AI files, HE effects, cannon sounds, material tables, tank HUD and shell icons. Made by `tools/armorbuff/build_armorbuff.py` |
| `content/mods/openbf2/aircraftdamage_objects/` | Damage smoke/fire stages of jets, helicopters and land vehicles, CCIP flags on aircraft weapons |
| `content/mods/openbf2/ccip_menu_server/` | HUD files that draw the CCIP marker |
| `content/mods/openbf2/Menu_client/` | `gameModes.con` with the Invasion game mode, added to `Menu_client.zip` |
| `content/mods/openbf2/Levels/` | Invasion layers (`server/`, added to the level's `server.zip`) and their menu maps and `.desc` (`Info/`). Sharqi also gets the larger navmesh of the Nations At War version (`AIPathFinding/`) |
| `content/tools/install_content.py` | The installer above |
| `content/tools/openbf2/` | Build wrapper for Linux (`winbuild.sh`, `winbuild.cmd`, `msvc-env.cmd`) and launchers |
| `content/tools/invasion/` | `make_invasion_layer.py` (first Invasion layer from a conquest layer), `editor_to_layer.py`, `make_strategic_areas.py` (AI area draft), `navcheck.py` (is a point on the navmesh) |
| `content/tools/armorbuff/` | Builds the rebalance archives from the mod's own archives |
| `content/tools/infomap-linux/` | Menu map generator (`generate_menumap.py <map> <mode>/<size> --mod openbf2`), a Linux port of worldlife's tool with X-Ray's resources |
| `content/tools/port-map/` | Ports a level between mods, swapping factions and vehicles |
| `content/downloads/` | Third-party downloads, unchanged: terrain shader fix, worldlife's info map generator 1.1.2, X-Ray's restyled resources, Rhino's "perfect minimap" kit |

The author tools find the game through the `BF2_GAME_DIR` environment variable (default: the current
folder).

With Claude Code in this repository, two commands wrap the tools: `/generate-menumap <map> <layers>`
redraws a level's menu maps, and `/port-map <map> <source_mod> <target_mod>` ports a level between mods
(`.claude/commands/`).

## Credits

- Upstream OpenBF2 and the AI reconstruction it builds on (GPLv3, see [LICENSE](../LICENSE)).
- Battlefield 2 by DICE / EA. Files under `content/mods/openbf2` are modified copies of retail game
  files or new files in the game's formats; you need the retail game to use them.
- Sharqi Peninsula's navmesh comes from the Nations At War 6.0 mod.
- `content/downloads`: the "Shaders Client Fix" (ModDB), worldlife's info map generator, X-Ray's
  restyled info map resources, and Rhino's perfect minimap files (Project Reality forum, t=31987).
