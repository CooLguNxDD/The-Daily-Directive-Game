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

## Generated (R11, R25)
| Asset | Path | Tool | Notes |
|-------|------|------|-------|
| Title menu key art (R25 neon noir, replaces the poster key art) | `Content/DC/Art/T_MenuKeyArt.uasset` (source `Content/DC/Art/Source/menu-keyart.png`) | local ComfyUI (Qwen-Image) via `.claude/skills/cat-fleet/scripts/comfy.py image`, 2026-11-14 ~00:30 | prompt in `goal/epic-ai-mega-jam2/refs/noir/menu-keyart.md`; imported by `tools/content/build_content.py` |
| Draft card art, all nine upgrades (R25 neon noir, same subjects as the poster set) | `Content/DC/Art/T_Card_{sundrop,singularity,decoy,strobe,fisheye,cursed,domino,eclipse,rogue}.uasset` (sources `Content/DC/Art/Source/card-*.png`) | local ComfyUI (Qwen-Image), `comfy.py image` | prompts in `goal/epic-ai-mega-jam2/refs/noir/card-*.md`; imported at 256 px by `tools/content/build_content.py` |
| Newsprint paper texture (no longer drawn after R25; kept for history) | `Content/DC/Art/T_PaperGrain.uasset` (source `Content/DC/Art/Source/tex-paper.png`) | local ComfyUI (Qwen-Image) | the UI sheet is now dark glass drawn in code (`ADCHUD::PaperSheet`) |
| Stamp grit (worn mask) | `Content/DC/Art/T_StampGrit.uasset` (source `Content/DC/Art/Source/tex-stampgrit.png`, alpha = wear) | local ComfyUI (Qwen-Image) | over every score stamp (`ADCHUD::StampGrit`) |
| Critic reference board (not in the build) | `goal/epic-ai-mega-jam2/refs/board/*.png` | `scripts/media.sh` (agy) | poster city, newspaper front, strike call-in, ink stamp, voxel city; the human picks keepers |

## Made in code or from text (no third-party source)
| Asset | Path | How it is made |
|-------|------|----------------|
| Voxel city, terrain, rubble | spawned at run time | C++ voxel engine (`Source/.../Voxel/`) from recipes and districts in `Config/DefaultGame.ini` |
| `M_DCVoxel` vertex-colour voxel material | `Content/DC/Materials/M_DCVoxel.uasset` | `tools/content/build_content.py` |
| `M_DCFlatUnlit` unlit flat colour (danger beam, rings) | `Content/DC/Materials/M_DCFlatUnlit.uasset` | `tools/content/build_content.py` |
| Photographer outfit parts (R23): `outfit_cap`, `outfit_helmet`, `outfit_beret`, `outfit_vest`, `outfit_trench`, `outfit_backpack`, `outfit_filmbelt`, `outfit_boxcam` | `Content/DC/Voxel/outfit_*.dcvox` | `tools/voxel-forge/forge.py` from `tools/voxel-forge/recipes/outfit_*.vfr` |
| `M_DCInkOutline` ink-edge post material | `Content/DC/Materials/M_DCInkOutline.uasset` | `tools/content/build_content.py` (HLSL in the script) |
| Enemy, prop and player-gear voxel models (swarmer, spotlight, walker, props, the chest press camera `presscam`) | `Content/DC/Voxel/*.dcvox` | `tools/voxel-forge/forge.py` from `tools/voxel-forge/recipes/*.vfr` (R17) |
| Faction unit models (trooper, shambler, raider, soldier, skitter; Brute and Brood are scaled shamblers/skitters) and the objective package (`package`) | `Content/DC/Voxel/*.dcvox` | `tools/voxel-forge/forge.py` from `tools/voxel-forge/recipes/{trooper,shambler,raider,soldier,skitter,package}.vfr` (R3, R4; original designs) |
| Faction sound cues (FactionShot, Groan, FactionStrike) | synthesised at run time | C++ synth in `DCAudio.cpp` |
| Shareable ID card (turntable PNG) | written to `Saved/IdCards/` at run time | C++ canvas render target from the builder's turntable capture (`DCIdCard.cpp`, R5) |
| Newsprint photo print (halftone, grain, border); fallback stamp grit when `T_StampGrit` is missing | at run time | C++ (`Rules/DCNewsprint.cpp`, `DCHUD::StampGrit`) |
| Decay effects, voxel blast core, weather precipitation | at run time | C++ pooled instanced cubes (`DCVoxelFx`, `DCOrbitalStrike`) from ini rows |

## Store packs
None are used. Store packs, if added later, stay local-only under `Content/_LocalPacks/` and get a row here.
