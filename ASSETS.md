# Assets used in Director's Cut: Front Page

Every asset in the build, where it comes from and its licence. Nothing from other games (names, logos, characters or
art) is in the game.

## Third-party
| Asset | Path in project | Source | Licence |
|-------|-----------------|--------|---------|
| Mannequin characters (Manny/Quinn meshes, skeleton, rifle and unarmed animations, ABP_Unarmed) | `Content/Characters/Mannequins/` | Unreal Engine 5 third-person template content (Epic Games) | Unreal Engine EULA: usable in Unreal Engine projects, including commercial ones |
| Basic shapes (cube, sphere, cylinder, cone) and BasicShapeMaterial | `/Engine/BasicShapes/` | Unreal Engine 5.9 engine content (Epic Games) | Unreal Engine EULA |
| Roboto font (HUD and menu text) | engine fonts `/Engine/EngineFonts/` | Google Roboto, shipped with Unreal Engine | Apache License 2.0 |

## Made for this game (no third-party source)
| Asset | Path | How it is made |
|-------|------|----------------|
| City, towers, rubble, debris | spawned at run time | C++ (`DCCityPlan`, `ADCBuilding`, `ADCDebrisPool`) from engine cube instances |
| Orbital strikes, supply pods, enemies | spawned at run time | C++ actors from engine basic shapes |
| `M_DCFlat` flat colour + glow material | `Content/DC/Materials/M_DCFlat.uasset` | built by `tools/content/build_content.py` |
| `L_City` empty level | `Content/DC/Maps/L_City.umap` | built by `tools/content/build_content.py`; everything in it is spawned by C++ |
| All sound and music | synthesised at start-up | `DCAudio.cpp` renders every cue from code recipes; no audio files |
| HUD, menus, newspaper front page | drawn at run time | Canvas HUD (`DCHUD*.cpp`) and Slate (`DCMenu.cpp`) |

## Store packs
None are used. Store packs, if added later, stay local-only under `Content/_LocalPacks/` and get a row here.
