# The Daily Directive

> Epic AI MegaJam 2026 · theme **DIRECTOR'S CUT** · formerly *Director's Cut: Front Page* · Unreal Engine 5.9 (C++)

[![The Daily Directive gameplay video](https://img.youtube.com/vi/xV4vN-_Von8/maxresdefault.jpg)](https://youtu.be/xV4vN-_Von8)
*Watch the gameplay video on YouTube.*

A co-op photo roguelike shooter. Call in an orbital strike, run toward it, and photograph the collapse at peak bloom. Photos pay for bigger bombs. The run ends with a re-staged highlight reel, a final cut, a vote, and a war-newspaper front page.

The game was built by an AI lead agent (Cat Fleet) working under a human director. [`DEVLOG.md`](./DEVLOG.md) records what was decided, why, and what broke.

## Pillars

- **The shot is the weapon.** Playing safe with the rifle loses upgrades.
- **Every blast is a photo opportunity.**
- **Risk for the frame.** The best shots are taken close to the danger.
- **The reel is the reward.**

## How a run plays

1. **Lobby**, then three sectors with a card draft between each.
2. **Strike.** Call a stratagem and watch the telegraphed collapse. Towers shake, creak and flash red before they give way.
3. **Shoot.** Photos are graded on the 3D scene at the shutter, not on pixels:
   - Blast timing: *Overexposed* 1.5x at 0.0 s, *Peak Cinema* 3.0x at 0.2–0.8 s.
   - Centring, occlusion, collapses and debris in frame, enemies, and distance to danger.
   - Spotlight enemies blow out a photo's exposure (0.75x).
4. **Meet HQ's quota.** Each sector has a photo-score target. Missing it costs the draft, and swarm pressure scales with how far the crew lags.
5. **Draft.** Three pools (Cinema Stratagems, Camera Gear, City Mutators), three options each.
6. **The reel.** Every machine rebuilds the sector from the run seed and replays the recorded collapses in slow motion. It then freezes, flashes white, and cuts to the real photo.
7. **Final cut, vote and front page.** One vote per player for someone else's shot. Votes can lift the runner-up to the headline.

## Features

- **Voxel city and real destruction.** A small C++ voxel engine with greedy meshing on worker threads. Strikes are host ops replayed on every machine (carve, bite, structure, settle, rest), and host and client op hashes match. Debris is pooled Chaos rigid bodies.
- **City crafted from data.** Building recipes, district rules, a seeded PCG rule table, and prop layers, all in `Config/DefaultGame.ini`.
- **Factions and objectives** with squads, territory and objective stages.
- **Voxel character builder** with versioned share codes and ID cards.
- **Co-op** for up to 4 players: LAN discovery (with a JOIN list beside join-by-IP) and internet play over EOS. Includes revives and a *second take* camera during reel replays.
- **Weather** as a pure forecast from seed, sector and server clock, with stacked post layers.
- **All audio synthesised from code recipes** at start-up.
- **Neon-noir art direction**: night sky, slate voxels, warm dim windows, and rare magenta and cyan signs.

Reported performance on the packaged build with 4 players: 99 fps steady, 83 fps average during collapses (see `DEVLOG.md`).

## Project layout

| Path | What |
|---|---|
| `Source/Epic_AI_Mega_Jam2/` | Game module: rules, state, replication, tuning. No Blueprints, so reviewers can read it all |
| `Source/Epic_AI_Mega_Jam2/Voxel/` | Voxel grids, crafting, ops and the city |
| `Source/Epic_AI_Mega_Jam2/Private/Tests/` | Automation tests, run headless |
| `Config/DefaultGame.ini` | Tuning, palettes, recipes, factions, stratagems |
| `Content/` | Assets via Git LFS. Store packs stay local in `Content/_LocalPacks/` (ignored) |
| `tools/` | Gate (`tools/gate/verify.sh`), evidence scripts, content build, `voxel-forge` recipes |
| `docs/AUDIO.md` | Audio notes |
| `ASSETS.md` | Asset credits and licences |
| `CLAUDE.md` | Developer and agent guide with all debug flags |

## Build and run

Requirements: Unreal Engine 5.9 source build (registered, found via `EngineAssociation`), Git LFS, and Git Bash on Windows.

```bash
git lfs pull
export UE_ROOT=L:/unreal/UE_5.9      # adjust to your engine path
bash tools/gate/verify.sh            # fast tier: compile editor target, run automation tests
FLEET_GATE_TIER=full bash tools/gate/verify.sh   # also cook and launch the packaged exe
```

Open `Epic_AI_Mega_Jam2.uproject` in the editor to play in PIE.

Useful launch flags (game or packaged exe): `-dcdemo`, `-dcseed=N`, `-dcshots=<dir>`, `-dcperf`. See `CLAUDE.md` for the full list.

## Credits

See [`ASSETS.md`](./ASSETS.md) for third-party assets.

## How it was made

Built with **Cat Fleet**, a multi-model agent fleet orchestrated under a human director. The orchestrator plans the work with the human, then headless coding agents implement each step in isolated git worktrees. Each step passes the gate (`tools/gate/verify.sh`) and gets a cross-family review (a different model family from the implementer) before it merges. The human approves every plan and makes every creative call. `DEVLOG.md` is the run record, and the checkpoint reviews it mentions are the fleet's.
