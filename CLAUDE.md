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
- Look data: `Config/DefaultGame.ini` `Look=`, `+PostLayers`, `+WeatherProfiles`, `+Palette`, `+Recipes`; materials
  are built by `tools/content/build_content.py`.
- Demo and evidence flags (game or packaged exe): `-dcdemo` (pilot), `-dcdemoquit`, `-dcseed=N`, `-dcshots=<dir>`,
  `-dcshotevery=<s>`, `-dcmenuflow=<dir>` (real menu click), `-dcbuilderflow=<dir>`, `-dccollapsecam=<dir>` (fixed wide
  frames of each strike's collapse), `-dcmedic` / `-dccostar` (co-op client helper), `-dcperf` (per-second DCPERF lines),
  `-dcsectorseconds=N`, `-dccityshot=<dir>`. From Git Bash, `export MSYS_NO_PATHCONV=1` before passing `/Game/...` maps.

## Editor MCP
- Epic's MCP server (`ModelContextProtocol` + `AllToolsets` in the `.uproject`) serves `unreal-mcp` on `http://127.0.0.1:23444/mcp` (auto-start and port in the per-user `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`; `.mcp.json` and the other client configs point there).
- Claude Code: the `unreal-mcp` skill of the `unreal-engine-skills-for-claude-code` plugin. Start a session with `cats game-dev` from the monorepo root.
- A Cat Fleet lead runs its own editor on port 8100; 23444 is the human's.

## Rules
@.fleet-rules.md
