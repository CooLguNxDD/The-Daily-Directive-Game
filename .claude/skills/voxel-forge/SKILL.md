---
name: voxel-forge
description: Make voxel models (props, enemies, characters, building parts) for Director's Cut from a short text recipe. Use when the game needs a new model or a model changes - write or edit a .vfr recipe, run the forge, look at the preview, and the game loads the .dcvox at run time. Not for the city towers (those are crafted from building recipes in Config/DefaultGame.ini).
---

# voxel-forge

Recipe text in, voxel model out. No engine, no editor: a Python script writes the game's text voxel format and a
preview image.

## Use

```bash
python tools/voxel-forge/forge.py tools/voxel-forge/recipes/swarmer.vfr   # one model
python tools/voxel-forge/forge.py tools/voxel-forge/recipes               # every recipe
```

- Model: `Content/DC/Voxel/<name>.dcvox` (text, committed, staged as a non-UFS file in packaged builds).
- Preview: `Saved/VoxelForge/<name>.png` (iso, front, side). Look at it before you wire the model in: a metric never
  proves a look. Copy the preview into `.fleet-task/evidence/` when it backs a claim.
- Colours come from the voxel palette (`+Palette` rows of `[/Script/Epic_AI_Mega_Jam2.DCVoxelSettings]` in
  `Config/DefaultGame.ini`). A recipe names palette materials, never colours, so the art bible stays in one place.

## Recipe language

One op per line, `#` comments. Voxels: x right (the model faces +x, the actor's forward), y left-right, z up.

| Op | Meaning |
|----|---------|
| `name n` / `grid cm` / `size x y z` | model name, voxel size in cm (5 props and enemies, 10 big machines), volume |
| `slot body Ink` | a slot used by shapes, bound to a palette material |
| `box x0 y0 z0 x1 y1 z1 slot` | filled box, max exclusive |
| `shell x0 y0 z0 x1 y1 z1 t slot` | hollow box, wall thickness t |
| `sphere cx cy cz r slot`, `ellipsoid cx cy cz rx ry rz slot` | balls |
| `cyl cx cy z0 z1 r slot`, `cone cx cy z0 z1 r0 r1 slot` | vertical cylinder, cone or frustum |
| `beam x0 y0 z0 x1 y1 z1 r slot` | thick line: legs, masts, barrels |
| `poly z0 z1 slot x,y x,y ...` | 2D outline extruded from z0 to z1 (angular silhouettes) |
| `carve <shape args>` | any shape without a slot: cuts air |
| `paint <shape args> slot` | recolours solid voxels only |
| `mirror x\|y\|z` | copies the low half onto the high half (build one side, mirror) |
| `noise p seed slot` | removes surface voxels of a slot with probability p (wear, rubble) |
| `pivot x y z` | origin (default bottom centre); the game attaches the model there |
| `part Role x0 y0 z0 x1 y1 z1 jx jy jz` | R20 animation segment: solid voxels in the box (later parts win) turn with Role (`LegL`, `LegR`, `ArmL`, `ArmR`, `Head`, ...) about the joint; at most 7 roles. The preview's 4th panel tints the parts |

## Art bible rules for recipes

- Palette: Ink/InkBlue/InkBrown silhouettes, Paper cream, Steel trim. `Danger` (propaganda red) only on threats
  (enemy eyes, warning parts). `Signal` (yellow) only on things the player can use. `Lamp` for light sources.
- Friendly shapes are rounded (ellipsoid, cyl); threats are sharp and angular (poly, cone, beam).
- Keep models small in voxels: a 5 cm grid at about 30 x 30 x 60 is plenty for an enemy; the mesher merges flat faces.

## Wire a model into the game

- Enemies: `Model="<name>",ModelScale=..,ModelOffset=(X=..,Y=..,Z=..)` on the `+Enemies` row (offset places the
  model's pivot relative to the actor, which flies at HoverHeight).
- Code: `DCVoxelModels::Mesh(Name)` returns the cached mesh (`Source/Epic_AI_Mega_Jam2/Public/Voxel/DCVoxelModel.h`);
  put it on a `UProceduralMeshComponent` with the `M_DCVoxel` material.

## Animated units (R20)

- A unit animates when its `+Enemies` row names a `Gait=` (`+UnitGaits` rows, `Rules/DCUnitAnim.h`): each gait limb's
  `Role` must be a `part` of the model (test `UnitAnim.Data`). Cut legs at the hip, arms at the shoulder, the head at
  the neck; put the joint on the cut. Parts of one role may sit apart (a crawler's tripod of legs).
- The game meshes each part on its own and the unit material (`M_DCVoxelUnit`) turns it about its joint in the vertex
  shader. Check the tinted panel of the preview, then frames from `-dcanimshot=<dir>` (side views per state).

## Format (`.dcvox`, version 1)

```
dcvox 1
name swarmer
grid_cm 5
size 28 20 14
pivot 14 10 0
slots Ink Steel Danger
solid 2474
rle
120*0 4*1 ...        # count*slot, x fastest, then y, then z; slot 0 is air
```
The game rejects a file whose runs do not fill the volume exactly (untrusted data from disk).
