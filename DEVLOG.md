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

## Problems met
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
