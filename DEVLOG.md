# DEVLOG: Director's Cut: Front Page

Epic AI MegaJam 2026, theme DIRECTOR'S CUT. Built by an AI lead agent (Cat Fleet) under a human director; this log
is kept from the run records (Glass Box modifier): what was decided, why, and what broke.

## 2026-11-12/13: Pitch and pillars (human)
- Co-op photo roguelike shooter: call an orbital strike, run toward it, photograph the collapse at peak bloom.
  Photos pay for bigger bombs; the run ends with a re-staged highlight reel, a final cut, a vote and a war-newspaper
  front page.
- Pillars: the shot is the weapon; every blast is a photo opportunity; risk for the frame; the reel is the reward.

## 2026-11-13: Foundation
- Unreal 5.9 C++ module; all rules, scoring and tuning in C++ and `Config/DefaultGame.ini`, so reviewers can read them.
  Blueprints are not used at all.
- The city is built in code: towers are instanced cube blocks from a seeded plan. A collapse is decided by the host
  and replicated as a time and an epicentre; each machine simulates its own debris (pooled Chaos rigid bodies).
  Geometry Collections were skipped: fracture assets need editor tooling, and a cube kit reads the same on screen.
- Photo grading is rule-based on the 3D scene at the shutter: blast timing windows (Overexposed 1.5x at 0.0 s,
  Peak Cinema 3.0x at 0.2-0.8 s), centring and occlusion, collapses and debris in frame, enemies, distance to danger.
  No pixels are analysed.
- All audio is synthesised from code recipes at start-up.

## 2026-11-13: The run
- One phase table drives the run: lobby, three sectors with drafts between them, reel, final cut, vote, front page.
- Draft: three pools (Cinema Stratagems, Camera Gear, City Mutators), three options each, effects by id in a registry.
- HQ Scoops: timed photo challenges; the first photo that meets one drops a supply pod.
- Reel: the host records each player's pose at 12 Hz and keeps two seconds per photo. At the end every machine
  rebuilds the sector from the run seed, replays the recorded collapses and blasts on a remapped clock and films a
  mannequin stand-in in slow motion, then freezes, flashes white and cuts to the real photo.
- Photos travel as JPEG in 16 KB chunks through the host, so every player sees every pick in the vote and on the
  front page.

## 2026-11-13: First checkpoint and after
- Checkpoint 1 (cross-family review) merged green with one major: a solo player who went down failed the run at
  once, so the self-revive could never finish. Now a downed solo player has 12 seconds to hold E.
- HQ propaganda quota: each sector has a photo-score target on the HUD; missing it costs the draft. Playing safe
  with the rifle now loses upgrades, which is the first pillar ("the shot is the weapon").
- The draft shows all nine cards (three pools, three options each).
- One collapse scheduler (advice from the Fable advisor): strikes, walker topples and Domino chains all write a
  telegraphed event record. A condemned tower shakes, creaks and flashes red until it gives way, so every collapse
  has a photo timing window, and the reel replays them from the same records.
- Upgrade and mutator effects became data rows (multipliers, light and fog, stun, domino reach, rogue strikes).

## 2026-11-13: Checkpoints 2 to 5
- Enemies: swarmers that rush whoever raises a camera, spotlight units that blow out a photo's exposure (BLOWN OUT
  0.75x), and walkers that march on the nearest tower and topple it into a telegraphed collapse.
- Revive: a teammate holds E on a downed player, who lies in the mannequin fall pose until helped.
- Vote: one vote per player for someone else's shot, 600 points each; a vote can lift the runner-up to the headline.
- Draw: changing slot plays a mannequin motion over the draw time; the camera has its own two-handed raise.
- Second take (stretch): while a reel clip replays, a player raises a camera from a free angle and snaps once per
  clip. The host grades it on its own re-staged scene at the replay clock, and it joins the final cut at its critic
  rank (checkpoint 5 found it appended at the bottom, so the timeout pick skipped it).
- LAN: a listen host announces itself on the local network; the title menu lists the games it hears with a JOIN
  button, beside joining by IP.
- Evidence tooling: scripted pilot runs for each draft option, a rifle-only sector, and a listening pass that
  records the master mix and checks every cue played is audible (`tools/evidence/audio_cues.py`).

## 2026-11-13: Pressure (after checkpoint 7)
- Swarm pressure follows HQ's quota: a pure rule turns how far the crew lags the photo quota into a target number
  of swarmers and a pack size; photo points lower it, kills never do. Packs of 4-8 land on a street ring around a
  player; a collapse or a downed player buys a few seconds of relief; a hard cap per sector stops a death spiral.
  A rifle-only player is now overrun in sector 2; a crew that keeps filing photos faces the smallest packs.
- The gate's full tier now launches the packaged exe (-dcsmoke: start a run, drop a laser on a tower, check the
  collapse) instead of only packaging it.

## 2026-11-13/14: Director's Cut polish (voxel engine, neon noir, real physics)
- A small C++ voxel engine: chunked grids with the voxel size as data per object (10 cm towers, 50 cm ground and
  rubble, 5 cm props and characters), greedy meshing on worker threads into procedural mesh cells uploaded under a
  millisecond budget, one palette table in ini.
- The city is crafted from data: building recipes, district rules, a seeded PCG rule table (weights, ranges,
  adjacency, symmetry, palette slots) and a city builder that also runs as a commandlet writing plan and aerial images.
- Strikes are host ops replayed on every machine: carve, bite, structure pass (the weakest layer breaks and the top
  topples away from the blast), rubble settled into the terrain grid; op hashes match on host and client. Ops are
  planned 1.2 s ahead and committed in the detonation frame.
- voxel-forge (repo skill + generator) turns short text recipes into the rifle, press camera, outfits and props.
- Weather is a pure forecast from seed, sector and server clock; post layers stack by priority and weight; photos get
  a CPU print treatment on the readback (blendables do not run in one-shot captures).
- Co-star, Last Frame and the Full edition front page; a voxel character builder with share codes; versioned saves.
- Art direction moved to restrained neon noir on 2026-11-13 ~23:30: night sky, slate voxels, warm dim windows, rare
  magenta and cyan signs whose pooled rect lights pool on the wet street, dark-glass UI.
- Real voxel physics: the biggest pieces of each strike fall as one and break into Chaos box bodies at the impact,
  then swap to the host op's rubble when they rest.

## 2026-11-14 (night): true voxel break-up, host-owned rubble, neon noir sweep
- Strikes now shatter: carved voxels fly as shard bodies (single voxels near the blast, small clusters further out),
  the toppling top breaks into chunk bodies mid-fall and a hard hit shatters a chunk into leaf bodies. Bodies come from
  a pool prewarmed at sector build and freeze one by one.
- The host owns where the rubble lies: its bodies' rest poses go out as a replicated Rest op and every machine
  re-settles that piece's rubble there; host and client op hashes match on every strike and rest.
- A look-sweep helper ran in its own worktree: night building form (palette albedo floor, low moon, violet sky fill),
  dark-glass UI everywhere, holographic stamp, feed timestamp and chroma split, a bloom threshold, a stop-down at the
  peak and a rim blast light. Signs got a neon tube edge; rubble piles burn with a flickering firelight.
- Frame time: shard clustering and fragment trees moved to the op worker; R13 on the packaged build with 4 players:
  99 fps steady, 83 fps average during collapses.
- The packaged solo run now goes menu to a real front page through all three sectors in about 13 minutes.

## Problems met
- Git Bash rewrote `/Game/...` map arguments into `C:/Program Files/Git/Game/...`; `export MSYS_NO_PATHCONV=1` fixed it,
  but the same export broke `taskkill //F` in the co-op script and left a fleet job hanging.
- A pile of touching physics bodies is one Chaos island and never sleeps as a group; bodies now freeze one by one.
- The engine is shared with an editor that has Live Coding on: UnrealBuildTool refuses to build. Fixed with
  `-NoHotReloadFromIDE`, and the packaging step now builds the game target itself.
- The gate script had a comment with a stray newline that broke tier B; fixed.
- The ground slab was Static, so moving it down at run time was refused: it sat 50 cm high and hid the streets and
  the tower bases. Found only when the new street lines did not show.
- Photos at the bloom peak were pure white: the capture inherited the flash's exposure kick. Photos now keep a
  fraction of the bloom and stop down while the flash is on screen.
- A pointer into a temporary list made the Domino chain fire with a 0.1 s warning instead of 1.2 s.
- Per-file colour constants clashed in unity builds and broke packaging; one palette header now owns them.
- A unity build failed on a local named like a file-scope constant (`Rate`); the gate's clean build caught it.
- Evidence frames after a network travel overwrote the earlier ones (the new controller restarts its count); frame
  names now carry a time stamp.
- The DRAMA tag on a pending strike sat at a fixed 9 m and fell off the bottom of frames aimed up a tower; it now
  rides the laser column where the view ray passes it.
- Every rifle-only test showed swarmers idling 40-60 m away. A per-enemy trace showed them frozen against towers:
  an inside corner or a window ledge blocked the one escape move they had. They now try several and take the first
  that moves.

## 2026-11-14 directors-cut-final (lead)
- Collapses throw single-voxel crumbs (one instanced cube per colour bucket, ballistic, local only); the host Rest op
  still owns the rubble that stays. Fragment and body budgets raised as data so tops break into more, smaller chunks.
- Factions: each map rolls a main faction and two invaders from the run seed; a friendly army acts on its own. Ground
  infantry walks on voxel column tops (no physics sweeps), retargets at about 4 Hz, and fires tracers. Faction
  destruction goes through the same strike path as the crew's (rate-limited by the host), so it carves, replicates and
  grades like any blast. Two factions in one frame earn a CLASH.
- Objectives: Retrieve (a package deep in the main faction's ground) or Destroy (three marked towers), then a lit
  beacon; reaching it ends the sector early. Time up fails the objective but the run goes on to the reel.
- The builder exports a shareable ID card PNG from its own turntable capture.
