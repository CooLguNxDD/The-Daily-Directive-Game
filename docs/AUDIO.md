# Audio map (R17)

Every cue is synthesised at start-up from a code recipe in `Source/Epic_AI_Mega_Jam2/Private/DCAudio.cpp`
(`Recipes()`); no audio files ship. This table lists each player-facing action and the cue it plays.
Keep it in step with the code when a cue is added.

| Action | Cue | Where |
|--------|-----|-------|
| Walk / sprint footsteps (by distance), crouch rustle | Footstep | DCCharacter UpdateFootsteps, OnCrouch |
| Jump, landing (volume by impact) | Jump, Land | DCCharacter OnJump, Landed |
| Draw a slot (1 / 2 / 3), raise the camera | Draw | DCCharacter SelectSlot, OnAim |
| Rifle shot, low ammo tick, dry fire, reload | Rifle, LowAmmo, DryFire, Reload | DCCharacter FireRifleOnce, PrimaryRifle, DoReload |
| Hit marker, kill confirm | HitMarker, Kill | DCCharacter ClientHitConfirm |
| Switch stratagem (Q / wheel), strike refused (cost or recharge) | UIClick, DryFire | DCCharacter OnCycle, PrimaryStratagem |
| Strike beacon lands, laser charge, telegraph beeps (faster to the end), detonation (3D + close 2D layer) | Beacon, LaserCharge, Telegraph, Detonation | DCOrbitalStrike BeginPlay, TickTelegraph, DetonateLocal |
| Condemned tower (walker topple, domino) | Creak | DCBuilding Tick |
| Tower collapse | Collapse | DCBuilding SimulateCollapse |
| Viewfinder zoom (lens ring), shutter, film wind-on, out of film | UIClick, Shutter, WindOn, DryFire | DCCharacter OnZoom, TakePhoto |
| Score stamp lands on the photo | Stamp | DCPlayerController ConfirmLocalPhoto |
| Teammate's shutter heard in the world | Shutter | DCCharacter MulticastShutterFx |
| Hurt, downed, revived | Hurt, Down, Revive | DCCharacter ClientHurt, OnRep_Downed |
| Swarmer rushes a raised camera; spotlight sweep; walker stomp | SwarmerAggro, Spotlight, WalkerStomp | DCEnemy behaviours |
| HQ Scoop issued; supply pod falls, lands, picked up | Scoop, PodDrop, Land + Stamp, Pickup | DCGameMode TickScoop, DCSupplyPod, DCCharacter HostResupply |
| Menu and HUD buttons; draft pick, send to press, vote | UIClick, UIConfirm | DCMenu, DCPlayerController Confirm |
| Reel: into a clip, freeze at the shutter, photo stamp | Whoosh, Flash, Stamp | DCHUDScreens DrawReel |
| Front page prints | Press | DCHUDScreens DrawFrontPage |
| Music bed (march loop), all phases | Music | DCPlayerController BeginPlay |

## Listening pass (evidence for "no silent action")
A scripted run records the real master mix and logs every cue it plays:
`-dcdemo -dccueroll -dcaudiorec=<dir> "-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0"`.
`-dccueroll` first plays every cue in the bank alone; then the normal scripted run plays its cues in context.
`python tools/evidence/audio_cues.py <dir>/mix.wav <run.log>` checks each logged cue against the mix: a cue
counts as heard when the loudest 50 ms in the 0.6 s after it is 3 dB over the quietest 50 ms before it, or peaks
above -30 dBFS.

Pitfall: the engine default `UnfocusedVolumeMultiplier=0` mutes a game window without focus, so a background
evidence run records pure silence unless the override above is passed. Players with the game focused are unaffected.

Result 2026-11-13 (run18, seed 777, editor `-game` build of this branch): 471 cue plays over 228 s, 35 distinct
cues, all 35 heard; none silent. Report: `.fleet-task/gallery/audio18_cues.txt`.
