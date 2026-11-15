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

## Card kinds (R22, final run)
- Draft cards beyond unlocks and gear: `+Upgrades` rows whose `Effect` has hooks in `DCUpgrades.cpp` (registry of
  Apply + optional SectorStart / Interact / Downed): `Companion` (drones each sector, `Companion=`, `Count=`), `Deploy`
  (Interact sets a turret down, `Count=` charges per sector), `LastStand` (going down calls the `Unlock=` stratagem on
  the spot). Pool `Support` shows as FIELD SUPPORT. Strike and support cards stay `UnlockStratagem` rows: Carpet Bomb
  uses the `Spiral` pattern, Paratrooper Squad the `Reinforce` call effect (`SupportFaction`, `SupportCount`).
- Companions: `+Companions` rows (`Rules/DCCompanionRules`: mover registry Orbit/Fixed, target pick, deploy ground);
  one pooled actor `ADCCompanion` (`DCCompanion.cpp`, pool `UDCCompanions`, `CompanionPrewarm`/`CompanionPoolMax`):
  host targets and damages, every machine places its copy from the replicated owner/anchor, shots multicast.
  Models `tools/voxel-forge/recipes/companion_{drone,sentry}.vfr`. Logs: `DCCOMPANION`, `DCREINFORCE`, `DCLASTSTAND`.

## Weapon chips (R24, final run)
- Every weapon hit wears the city (advisor a-37): `+Weapons` rows carry `ChipDamage`, `ChipRadiusCm`, `ChipsPerSecond`;
  `Tuning=` carries `ChipsEnabled` and the `Chip*` window, budget, support and bite numbers. Host-only ledger
  `Rules/DCChipRules` (bucket damage, per-shooter rate, window flush under entry and render-cell budgets, structure
  trigger once per interval under the body-load gate); `ADCVoxelCity::HostChip`/`TickChips` in `DCVoxelChips.cpp`,
  called from the host's pellet loop (`DCCharacter.cpp` ServerFire).
- Op kinds dispatch through the registry in `DCVoxelOps.cpp` (`FindKind`/`Run`: Strike, Fell, Rest, Chip, one shared
  `StructureTail`). A `Chip` op lists spheres (`Chips[]`, tower lot or `PropGrid`) and optional `StructureLots` (base
  bite on the shooters' side, then the structure pass; committed two-phase like a strike). Chip-only ops skip the
  street snapshot, keep the surface cache, throw crumbs (never shard bodies) and fold on replay.
- Evidence flags: `-dcchips` (host: chips on while the data default is off), `-dcchipfire` (pilot: sustained fire at
  the nearest tower's wall foot for 60 s, fixed-pose `chipfire_*` frames, `DEMO chipfire` hash lines); logs `DCCHIP`
  (host ops; on clients `DCCHIP check ... match=host` once idle at the newest chip entry and its host hash arrived).

## Larger world (R23, final run)
- District variants: `+Districts` rows carry `Sector` and `Weight`; `UDCVoxelSettings::District(Sector, SectorSeed)` rolls
  one row per sector (three per sector now: 4x4 first-run rows plus 5x5/5x4 rows; keep GridY <= GridX, the spawn sits at
  -HalfExtent on the west street). Lot rules may add `TowerRecipe` (district tower mix) and `LandmarkRecipe` (landmark
  roll) slots; `OpenKinds` are paved lots without a building (Market, Court, Parking, Yard, Park) dressed by Lot layers;
  `StreetMaterial`/`LotMaterial` pick palette rows for the ground.
- Craft registries in `DCVoxelCraft.cpp`: roofs (+Tiered, Dome, Helipad, Sawtooth, Shanty, Beacon, Garden, Marquee),
  signs (+Banner, Crown; `BackCells` = solid backing at paint time), bays (+R, T, X). Test `District.RecipeKinds` checks
  every recipe and rule set against them.
- Prop layers added: Escape, Market, Court, Parking, Yard, Park, Balcony, Monument (landmark forecourt), Mural (a
  `Value="None"` choice is a blank wall). Generated sign art lives in `tools/voxel-forge/art/` (`bb_*`, `mural_*`).
- Evidence: District.CityBuilder logs `DCDISTRICT` (towers, solid voxels, chunks, craft ms) and `DCWORLD` (first run seed
  rolling each district: use with `-dcseed=N -dcstartsector=S`); plan PNGs headless: `UnrealEditor-Cmd <uproject>
  -run=DCCityPreview -seed=N -out=<dir>`.

## Editor MCP
- Epic's MCP server (`ModelContextProtocol` + `AllToolsets` in the `.uproject`) serves `unreal-mcp` on `http://127.0.0.1:23444/mcp` (auto-start and port in the per-user `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`; `.mcp.json` and the other client configs point there).
- Claude Code: the `unreal-mcp` skill of the `unreal-engine-skills-for-claude-code` plugin. Start a session with `cats game-dev` from the monorepo root.
- A Cat Fleet lead runs its own editor on port 8100; 23444 is the human's.

## Rules
@.fleet-rules.md
