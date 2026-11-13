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

## Problems met
- The engine is shared with an editor that has Live Coding on: UnrealBuildTool refuses to build. Fixed with
  `-NoHotReloadFromIDE`, and the packaging step now builds the game target itself.
- The gate script had a comment with a stray newline that broke tier B; fixed.
