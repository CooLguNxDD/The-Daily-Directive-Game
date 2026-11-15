# Epic AI Mega Jam 2

Unreal Engine 5.9 jam game. Engine: `L:/unreal/UE_5.9` (registered source build, found from `EngineAssociation`).

## Layout
- `Source/Epic_AI_Mega_Jam2/`: the game module. Game rules, state, replication and tuning live here and in text data, so reviewers can read them. Blueprints stay thin on C++ base classes (wiring, visuals, widget layout).
- `Source/Epic_AI_Mega_Jam2/Private/Tests/`: automation tests (`Epic_AI_Mega_Jam2.*`), run headless under `-nullrhi` by the gate.
- `Content/`: assets through Git LFS (`.gitattributes`). Store packs stay local-only under `Content/_LocalPacks/` (ignored).
- `tools/gate/verify.sh`: the gate. Fast tier: compile `Epic_AI_Mega_Jam2Editor`, then automation tests. Full tier (`FLEET_GATE_TIER=full`): also cook (`UNREAL_GATE_PACKAGE=1` packages). Needs `UE_ROOT=L:/unreal/UE_5.9`.

## Voxel city and destruction (Director's Cut)
- `Source/.../Voxel/`: voxel grids, crafting from recipe/district data (`DCVoxelCraft`), ops (`DCVoxelOps`: carve, bite,
  structure, settle, `FragmentTree`, `DeriveCosmetics`, host `ApplyRest`). `DCVoxelCity.cpp` meshes on workers, runs
  ops in Seq order (two-phase: planned ahead, committed at detonation), drives the R26 physics (chunk/leaf body pool,
  shatter, per-body freeze) and turns the host's rest poses into a replicated `Rest` op. Tuning: `FDCVoxelTuning`
  (`Public/Voxel/DCVoxelTypes.h`), overridable in `Config/DefaultGame.ini` `Tuning=`.
- City props (R2): `+PropLayers` rows (Placer from the placer registry in `DCDistrictBuilder.cpp`: Street, Lot,
  Frontage, Roof, Wall, Cable; optional Mount from the mount registry in `DCVoxelCraft.cpp`: Roof, RoofEdge, Wall). A
  district's prop rule set (`Props=` of its `+Districts` row) turns a layer on with the slots `<Id>` (models) and
  `<Id>Count`. Ground props and cables go into the 5 cm prop grid; roof and wall mounts are stamped into the tower's
  own grid (they fall with it). Models: `tools/voxel-forge/recipes/*.vfr` (skill `voxel-forge`).
- Look data: `Config/DefaultGame.ini` `Look=`, `+PostLayers`, `+WeatherProfiles`, `+Palette`, `+Recipes`; materials
  are built by `tools/content/build_content.py`.
- Demo and evidence flags (game or packaged exe): `-dcdemo` (pilot), `-dcdemoquit`, `-dcseed=N`, `-dcshots=<dir>`,
  `-dcshotevery=<s>`, `-dcmenuflow=<dir>` (real menu click), `-dcbuilderflow=<dir>`, `-dccollapsecam=<dir>` (fixed wide
  frames of each strike's collapse), `-dcmedic` / `-dccostar` (co-op client helper), `-dcperf` (per-second DCPERF lines),
  `-dcsectorseconds=N`, `-dccityshot=<dir>`. From Git Bash, `export MSYS_NO_PATHCONV=1` before passing `/Game/...` maps.

## Factions, objectives, ID card (final run)
- Factions (R3): `+Factions`, `FactionMix`, unit rows on `Behaviour="Infantry"` in `Config/DefaultGame.ini`; pure rules
  `Rules/DCFactionRules` (mix roll, hostility, territory); host director `DCFactionDirector.cpp` (squads, allies,
  faction strikes via `CallStrike`, territory); live unit list `UDCUnits`; behaviour/goal/formation registries in
  `DCEnemy.cpp`. Faction strikes are `+Stratagems` rows with `bStartsUnlocked=False` and `+Carves` detail rows.
- Objectives (R4): `+Objectives`, `ObjectiveLook`; pure stage table `Rules/DCObjectiveRules`; replicated state and
  visuals `ADCObjective`; host logic `DCObjectiveDirector.cpp`.
- ID card (R5): `DCIdCard.cpp` (`ADCOutfitPreview::ExportCard`), `Card=` data, PNGs in `Saved/IdCards/`.
- Builder (R18): `+OutfitParts` (append only: a share code stores each part's index inside its slot, in data order),
  `+OutfitSwatches`, `+OutfitGlows` (the `SwatchGlow` palette slot), `+OutfitPresets` (by ids); pure rules
  `Rules/DCOutfit` (versioned codes: `Versions[]` table, DC1 = no glow, DC2 newest; `FromPreset`, `Random`). Test
  `Outfit.OldCodes` pins old codes by part id. `-dcbuilderflow=<dir>` frames every part, preset, random and glow.
- Destruction budget: `ADCVoxelCity::BodyLoad`; crumbs `UDCCrumbField` (`Crumb*` in `Tuning=`).
- Evidence flags: `-dcmain=<Faction>`, `-dcobjective=<Id>`, `-dcobjrun` (pilot runs the objective), `-dcenemytrace`;
  look numbers `tools/evidence/look_metrics.py`.

## Unit animation (R20, final run)
- Units with a `Gait=` on their `+Enemies` row animate: `+UnitGaits` (stride, bob, lean, limb swings per role),
  `+UnitClips` (spawn, attack, flinch, death moves), `+AnimLods` (pose rate by camera distance) in
  `Config/DefaultGame.ini`; pure rule `Rules/DCUnitAnim` (clip state table, envelope, pose); applier
  `UDCUnitAnimator` (`DCUnitAnimator.cpp`: local on every machine from motion, health and the replicated `AttackSeq`;
  quaternions per segment into custom primitive data 1..28); material `M_DCVoxelUnit` (build_content.py, WPO turns
  each segment about its joint from UV1/UV2). Segments are voxel-forge `part` lines. Death clip plays, then crumbs.
- Evidence: `-dcanimshot=<dir>` (side lineup, frames per state, `-dcanimkinds=A+B+C`, `-dcanimshotquit`),
  `-dcanimswarm=N` (+`-dcanimoff`) for DCANIMPERF lines.

## Editor MCP
- Epic's MCP server (`ModelContextProtocol` + `AllToolsets` in the `.uproject`) serves `unreal-mcp` on `http://127.0.0.1:23444/mcp` (auto-start and port in the per-user `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`; `.mcp.json` and the other client configs point there).
- Claude Code: the `unreal-mcp` skill of the `unreal-engine-skills-for-claude-code` plugin. Start a session with `cats game-dev` from the monorepo root.
- A Cat Fleet lead runs its own editor on port 8100; 23444 is the human's.

## Rules
@.fleet-rules.md
