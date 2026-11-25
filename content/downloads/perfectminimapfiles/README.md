# Perfect minimap: how to use these files

Source: Rhino's tutorial "Making The 'perfect' Minimap" (2007), Project Reality forum:
https://www.realitymod.com/forum/showthread.php?t=31987 (now forum.realitymod.org/viewtopic.php?t=31987).
The images in the thread are gone; this folder is the tutorial's `perfectminimapfiles.zip`.

These files make the **in-game minimap** only: `Levels/<map>/Hud/Minimap/ingameMap.dds`.
The **menu map** (`Info/<gamemode>_<size>_menumap.png`) is made from that minimap by a separate tool, see the end.

## What's in here

| File | What it does |
|---|---|
| `makeMegaMap_1024x4.con` | Editor console script for 4096 m maps (`heightmapcluster.setHeightmapSize 4096`, e.g. Beach_run). Takes 16 top-down shots, 2048 px each, every shot covering 1024 x 1024 m. |
| `makeMegaMap_1024x2.con` | Same for 2048 m maps: 4 shots. |
| `MiniMap 1024x4.atn` | Photoshop action: new 8192 x 8192 canvas, places the 16 shots, merges them. |
| `MiniMap 1024x2.atn` | Same for the 4 shots, 4096 x 4096 canvas. |

The scripts write `C:\Megamap1.dds` ... `C:\Megamap16.dds`. Under Wine that is `~/Games/bf2/drive_c/`.

Tile layout (east = right, north = up), as placed by the action:

```
1x4:   1  2  3  4      (tile 1 = top right, centred at x 1536, z 1536)
       5  6  7  8
       9 10 11 12
      13 14 15 16      (tile 16 = bottom left, centred at x -1536, z -1536)

1x2:   1  2
       3  4
```

Careful: tile 1 is at the **top right** and numbers run right to left.

## Steps

1. **Setup.** Copy the two `.con` files into the game folder (`Game/Battlefield 2/`).
   In the BF2 Editor, Options → Miscellaneous: set `CustomCommandFile1` to `makeMegaMap_1024x4.con`
   and `CustomCommandFile2` to `makeMegaMap_1024x2.con`. Start with nothing else running: a 1024x4 map
   needs about 2 GB of memory.
2. **Quick preview (optional).** The Level Editor's minimap "Generate" button makes a quick, low-quality
   `Editor/minimap.dds`. It is not the in-game one.
3. **Prepare the level. Don't save these changes:**
   - Build all final roads, so the road textures are on the terrain.
   - Lower sea level by about 1 m, and any water planes by the same amount. This matches the in-game water height.
   - Make overgrowth show up as static objects by relightmapping a small object such as a crate.
     If that doesn't work, a terrain lightmap is the fallback.
   - Load the right texture suffix for the map (Desert, Woodland, or custom).
   - Switch to Detail Texture Mode.
4. **Take the shots.**
   - 4096 m map: run custom command file 1 (16 shots).
   - 2048 m map: run custom command file 2 (4 shots).
   - Small maps (256/512): no script. Type `renderer.minCullDistance 2000` and `staticMeshRenderer.noLods 1`
     in the console, then click Show.
   - The scripts change render settings and don't restore them. Close the editor afterwards **without saving**.
5. **Stitch.** In Photoshop, run the matching action as a batch (Photoshop needs NVIDIA's DDS plugin to open
   the shots). Then clone over seams and water, preferably on new layers. Small maps: light edits only, no filters.
   Without Photoshop, the same layout can be built with GIMP or ImageMagick (untested sketch):
   `magick montage Megamap{4,3,2,1,8,7,6,5,12,11,10,9,16,15,14,13}.dds -tile 4x4 -geometry +0+0 mega.png`.
   This uses the mirrored order, because `montage` fills rows left to right.
6. **Save for the game.**
   - Resize to a power of two: 2048 x 2048 for a 4096 m map. Non-power-of-two sizes can break on ATI cards.
   - Save as `Hud/Minimap/ingameMap.dds`, DXT1, no mipmaps (about 2 MB at 2048).
     With ImageMagick: `magick mega.png -resize 2048x2048 -define dds:compression=dxt1 -define dds:mipmaps=0 ingameMap.dds`.
   - Don't delete `commanderMap.dds` and `commanderMicromap.dds` (removing them may crash). Shrink them to 32 x 32
     with the same settings if you don't use them.
   - The level then needs `Hud/Minimap/ingameMap.dds` inside its `client.zip`.

## Menu maps (separate tool)

The map previews in the menu, `Info/<gamemode>_<size>_menumap.png`, are DXT1 DDS files despite the `.png` name
in vanilla, and real PNGs in NAW; the game reads both.
They come from worldlife's info map generator (`modding/infomap-generator-v1.1.2.zip`; X-Ray's restyled
resources are in `modding/improved_infomap_generator_updated_for_python_3_*.rar`).
It reads `Hud/Minimap/ingameMap.dds` and the layer's `GamePlayObjects.con`, so **make the minimap first**.
It saves every `sp*` layer as `sp1_<size>_menumap.png`.
On Linux, use the Linux port in `modding/infomap-linux/`, or in Claude Code: `/generate-menumap <map> <layers>`
(e.g. `/generate-menumap Beach_run 16 32 64`).
