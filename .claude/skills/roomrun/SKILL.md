---
name: roomrun
description: Work on the Room Run procedural course generator (roomrun/) - build, test, batch-validate over random rooms, render previews, tune modules or physics. Use when changing anything in roomrun/, tuning course generation, Chao Race tracks, scale/speed, or checking that generated courses are fun and always completable.
---

# Room Run generator

The spec is `BRAINSTORM.md`, "Pillar B". The code is `roomrun/`: a C++17 library plus `roomrun-gen` and `roomrun-batch`, with no third-party dependencies.

## Commands
```
roomrun/build.sh                         # build + full test suite in the opensa2-build container
roomrun/run.sh ./build/roomrun-gen --room examples/rooms/living_room.json --seed 7 --out c.json --svg c.svg --obj c.obj
roomrun/run.sh ./build/roomrun-gen --room R.json --sonic-cm 12 --play-obj p.obj   # the playable mesh the game gets
roomrun/run.sh ./build/roomrun-batch --rooms 1000   # the acceptance batch (about 20 s); paths are relative to roomrun/
roomrun/examples/make_examples.sh        # regenerate example SVG/OBJ/JSON and PNGs in examples/png/
```
`nix develop path:roomrun` also works natively.

## Acceptance bar (don't regress)
- **Every room yields a course**, and the trivial fallback is valid by construction.
- **100% completable** over 32 bot runs × 3 laps, and **100% clearance**: no track in the player cylinder (0.7 m), outside the inset room, or through boxes, except deliberate landings on tops.
- **At least 90% without fallback** (currently about 99.6%).
- **Determinism:** same seed + room (snapped to 5 cm) gives byte-identical output. Build with `-ffp-contract=off`.
- Compare against the previous `examples/batch_1000.txt` and report the deltas.

## Look at the output
Render the PNGs and inspect a few yourself. A course should read as a fun Hot Wheels track: furniture tops used, varied modules, rails and springs where they make sense.

## Design facts
- **Sonic scale is 10–15 cm (user decision).**
- **Speed scale 0.6** slows time but keeps arc shapes.
- **Physics constants** are approximate SA2 values (run 5, top 16 u/f, jump 1.66, gravity 0.08). Re-certify the module envelopes against real SA2 through headless OpenSA2 before trusting the guarantee in-game.
- **Module library:** `data/modules.json`. Chao Race uses `data/modules_chao.json`.

## Known weaknesses (update when fixed)
- In a small play area (3 × 2.5 m around the user) the course is a compact figure-8 off to one side, mostly rails (seen in-game 2026-11-21).
- 15 cm laps are short (median ~5.5 s).
- Layouts are mostly perimeter loops.
- Mid-room furniture is ignored.
- Few gaps or drops.
- Pillars can cross the lower deck at figure-8 crossings.
- Homing chains usually have a single target.

## Integration (Room Run v0, built 2026-11-21)
- **In the game now:** `sa2_player_xr` links the library (host/xr/build.sh compiles `roomrun/src` from the `/roomrun` mount that `build-env/run.sh` and `build-env/cross/run.sh` add). See `external/OpenSA2/host/xr/README.md` "Room Run v0".
- **`rr::buildPlayMesh`** (`playmesh.hpp`) is the contract with the game: every station a surface (springs, homing and gaps bridged as ramps; steep rises ramped back), rails a 14-unit strip, walls 24 units high both sides, collision chunks of 32 stations, colour groups for drawing, the centreline. Frame: metres, X = plan x, Y up, Z = -plan y (a proper rotation; triangles CCW from their walkable side). Since round 3 (README "Fixes from the Frame's third test"): the profile smoothed (slope change at most 0.08 a station), slopes limited on the steepest deck facet (0.45 up, 0.8 down), 2 deck lanes, width changes of at most 1 unit a station, walls in pieces of their own, each piece sorted by Z, highest first, at most 7000 collision triangles. Its test checks all of that, plus the deck facing up, walls upright, inside the room, crossing gaps, determinism, and the bots on the play mesh's profile (expert and average 100%; bad no worse than the round-2 profile). The batch prints a play-mesh line (triangles, pieces, steepest facet, largest crease).
- **The game's collision limits for the play mesh:** a polygon pool of 8191 for every piece near Sonic (the whole course in a small room), at most 64 pieces (`SA2_RR_MAX_CHUNKS`), 4096 triangles a piece. Never mix walls and decks in one piece.
- **Input today:** the STAGE play-area rectangle (or `SA2_XR_ROOM_RUN_RECT`), the user at the head's place, Sonic 12 cm. Furniture boxes and the traced polygon come later.
- **Verified offscreen in the real game:** Sonic ran 4.4 laps of a 4.66 m figure-8 with 0 falls (`media/roomrun_v0_*`). The module envelopes are still not certified against real SA2: v0 sidesteps that by making everything a road.
- **Game objects (2026-11-21):** `rr::placeObjects` (`objects.hpp`) plans rings, springs, dash panels, dash ramps, grind-rail paths and optional enemies on the play mesh, and checks each spring / ramp flight with `rr::checkFlight` under the game's measured air physics (gravity 0.08, 1% drag a frame; the dash ramp holds its up-speed 23 frames, no drag): it must stay in the walkway, under decks above, and land ahead; failures are left out (the bridge stays). Rails climbing over 40 units above their start keep the strip only. The tests check it; the batch prints an objects line. The player converts to SET conventions (`host/xr/xr_roomrun.cpp`); `vr-sonic` spawns them. README "Room Run objects".
- **Next for the game:** homing targets on homing chains; a LandTable-style course instead of DynCol pieces if the piece count grows; a smaller dash ramp (the game's is full size); derive offsets independently (never copy them from the CC BY-NC-ND / no-commercial mod sources).
