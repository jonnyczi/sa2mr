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
roomrun/run.sh ./build/roomrun-gen --area 2,2,0.10,0.16 --sonic-cm 12 --ppm v.ppm   # a VR play area (W,D,user x,y); 3D preview
roomrun/run.sh ./build/roomrun-gen --room R.json --sonic-cm 12 --mesh-bots           # the bots on the play mesh's profile (as the tests)
roomrun/run.sh ./build/roomrun-batch --rooms 1000   # the acceptance batch (about 1 min); paths are relative to roomrun/
roomrun/run.sh ./build/roomrun-batch --variety 500  # objects and shape for the VR player's 1x1 / 2x2 / 3x2.5 m play areas
roomrun/run.sh ./examples/make_examples.sh          # example JSON/SVG/OBJ + 3D previews (PPM); then on the host:
#   for f in roomrun/examples/png/*.ppm; do magick "$f" "${f%.ppm}.png" && rm "$f"; done   (and magick the SVGs to png/)
```
`nix develop path:roomrun` also works natively. `--debug` prints each candidate, coil/tower trial and stack-solver failure.

## Acceptance bar (don't regress)
- **Every room yields a course**, and the trivial fallback is valid by construction.
- **100% completable** over 32 bot runs × 3 laps, and **100% clearance**: no track in the player cylinder (0.7 m), outside the inset room, or through boxes, except deliberate landings on tops.
- **At least 90% without fallback** (currently about 99.6%).
- **Determinism:** same seed + room (snapped to 5 cm) gives byte-identical output. Build with `-ffp-contract=off`.
- **Stacked decks a full jump apart:** where walkways overlap in plan, deck to deck ≥ `layerGapU` 48 units (jump apex 35 + Sonic 10 + underside 2, measured in the game). The batch's shape line prints the smallest such gap (never below 48).
- **Play mesh within the game's budget:** ≤ 7000 collision triangles, ≤ 64 pieces (every candidate capped at 860 stations / 26 m).
- **The bots on the play mesh:** expert and average complete every run on every course; the bad driver fails ≤ 2% of its runs in aggregate (per course it is a re-roll of its random inputs where it is marginal).
- Compare against the previous `examples/batch_1000.txt` and `examples/variety_500.txt` and report the deltas, the shape line included (lap per floor area, deck heights, tiers, crossings).

## Look at the output
Render the 3D previews (`--ppm`, `examples/png/*_3d.png`: a standing user's corner view and a side view with a 0.2 m scale) and inspect a few yourself. A course should read as a fun Hot Wheels track: furniture tops used, varied modules, rails and springs where they make sense, and in small rooms height and stacked decks.

## Design facts
- **Sonic scale is 10–15 cm (user decision).**
- **Speed scale 0.6** slows time but keeps arc shapes.
- **Physics constants** are approximate SA2 values (run 5, top 16 u/f, jump 1.66, gravity 0.08). Re-certify the module envelopes against real SA2 through headless OpenSA2 before trusting the guarantee in-game.
- **Module library:** `data/modules.json`. Chao Race uses `data/modules_chao.json` (no constant-slope modules, so `GenParams::vertical` is off for it: plain loops only).
- **Height window 0.2–1.6 m** (`minH`, `maxH`; plain loops and the fallback still keep to `prefMinH` 0.4 and up).

## Verticality (2026-11-21): rollers, towers, coils
Small rooms used to get flat 0.4–0.9 m loops. Now (`src/stack.cpp`, `tower.cpp`, `coil.cpp`):
- **Solved profiles.** `solveStackedProfile` sets every station's height by difference constraints (Bellman-Ford, deterministic): each slope within the walkable limit on the inside edge of its bend (`walkRise` 0.45 / `walkFall` 0.8 × `walkBendFactor`; climbs at `coilRiseSafety` 0.82 of it, descents 0.95), the window and furniture tops, the start line level, and every overlapping pair (`Skeleton::partners`) `layerGapU + stackMarginU` apart. Which deck is on top in each over/under region is a depth-first search (the guide's choice first); the profile is the middle of the highest and lowest solutions within a band around the guide. The assembler follows it with **`spiral_ramp` / `spiral_rail`** (constant slope, `params.ease 0`) and keeps every ramp/rail walkable on the inside of its bends (`walkFactor`); spring and homing bridges too.
- **Roller profiles** (`rollerProfile`): most perimeter / figure-8 / circle candidates (80% in small rooms) get low and high plateaus, a climb shaped by each station's limit and a steeper descent, as tall as the climb allows (a small one centred at 0.9 m); a figure-8's crossing sits on the plateaus, a jump apart.
- **Towers** (`towerCandidates`, kind `tower`): a loop (perimeter, figure-8, circle, or the user's ring) with 1–3 spiral towers hung off it, one or two turns down beside the loop and back onto it a gap lower; the loop climbs back. A tower hugs its loop near the tangent point, so it needs r ≳ 0.3 m at 12 cm (`towerFall`); the start line sits on the level top before the first tower (`Skeleton::keepStart`).
- **Coils** (`coilCandidates`, kind `coil`): chains of 2–3 circular lobes in free disks joined by tangent lines (crossing where neighbours turn opposite ways), climbing on some lobes and descending on others, a lobe wound over itself where a turn can make the gap.
- **Selection:** candidates score `4 × height range + 6 × stacked share` (plus the stacked-kind bonus) on top of the old score; seeds vary the lobes, towers, radii, turns and hills.
- **Play mesh:** a deck held over another is lifted back to its gap by a smooth cosine bump (±12 stations), not station by station (that made creases of 0.4 a station); pillars skip any lower deck's walkway. **Objects:** `checkFlight` takes the highest deck under the flier where decks stack; up to 2 **climb springs** a course launch up a climb onto a higher deck (`ObjectStats::climbSprings`, a new field).
- **Sim:** a respawn restarts the "stuck" progress window (retries are the net-loop limit's business), so a bot that misses a homing chain four times is no longer called stuck.
- **Numbers** (`examples/batch_1000.txt`, `variety_500.txt`; before → after, 12 cm play areas): 1×1 m: tiers ≥ 2 4% → 18%, deck max p50 0.88 → 1.13 m, range p50 0.30 → 0.52 m, lap/area p50 1.18 → 1.29; 2×2 m: stacked 7% → 66%, lap/area 1.27 → 1.71, range 0.30 → 0.74 m; 3×2.5 m: stacked 24% → 91%, range 0.39 → 0.76 m. 1000 rooms, 10 cm: stacked 9% → 89%, range p50 0.40 → 1.11 m.

## Known weaknesses (update when fixed)
- **1 × 1 m play area (2 × 2 m course, user near the middle):** stacks in only ~18% of user spots. The free floor is a C or a ring of 0.4–0.6 m arms, and stacking needs a full 0.62 m (52 units) climb on each side of an over/under spot; most get an unstacked roller loop of ~4.3–5 m, 0.6–1.1 m high. Narrow-strip partner radii (rails on tight towers) would help a little.
- **Generation time** rose: p50 0.55 s, p95 1.9 s, max 3.6 s at 10 cm in the random rooms (was 0.2 / 0.7 / 1.1 s); 12 cm play areas 15–100 ms. On the Frame, measure it (it runs on a worker thread).
- Coils win rarely (towers do most of the stacking); `hallway_helix` no longer makes a helix (towers outscore it).
- 15 cm laps are short (median ~6 s).
- Mid-room furniture is ignored.
- Few gaps or drops (drops need straights; small rooms have none).
- Homing chains usually have a single target.
- Courses are mostly ramps and rail strips on the climbs (`spiral_ramp`, `spiral_rail`); flat road for loops, dash panels and enemies is scarcer.

## Integration (Room Run v0, built 2026-11-21)
- **In the game now:** `sa2_player_xr` links the library (host/xr/build.sh compiles `roomrun/src` from the `/roomrun` mount that `build-env/run.sh` and `build-env/cross/run.sh` add). See `external/OpenSA2/host/xr/README.md` "Room Run v0".
- **`rr::buildPlayMesh`** (`playmesh.hpp`) is the contract with the game: every station a surface (springs, homing and gaps bridged as ramps; steep rises ramped back), rails a 14-unit strip, walls 24 units high both sides, collision chunks of 32 stations, colour groups for drawing, the centreline. Frame: metres, X = plan x, Y up, Z = -plan y (a proper rotation; triangles CCW from their walkable side). Since round 3 (README "Fixes from the Frame's third test"): the profile smoothed (slope change at most 0.08 a station), slopes limited on the steepest deck facet (0.45 up, 0.8 down), 2 deck lanes, width changes of at most 1 unit a station, walls in pieces of their own, each piece sorted by Z, highest first, at most 7000 collision triangles. Its test checks all of that, plus the deck facing up, walls upright, inside the room, crossing gaps, determinism, and the bots on the play mesh's profile (expert and average 100%; bad no worse than the round-2 profile). The batch prints a play-mesh line (triangles, pieces, steepest facet, largest crease).
- **The game's collision limits for the play mesh:** a polygon pool of 8191 for every piece near Sonic (the whole course in a small room), at most 64 pieces (`SA2_RR_MAX_CHUNKS`), 4096 triangles a piece. Never mix walls and decks in one piece.
- **Input today:** the STAGE play-area rectangle (or `SA2_XR_ROOM_RUN_RECT`), the user at the head's place, Sonic 12 cm. Furniture boxes and the traced polygon come later.
- **Verified offscreen in the real game:** Sonic ran 4.4 laps of a 4.66 m figure-8 with 0 falls (`media/roomrun_v0_*`). The module envelopes are still not certified against real SA2: v0 sidesteps that by making everything a road.
- **Game objects (2026-11-21):** `rr::placeObjects` (`objects.hpp`) plans rings, springs, dash panels, dash ramps, grind-rail paths and optional enemies on the play mesh, and checks each spring / ramp flight with `rr::checkFlight` under the game's measured air physics (gravity 0.08, 1% drag a frame; the dash ramp holds its up-speed 23 frames, no drag): it must stay in the walkway, under decks above, and land ahead; failures are left out (the bridge stays). Rails climbing over 40 units above their start keep the strip only. The tests check it; the batch prints an objects line. The player converts to SET conventions (`host/xr/xr_roomrun.cpp`); `vr-sonic` spawns them. README "Room Run objects".
- **Round 2 (2026-11-21):** `ensureVariety` adds a hop spring / dash panel where modules have none; enemies on (0.25 a metre, Beetles only, 20 units up); grind paths 7 units up (the game grinds within 4 units: jump on), joined across 6-station breaks and over road to 35% of the course, drawn by `grindRailGroup`. `roomrun-batch --variety N` reports the share of courses with each object for the 1×1, 2×2 and 3×2.5 m play areas (`examples/variety_500.txt`). A rail-share cost in the assembler is off (small rooms need the rails for their bends).
- **Verticality round (2026-11-21, generator `roomrun-0.2.0`):** no OpenSA2 change needed. `buildPlayMesh` / `placeObjects` and the course JSON keep their shapes; new skeleton kinds `tower` and `coil`, new module ids `spiral_ramp` / `spiral_rail` (kinds ramp / rail, so the play mesh's colour groups are unchanged), `ObjectStats::climbSprings` added (source-compatible). The version is hashed into the course seed, so `SA2_XR_ROOM_RUN_SEED` 1 now gives a different (taller) course. Needs the headset: the feel of long 0.3-slope spiral climbs and 0.5-slope descents at 12 cm, decks up to 1.6 m around a standing user, and towers within arm's reach.
- **Next for the game:** homing targets on homing chains; a LandTable-style course instead of DynCol pieces if the piece count grows; a smaller dash ramp (the game's is full size); derive offsets independently (never copy them from the CC BY-NC-ND / no-commercial mod sources).
