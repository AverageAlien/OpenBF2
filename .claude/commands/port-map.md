---
description: Port a level from one BF2 mod to another (e.g. Nations At War -> vanilla bf2 or openbf2), swapping factions and vehicles for the target's and copying the static objects the target lacks
argument-hint: <map> <source_mod> <target_mod>  (mods: folder name under <games>/*/mods/, or a path)
---

Port the BF2 level: $ARGUMENTS

The tool is `content/tools/port-map/port_map.py` (run from the repository root, with `python3 -I`). It was built
from a hand port of NAW's POE_Odessa to vanilla, and a staged rebuild of that port matched the hand-made one.
`analyze` reports what the target lacks and writes a spec skeleton; `build` applies the filled spec. Read the
script's docstring for details.

Mods can be given as paths (safest) or as folder names. Names are searched in `<games>/*/mods/`, where `<games>` is
`BF2_GAMES_DIR`, or the folder above `BF2_GAME_DIR` when that is set. Ask the user where the source and target
mods are if that isn't clear. The tool writes into the target mod: pick it on purpose (`mods/openbf2` for content
that uses this fork's engine, `mods/bf2` to keep it playable with the retail exe).

What it does, so you can explain it:
- Factions: the team lines of `Init.con` (names, languages, flags, kits) are replaced by the ones of a team of a
  reference level in the target (e.g. vanilla Strike_at_Karkand: team 1 MEC, team 2 US). Drop-vehicle lines
  (`setTeamDropVehicle`) are removed when `remove_drop_vehicles` is true; then add the target faction's own
  drop vehicles back by hand, or the commander has no vehicle drop.
- Spawners: in every `GamePlayObjects.con`, templates the target lacks are replaced, removed (a spawner left with
  no template is deleted with its `Object.create` block) or ported as-is (`port`).
- Static objects, road templates and other files (terrain detail and sky textures, ambient sounds) the target lacks
  are copied from the source into `<target>/<prefix>_objects.zip` / `_common.zip` / `_menu.zip` (merged with
  existing ones, only paths the target doesn't have), mounted last in `ServerArchives.con` and
  `ClientArchives.con` (copies `*.con.before-<level>` are kept).
- Localization: the level name and control point names go into `Localization/English/English_<PREFIX>.utxt`.
- `Strategies.ai` files are dropped by default, so bots use the target's default strategies. Nations At War's run
  ESAI from `/mods/naw/ESAI`, which crashes a target without it right after joining.

Steps:

1. If the map or either mod is missing from the arguments, ask. If the script says a mod name is ambiguous, it picks
   the one that has the level; if several have it, ask which one.
2. Run `python3 -I content/tools/port-map/port_map.py analyze <map> <source_mod> <target_mod> --spec <scratchpad>/port-<map>.json`.
   If the target already has the level, say so and ask before going on (the build then needs `--force`).
3. Present two tables to the user and **stop until they approve or change them**. Don't build before that.
   - Factions: each source team -> a target faction, with the reference level and team the lines come from
     (pick a target level whose `Init.con` has that faction; check it).
   - Every spawned template the target lacks: original, count, suggested replacement from the target catalogue
     in the report (same role and team side: tank -> tank, transport heli -> transport heli, jeep -> jeep), or
     remove / port. Also list templates that the target already has, so the user sees the whole picture.
     Defaults that worked for Nations At War -> vanilla: self-propelled artillery is removed (not swapped for
     stationary artillery; a spawner shared by both teams' self-propelled guns goes entirely), drones and pickup-kit
     spawners are removed, supply crates (`aix_resupplycrate`) are ported as-is.
     Mention the statics/files that will be copied (count, MB), unresolved paths, material mismatches and
     `Strategies.ai` handling.
4. Fill the spec: `teams` (`from_level`, `from_team`), `replace` (every key needs a value unless it's in `remove`
   or `port`), `remove`, `port`, `desc.name` (rename factions in it), `desc.music` (one of the target's menu music
   files when the report says the target lacks the original), `desc.replace` (text swaps in the briefing, e.g. old
   faction names), `localization_replace` (e.g. "Ukrainian Airfield" -> "Airfield"), `init_replace` (comment
   headers that name the old factions), `strategies` (`drop`, or `keep` only if the user asks and the scripts it
   runs exist in the target).
5. Dry run: `python3 -I content/tools/port-map/port_map.py build <spec> --stage <scratchpad>/port-stage`. Check the
   output: removed spawners, files added, and `unzip -p <stage>/Levels/<map>/server.zip Init.con` for the team block.
6. Real run: the same without `--stage` (add `--force` only if the user agreed to replace an existing level).
7. Report what was done and how to test (single player, with the client matching the target mod; for
   `mods/openbf2`, `content/tools/openbf2/run-openbf2.sh`, log in `<prefix>/debug/openbf2-<time>.log`).
   Known failure signs and their causes:
   - crash while loading (around 62%) in `RendDX9.dll` with `Texture not found` before it: a texture is missing;
     find which file names it (terrain `terraindata.raw`, `sky.con`, meshes) and add it to the archive.
   - a breakpoint (`int3`) in `RendDX9.dll` while loading: a missing mesh. Under Wine, run the game with
     `winedbg --file <script>` where the script repeats `x /s *(int*)(*(int*)($ebp+8)+4)` and `cont`: it prints
     the file name at each break and steps past it.
   - crash right after joining in the AI library: the bots have no strategies (a `Strategies.ai` that runs scripts
     the target lacks).
   - control points shown as `CPNAME_...` keys: localization not picked up.
   The menu map previews are the source's; offer `/generate-menumap <map> <layers>` to redraw them with the new
   factions' flags.
8. Tell the user which files changed (level, archives, `*Archives.con`, localization), and that it isn't tested
   in-game yet.
