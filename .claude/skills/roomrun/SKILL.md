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
- 15 cm laps are short (median ~5.5 s).
- Layouts are mostly perimeter loops.
- Mid-room furniture is ignored.
- Few gaps or drops.
- Pillars can cross the lower deck at figure-8 crossings.
- Homing chains usually have a single target.

## Integration (later)
The XR host links `libroomrun` and feeds it the traced floor polygon, boxes and player position. The course is injected into SA2 as a stage's geometry/collision table plus a SET object list: springs, dash panels, rings, rails as path data. Derive offsets independently; don't copy them from the CC BY-NC-ND / no-commercial mod sources.
