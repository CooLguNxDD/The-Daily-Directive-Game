# Assets used in Director's Cut: Front Page

Every asset in the build, where it comes from and its licence. Nothing from other games (names, logos, characters or
art) is in the game.

## Third-party
| Asset | Path in project | Source | Licence |
|-------|-----------------|--------|---------|
| Mannequin characters (Manny/Quinn meshes, skeleton, rifle and unarmed animations, ABP_Unarmed) | `Content/Characters/Mannequins/` | Unreal Engine 5 third-person template content (Epic Games) | Unreal Engine EULA: usable in Unreal Engine projects, including commercial ones |
| Basic shapes (cube, sphere, cylinder, cone) and BasicShapeMaterial | `/Engine/BasicShapes/` | Unreal Engine 5.9 engine content (Epic Games) | Unreal Engine EULA |
| Roboto font (fallback only, when an OFL face is missing; engine debug text) | engine fonts `/Engine/EngineFonts/` | Google Roboto, shipped with Unreal Engine | Apache License 2.0 |
| Anton (headlines, menu title), Oswald (HUD body), UnifrakturMaguntia (masthead), Black Ops One (score stamps) | `Content/DC/Fonts/FF_*.uasset` (sources and licence texts in `Content/DC/Fonts/Source/`) | Google Fonts (github.com/google/fonts), fetched by the session 2026-11-13; imported by `tools/content/build_content.py` | SIL Open Font License 1.1 (`OFL-*.txt` beside each face) |

## Made for this game (no third-party source)
| Asset | Path | How it is made |
|-------|------|----------------|
| City, towers, rubble, debris | spawned at run time | C++ (`DCCityPlan`, `ADCBuilding`, `ADCDebrisPool`) from engine cube instances |
| Orbital strikes, supply pods, enemies | spawned at run time | C++ actors from engine basic shapes |
| `M_DCFlat` flat colour + glow material | `Content/DC/Materials/M_DCFlat.uasset` | built by `tools/content/build_content.py` |
| `L_City` empty level | `Content/DC/Maps/L_City.umap` | built by `tools/content/build_content.py`; everything in it is spawned by C++ |
| All sound and music | synthesised at start-up | `DCAudio.cpp` renders every cue from code recipes; no audio files |
| HUD, menus, newspaper front page | drawn at run time | Canvas HUD (`DCHUD*.cpp`) and Slate (`DCMenu.cpp`) |

## Generated (R11)
| Asset | Path | Tool | Notes |
|-------|------|------|-------|
| Title menu key art | `Content/DC/Art/T_MenuKeyArt.uasset` (source `Content/DC/Art/Source/menu-keyart.png`) | `scripts/media.sh` (agy backend, 2026-11-13; codex was out of quota) | prompt in `goal/epic-ai-mega-jam2/refs/game/menu-keyart.md`; imported by `tools/content/build_content.py` |
| Draft card art: Sun-Drop, Gravity Singularity | `Content/DC/Art/T_Card_sundrop.uasset`, `T_Card_singularity.uasset` (sources `Content/DC/Art/Source/card-*.png`) | `scripts/media.sh` (agy backend, 2026-11-13) | prompts in `goal/epic-ai-mega-jam2/refs/game/card-*.md`; imported at 256 px by `tools/content/build_content.py`; the other seven cards wait on image quota (text-only until then) |
| Critic reference board (not in the build) | `goal/epic-ai-mega-jam2/refs/board/*.png` | `scripts/media.sh` (agy) | poster city, newspaper front, strike call-in, ink stamp, voxel city; the human picks keepers |

## Made in code or from text (no third-party source)
| Asset | Path | How it is made |
|-------|------|----------------|
| Voxel city, terrain, rubble | spawned at run time | C++ voxel engine (`Source/.../Voxel/`) from recipes and districts in `Config/DefaultGame.ini` |
| `M_DCVoxel` vertex-colour voxel material | `Content/DC/Materials/M_DCVoxel.uasset` | `tools/content/build_content.py` |
| `M_DCInkOutline` ink-edge post material | `Content/DC/Materials/M_DCInkOutline.uasset` | `tools/content/build_content.py` (HLSL in the script) |
| Enemy, prop and player-gear voxel models (swarmer, spotlight, walker, props, the chest press camera `presscam`) | `Content/DC/Voxel/*.dcvox` | `tools/voxel-forge/forge.py` from `tools/voxel-forge/recipes/*.vfr` (R17) |
| Newsprint photo print, stamp grit | at run time | C++ (`Rules/DCNewsprint.cpp`, `DCHUD::StampGrit`) |
| Decay effects, voxel blast core, weather precipitation | at run time | C++ pooled instanced cubes (`DCVoxelFx`, `DCOrbitalStrike`) from ini rows |

## Store packs
None are used. Store packs, if added later, stay local-only under `Content/_LocalPacks/` and get a row here.
