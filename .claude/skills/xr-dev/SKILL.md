---
name: xr-dev
description: Develop the OpenXR (VR/MR) player for SA2 - per-eye camera substitution, diorama/toy-scale anchoring, HUD layers, passthrough, controller bindings, frame pacing - and test it in Monado with the Steam Frame mimic layer. Use for any change under external/OpenSA2/host/xr or its hooks in host/d3d9gpu, host/player, host/launch.
---

# XR player development

Read `external/OpenSA2/host/xr/README.md` first. It holds the measured camera findings, the design, the settings table, and the list of upstream hook files.

## Key facts (verified)
- **The camera lives in shader constants, not SetTransform.**
  - `g_WorldMatrix` c0–c3 is the model-view; `g_ProjMatrix` c8–c11 is an infinite-far projection, near 1, depth 0..1. Both are right-handed with −Z forward, the same as OpenXR.
  - There is no separate view matrix; the camera is folded into each draw. Identity-MV draws are CPU-skinned models (Sonic) already in view space, not the sky.
  - **The true camera comes from guest memory.** `SA2_REC_CAMERA=1` makes the game-side recorder (`host/sa2_camera_rec.h`) emit a `SA2S_CAMERA` record (stream version 3) after each BeginScene, holding:
    - `CameraData[0]` at `0x1DCFF40`; its view pos/dir/roll/fov at `+0x16C`;
    - player 1's `EntityData1` (via the pointer at `0x1DEA6C0`), position at `+0x14`.
  - The camera record is verified by rendering: Sonic's view-space mesh lands on the road at the player position, and the world stays consistent.
  - `V = lookAt(pos, dir)` with world +Y up; roll was 0 in City Escape.
  - Five vertex shaders. `67664a3329` (`g_DeviceInfo`) is all 2D/HUD.
- **Stereo:** "world" draws are re-issued per eye with MV′ = X·MV and the eye's projection (x/y rows from XR FOV, game z/w rows kept). Normals and lights get the same rotation. The HUD goes to a quad layer above the projection layer.
- **Vulkan access:** SDL 3.4.16's GPU API has no XR hooks, so the shim `libsa2_xr_vk.so` routes `vkCreateInstance`/`vkCreateDevice` through `XR_KHR_vulkan_enable2`. Reading SDL's private texture structs is tied to the pinned SDL version, with a runtime check.

## Rules
- **Portability:** pure Vulkan plus core OpenXR. Gate anything Frame-only behind capability checks: the `XR_VALVE_frame_controller_interaction` binding variants, the Turnip single queue (`tu_emulate_second_queue` or a mutex), and refresh-rate handling. The Quest APK port will reuse this code.
- **The flat player must not change behaviour.** Rebuild and run the d3d9gpu tests after touching the hooks.
- **Never let the XR loop back-pressure the game.** The stream runs at the game's 60 Hz while XR presents the newest frame at the runtime rate (Frame: 72–144 Hz), with prediction and reprojection.
- **Passthrough:** the stock Frame passthrough is grayscale and must be enough; color is optional. It requires alpha-correct world draws and a clear to alpha 0. Project layer first (SteamVR reprojects only layer 0).
- **Toy scale:** Sonic is 10–15 cm (`SA2_XR_SCALE` ≈ 0.010–0.015 m/unit).
- **Stream contract:** new kinds go in a new version, written only when asked, so default streams stay byte-identical. `host/player` checks audio by `header().has_audio`, not by version.

## Test loop
1. Build: `build-env/run.sh 'source tools/env.sh; bash host/xr/build.sh'`
2. `build-env/monado.sh &` (or `MONADO_NULL=1` headless), then `build-env/play-xr.sh` with the Frame-mimic layer variables (see the `run-and-measure` skill).
3. Evidence: `xr:` lines in `player.err`, `build-env/pace-stats.sh`, and eye dumps (`SA2_XR_SHOT`) saved to `media/`. Inspect the images yourself.

## Lessons learned
- **Identity `g_WorldMatrix` draws are *not* only the sky.** Sonic and the other CPU-skinned models are drawn already in view space with an identity MV. Every 3D draw must get the full eye transform. `SA2_XR_DROP_VIEWSPACE=1` is the diagnostic that hides them.
- **Diorama:** room-locked means `X_eye = (1/s) * eye_from_room * A * S * T(-sonic) * V^-1` applied to the game's MV. The table is a treadmill that follows Sonic; the axes are the game world's.
  - **Clip box:** D3D9 user clip planes. Every SA2 program has `clip_planes@2` and `state@3`; the count is state word 0, bits 16–23, read at run time. Planes are in clip space: `P^-T * view-space plane`.
  - **Synthetic head:** `SA2_XR_SIM_HEAD` gives side, top and rear eye dumps without a headset.
  - **Watch out:** a steep street plus the box's depth limit makes the visible region look off-centre. That's the slope, not a bug.
- **Chao Garden diorama:** `SA2_TWEAKS=skip-intro,start-in-chao-world,vr-chao-garden` with a NEW FILE.
  - Press Enter twice to get there: the scratchpad `enter.py` writes key records into the session's `input.fifo`.
  - Test profile: point `XDG_CONFIG_HOME` at a copy of `sa2recomp.cfg` with its own `overlay=`, so the user's saves are never touched. Moving saves aside is not allowed.
  - Garden mode is static: 0.004 m/unit (was 0.0022: Chao too small to reach), box 318 units. The grips move/turn/scale it (`place_`), kept in `$XDG_CONFIG_HOME/sa2recomp/xr_garden_place.txt`.
- **Hands:** XR thread → `host/sa2_vr_hands.h` mmap file (`SA2_VR_HANDS`, launcher, XR only) → game tweak `vr-chao-hands` (acts in garden-ring-trickle's wrap of `ALW_Control_Main`). Object hold = status `0x8000` on the item, never Sonic's hold slot; gentle/throw by calling `Chao_Behaviour(0x565BE0 / 0x564320)` directly; pet = `Chao_Behaviour(0x5648A0)` (no reward right after holding: the game's `+0x132` rule). Test: `SA2_VR_HANDS_SCRIPT` in game.env (spawnchao, spawn, hand, move, follow, stroke, grip, hunger, info) and `SA2_XR_TEST_HANDS` (`p` = player-relative, through the real transform).
- **The drawn view is the Ninja matrix stack base**, not the view struct. `CameraApplyView` (`0x0077E0C0`) builds it during display; the camera record carries it (regions 6/7, emitted after BeginScene and before the first draw), and `SA2_XR_VIEW=8` (default) uses it. Measure with `SA2_XR_STILL=1`: level pieces' `V^-1·MV` must stay at 0.000. The view struct's lookAt taken at Present was a frame off and made the island shift as Sonic walked.
- **Only draw with views that are views.** `SA2_XR_VIEW` accepts 0 or 8; candidates 1–7 are measurements (`SA2_XR_VIEW_ANY=1`). A test window with a wrong candidate (the transposed base) showed stretched, exploding polygons, and the user took it for a broken game. Test windows: short, and correct views only.
- **Steering:** `SA2_XR_STEER=head` (diorama default) turns the stick by head-forward (floor, through the table transform) vs the camera's yaw, per display frame. Test without controllers: `SA2_XR_TEST_STICK=x,y,secs` + `SA2_XR_STEER_LOG=1` under Monado (the pad reaches the game; `SA2_INPUT_PAD_LOG` in game.env shows the records).
- **Chao World objects** have their own camera test `0x0054AC70` (14 display functions) plus Ninja's model clip (`0x0042B400`, `0x0042F640`); `vr-object-culling` answers "in view" in Chao World. The generic object clipper `0x00488C80` is not what hides them. `SA2_XR_DRAWLOG=1` logs draw counts every 60 frames.
- **Swim with head motion = a stale pose.** Draw each frame for the display time it is *shown* at (pose lead, measured from first showings), and submit exactly the pose it was drawn with. Runtimes reproject rotation only, so staleness shows as swim under head *translation* on a near table. Measure with `SA2_XR_TEST_MOTION=0.1,0.5` ("pose age …" lines every 600 layers): lead off 33 ms / 6.7 mm, on 0.1 ms / 0.01 mm.
- **Gloves = Sonic's own hands** (`xr_sonic_hands.cpp`):
  - **Since 2026-11-21: the cutscene hands** (event e0000, 449 vertices, authored fist/relaxed/open poses blended by the squeeze), hash-checked; 0x2a79c is the RIGHT hand. **Lesson:** a geometric thumb test (the widest side near the cuff) found the little finger and swapped the hands on the Frame. Judge handedness in a real XR dump of a test hand in the handshake pose (palm left, fingers forward, thumb up), not from small offline renders. A in the tuning mode swaps the meshes.
  - `sonicmdl.prs` is PRS-decompressed and decoded in memory (big-endian table and chunks).
  - The model files hold ONE relaxed hand: 16, and 18 is the same mesh on the arm chain. Open and fist are made by bending its fingers (`curl`).
  - The fit is measured in the hand node's frame, and a load check logs the triangles and finger length per state.
  - Never write the mesh anywhere. The procedural fallback is in `xr_gloves.cpp`.
  - **Verify gloves offscreen before any visible window:**
    - `private/sa2mr/xrtools/glove_dump.cpp` with a scratch renderer;
    - or a second Monado with `XRT_COMPOSITOR_NULL=1` on a short `XDG_RUNTIME_DIR` (the socket path limit is 108 characters), plus `--video-driver offscreen`.
  - A test eye on the forearm axis hides the hand behind Sonic's big cuff.
- **Drawing:** our own SDL_gpu pass via `Gpu::external_pass`, after the world, with the world's P and depth (`last_P_`, `last_ds_`).
  - SPIR-V comes from `host/xr/shaders/gen_spv.sh`. glslangValidator is in `private/s5-tools/install/bin`, not on PATH; the header is committed for the cross build.
- **Chao facing:** a Chao's model faces (sin a, 0, cos a) at Ninja yaw a, so yaw = atan2(dx, dz). The first try was checked only from behind and was mirrored sideways. Always check facing from the left, right and front.
- **Petting needs the kind-2 sphere.** Calling `Chao_Behaviour(0x5648A0)` directly ends at once, because its loop needs the sphere, and gives no reward right after a hold. Use a `ChaonadeExec` per hand: owner byte 6/7, its main wrapped to sit at the hand.
- **Test runs:** `run_garden.sh` runs each test in its own session, then kills by process name (`pgrep -x`) and start time.
  - `pgrep -f` matched the caller's own shell.
  - The game and the player may live in steam-run's own session.
  - Check `pgrep -x sa2_player_xr` after each run: an orphaned player window confused the user.
- **Test hands that follow the game's world (`p`/`c` frames) must never hold the world:** it chases itself and the world flies off. The test code forbids it.
- **Draw marks:** stream version 4 `SA2S_DRAW_MARK` (`host/sa2_drawmark_rec.h`) brackets the draws one piece of the game makes. A tweak calls `sa2_rec_draw_mark(1|0, tag, object)` around a display function, and the player gets `sa2xr_draw_mark` in stream order.
  - In use for the Chao status bubble (ParamFukidasi, display `0x565200`, 118 draws), held objects (`SA2S_MARK_HELD`) and the room Chao (`SA2S_MARK_CHAO`).
- **Chao in Your Room:**
  - The room Chao lives in a game-side pen (`SA2_VR_PEN`). It must be **dry**: below the water level (-2) the Chao swims every frame and does nothing else.
  - Its room circle must stay wholly outside the garden box, or the garden's water hides it.
  - Test with the `r` hand frame, `SA2_XR_TEST_HEAD_PITCH`, and the game script's `tp:X:Z` / `info` (which prints the water level).
  - Never hold `xs_mu_` while calling `head_room()` or anything else that locks it: that deadlocked the XR thread.
  - The game's 2D-looking Chao panels are 3D sprites in front of its camera (identity MV, world P); without marks they are indistinguishable from Sonic's skinned draws.
- **Held objects at hand scale:** `SA2S_MARK_HELD` marks (anchor = centre and radius) → MV' = V·E·V⁻¹·MV, one k for all (a Chao 18 cm), eased 0.2 s (advance once per frame: a Chao is marked twice a frame).
  - Test with two test Chao (`c` = last spawned, `d` = the one before), at least 40 units apart (the reach is 20).
  - The XR test clock starts about 8 s before the game script's: start the hand steps at 10 s or later.
- **Passthrough:** clear alpha to 0, give world draws a coverage blend, and set the layer's `SOURCE_ALPHA` bit (premultiplied).
- **Chao draws:** view-space (identity `g_WorldMatrix`) draws with their own texture groups, after the land.
  - To relocate one, multiply its MV by `V·R·V⁻¹` in view space.
- **Pacing:** an XR thread runs at the runtime rate and the player thread at the game rate, with hand-over of the newest frame and pose. One **recursive** `queue_lock()` guards SDL's single queue, because Monado's `xrAcquireSwapchainImage` submits through our wrapper. The remaining dips come from CPU cost: about 3200 doubled draws in heavy sections. Fix that with instanced or multiview stereo.

- **2D window, Sonic out, first person (`vr-sonic`, 2026-11-21).** README "2D window, Sonic out of the window, first person".
  - **Window:** draws into the back buffer are redirected unedited into the HUD's texture, which becomes an opaque quad, under the projection layer by default. The eyes hold only the gloves and Sonic.
  - **Tests:** `SA2_XR_WINDOW=1` starts in the window. Window test-hand frames: `h` head, `n` window centre, `w` Sonic's spot on the window, `s` Sonic in the room. `SA2_XR_TEST_PAD` takes several `MASK:FROM:TO` steps.
  - **Eye dumps:** `_window.png` composites the quad.
  - **Pause-menu timing:** steps fail when the game runs slower than real time, because shots slow it. Leave at least 3 s between menu steps, or use `SA2_XR_WINDOW=1`.
  - **Held Sonic's facing:** `SA2_XR_SONIC_FACE_OFF` is 180, measured. Judge from the side and the front.
  - **Don't hold the game camera far from Sonic** while his draws are relocated: his weighted legs stretch. The camera follows him onto the proxy floor.
  - **Script:** the scratchpad's `run_vs.sh` and `grab_w.sh`, under `monado_null.sh`, a private x9 Monado. Never `pkill monado-service`: the user's own Monado may be running.
- **Room Run v0 (2026-11-21).** README "Room Run v0". `SA2_VR_ROOM_RUN=1` in game.env (the game publishes `room_run_on`, channel v8) or `SA2_XR_ROOM_RUN=1`; the drop out of the window becomes command 4 onto the course.
  - Course frame: metres, Y up; room = A + R_yaw p, game = F + p / m (F from the game's `sonic_floor`). The generator runs on a worker thread at the window's first frame; the file `<SA2_VR_HANDS>.course` (`host/sa2_roomrun_course.h`) carries the collision to `vr-sonic`.
  - Drawing: `Gloves::set_course` (its own vertex buffer), GloveDraw `course` = the colour group, drawn first (opaque, depth written) so shadows land on the deck.
  - Track steering replaces the head's forward in `steer()` with the direction to the centreline 6 stations ahead.
  - **Test:** scratchpad `rr_w.sh` (window grab + `SA2_VR_ROOM_RUN=1` + stick 0,1). Evidence lines: `room run: Sonic at station i of N ... above its deck`, `completed lap`. **The default test head (0,-1.15,0) is below the decks (0.4–1.4 m) and sees only pillars**: use `HEAD=-0.6,0.7,2.6 PITCH=-32` for an overview.

- **Teardown order (2026-11-21).** Release every SDL object the XR player owns (`gloves_.release()`) before `delete in_` in `~XrGpu`; members are destroyed after the body, into a freed SDL device. That was the Frame's "crash on pause" (`malloc(): unsorted double linked list corrupted`): the game had ended, and the player died in teardown after writing a complete report. ASan misses it (the bad writes are inside uninstrumented SDL), and test scripts that kill the session never reach teardown. Test exits with `run_end.sh` (scratchpad: `ENDGAME_AT=N` sends SIGTERM to `sa2-game`) and read `"player": {"exit": ...}` in `launch.runrec`: it must be 0.

- **Frame mode fixes (2026-11-21).**
  - **The stick:** the game turns it by the camera location's angle word **E4** (`0x01DD00E4`, = atan2(dir) + 180°). A camera that writes only the position and direction mis-steers.
  - **Remaps only in play:** remap the stick only in play (`game_state` 16), never in menus.
  - **Recentre on mode changes:** the seated view's forward is the reference.
  - **Window default `eyes`:** SteamVR put the window's quad over the projection layer whatever the order.
  - **Restart:** it **reuses the player task's address** and keeps the dynamic collision list. Reset on GameState, not on the task.
  - **Test tools:** `SA2_XR_TEST_STICKS`, `SA2_XR_TEST_HEAD_FROM` (a head turn after the recentre), `SA2_XR_TEST_STAGE_BOUNDS`, `SA2_VR_POS_LOG`.
  - **Scratchpad scripts:** `restart_test.sh` (toggles, Restart, stick).
  - **Open ground for path tests:** Metal Harbor (`STAGE=10`); City Escape's board section weaves by itself.

- **Frame mode fixes, round 2 (2026-11-21).**
  - **Frame order:** the game camera runs before Sonic in a frame. A first-person eye must be moved after Sonic_Main, through the view struct at `0x01DD00AC`.
  - **Weighted skinning stretches with camera distance.** Draw his display from a shifted view (`SA2S_MARK_SONIC_VIEW`) when the camera is far.
  - **Riders:** a player standing on a dynamic collision object is carried with it, so remove the object a few frames before teleporting him.
  - **Room Run decks need an underside** (a ceiling); a one-sided deck lets him through from below.
  - **Test hooks:** `SA2_XR_TEST_RSTICKS`, `SA2_XR_TEST_ROOM_RUN_USER` (0.02,0.05 with seed 3 gives a figure-8 with a bridge), `SA2_VR_POS_LOG=N`.
  - **Never run two `run_vs.sh` at once:** they share and restore the release's `game.env`.

- **Frame mode fixes, round 3 (2026-11-21).** README "Fixes from the Frame's third test".
  - **Judge a put-back by Sonic AND the camera.** The Frame's "he falls into the void" was the game camera under the street; Sonic stood on it. The player's `game camera record` lines (`player at`) tell them apart. The camera carries itself by his teleports: hold its location and work (`0x01DCFF0C`) for 3 updates after any teleport.
  - **Game collision: never put walls in a deck's dynamic-collision piece.** On a slope, touching a wall in the same piece sank him 0.5 units into the deck without ground. Each piece's triangles go highest Z first (the cache's band chains).
  - **Test at the Frame's spots:** `SA2_VR_TEST_TP=F:x,y,z`; `SA2_VR_POS_LOG` logs collision, cache, pool and camera. Scratchpad `pb_w.sh` (take out, room, run, put back; `WY` moves the drop point on the window, `STEPS` stick steps).
  - **Ramp tests:** scratchpad `rr_ramp.sh` (the Frame's course, the hand drops him on station `ST` via the `k` frame, track steering forward then back) and `course_ramp.py <media dir>` (his height against the deck every 2 frames, passes, monotonic or not, samples without ground). Diagnostics `SA2_XR_ROOM_RUN_WALLS_APART=0`, `SA2_XR_ROOM_RUN_SHAPE=v0`.

- **Room Run objects (2026-11-21).** README "Room Run objects"; `docs/vr-roomrun-objects.md` section 8.
  - The game's own objects are spawned by `vr-sonic` from the course file (version 2) and drawn into the room by marks (`SA2S_MARK_RR_OBJECT` 6): their display slots point at **trampolines** (natives at our own arena addresses in the function table), which shift the view next to the object (60 units ahead) so the game's in-front tests pass with the camera held at the level.
  - **Rings have no display:** the ring manager's batch `0x006C2AF0` draws every ring record; vr-sonic wraps it (ours hidden, then each alone, shifted).
  - **Calling a lifted function twice from a wrap:** put ESP and EIP back before the second call (the first popped the return address); otherwise the guest's cookie check ends the game.
  - **SET conventions (measured):** ring line `Ry Rx (0,0,-1)`; spring `Rz Rx (0,1,0)` at 5 + scale Y (rot Y only turns Sonic); dash ramp up = 3.2 + scale Z, held 23 frames.
  - **Restart frees the SET objects but not rail path tasks.** Grind speed assist `SA2_VR_RR_RAIL_MIN` (2.0) or a slow climbing rail traps him.
  - **Test:** scratchpad `rr_obj.sh` (5 × 4 m, seed 4; `ST` drop station, `BACK` put-back, `PAD` pad steps, `STICK2` a second stick range; the stick must be released before pause-menu steps). Evidence lines: `room run objects: rings A -> B`, `action 1 -> 7 ... from the spring`, `flight from ... landed`, `dash panel at station`, `Sonic grinding`, `is gone (destroyed)`, `after the removal: 0`.

- **Room Run round 2 (2026-11-21).** README "Room Run, round 2". No grind assist; throws (`sonic_cmd_vel`, channel version 11; `SA2_XR_THROW_*`), held Sonic turns with the hand (`SA2_XR_SONIC_HOLD_TURN`), the course drawn only while `course_shown` (put-back, restart, menus and view changes take it away; a view change puts Sonic back). Test: scratchpad `rr2.sh` (the Frame's 2 × 2 m case; `HANDS` steps `time@hand:frame:x,y,z:grip`, `TURN`, `STICKS`, `PAD`). Pause-menu masks: Start 0x0010, Down 0x0002, Up 0x0001, A 0x1000, Left 0x0004 (Up from the top is EXIT).

- **Walk-through window (portal) (2026-11-21).** README "Walk-through window (portal)". In the stages, the VR / 2D
  VIEW window is the portal by default (`SA2_XR_WINDOW_PORTAL=0`: the mono window; the garden keeps the mono one).
  - **The model:** seated, anchored at the sweet spot `A = W T(0,0,D)`, `D = w/2 · P00`; `X_e = S(1/s) E_e⁻¹ A S(s)`. The
    scale is auto `(D + 0.2) / 60`, and Sonic out of the window uses the same `s`.
  - **The glove pass's V must map room metres to game units:** divide the whole 3×4 by `s`, not only the translation.
    Dividing only the translation put the portal's quads at nonsense clip coordinates, and the frame and HUD
    vanished.
  - **Planes:** two-pass uses `set_clip_planes(…, 5)`. Single-pass uses stereo-block rows 6–10, added to clip distances
    0–4 in left-eye clip space (`P0^-T M0^T p`). Two-pass and single-pass agree to at most 2 pixels.
  - **`SA2_XR_PORTAL_GLASS=open` is the default:** the strict glass plane cut the street under the camera
    (everything within `D/s` ≈ 42 units).
  - **Inside:** a stencil hole (our own bit; the game clears the stencil but never tests it). Room meshes go through
    the hole only; Sonic is hidden inside (v1).
  - **Crossing:** a logical state with a band (for input) and an exact one (for drawing). Use one side convention,
    z ≥ 0 = front: strict signs flapped at z = 0.
  - **Throws and jumps into the window:** `throw_into_window` / `flight_into_window` simulate the flight while
    ignoring the proxy floor's walls, and `drop_into_window` shares the put-back.
  - **Tests:**
    - offline: a replayed stream with `SA2_XR_WINDOW=1` and no runtime (synthetic eyes at `SA2_XR_PORTAL_HEAD`/`_PATH`);
    - live: the scratchpad's `pt.sh` / `portal_vs.sh`;
    - test-hand frame `g`; `_portal.png` dumps.
    - **The time-based head paths race the unpaced replay:** compare static poses only.
  - **Another agent may be running `sa2-game`** (benchmarks): `portal_vs.sh`'s cleanup only kills processes with
    this run's `XDG_CONFIG_HOME` and the XR release path.
  - **Pause-menu tests need two Downs to reach VR / 2D VIEW** (Continue, Restart, then the item); one Down lands on
    Restart.
  - **Frame portal test (2026-11-22):**
    - **Crossing is latched (second Frame test):** in front to back through the opening, out only back to front
      through it, round the frame keeps you in (the user's wish). The original "back view instead of the room" came
      from any crossing through the opening toggling the state. The first fix, leaving on any return to the front,
      lost "stay in the game".
    - Command 2 (the flat floor) must carry the hand's height, or drops and throws start from the floor.
    - The course: `rr_generate` tries more seeds, then grows the rectangle. Reproduce generator failures with
      `roomrun-gen --area W,D,UX,UY --sonic-cm 14.8 --player-r 0.3`.
  - **Room Run objects at half size (the user's decision):**
    - draws: `T(0,0,−60) S(k) T(0,0,60)` (the trampoline puts the object at (0,0,−60));
    - collision: each object's shape array (CollisionInfo `+0x0C`, count `+0x06`, range `+0x08`; 48-byte shapes)
      scaled. **Scale a heap array in place:** the game frees it with the object, so a replacement pointer of ours
      trapped in HeapFree (frame-place2). Only static arrays (the dash panel's `0x00B43EAC`) are copied;
    - **every character:** vr-sonic wraps the three player mains (`0x00717840`, `0x00728D70`, `0x00741530`) and
      gets the four other player displays through hide-upgrade-models; head nodes per display in
      docs/vr-game-hooks.md section 13. Test other characters with `STAGE=16` (Wild Canyon), `drylagoon`,
      `missionstreet`;
    - **room sounds:** positional sounds take their volume and pan from the camera location (`0x00438230`,
      `0x004383A0`), so vr-sonic places a room sound next to the camera for those calls;
    - enemy displays test the **camera location** (`[0x01DD92B0 + 4*screen]`, `0x007983F0`), not the view matrix:
      the trampoline moves it next to the object for the call, as it does the view;
    - `SA2_XR_ROOM_RUN_OBJECT_SCALE`, channel version 12.
  - **Course choice:** the generator's pick flips with tiny scale changes (10.12 vs 10.14 cm). The player keeps the
    most vertical of 9 seeds when the first isn't vertical.

## Frame performance (measured 2026-11-21)
- Seated VR with about 1,090 world draws: 59.9 fps.
- With `vr-level-culling` (radius 3000, about 1,780 world draws, so about 3,560 GPU draws after per-eye doubling): about 49–52 fps. Temperature is 75 °C, so it is CPU per-draw cost, not thermal.
- **Single-pass stereo (built 2026-11-21, the default `SA2_XR_STEREO=single`):** one two-instance draw per world draw, a SPIR-V epilogue picking the eye (`xr_stereo.cpp`, README "Single-pass stereo"). x86 replay of City Escape: −37% frame time in the heavy part, eyes match two-pass to ≤ 8 pixels beyond 2/255. Not yet measured on the Frame. Don't lower `SA2_VR_CULL_RADIUS` as a workaround unless asked.

## Frame performance after single-pass stereo and game-side dedupe (2026-11-21)
- City Escape, seated, with vr-level-culling, played to the end: **mostly 56–60 fps**, with lows of 53 (it was 42–55).
- The user says it is "much better", and deferred further performance work.
- Remaining costs: the translated code's memory helpers and the audio DSP, both in the core translation.

## Frame performance, round 3 (2026-11-22; BUILD_NOTES "Performance round 3")
- **Two heavy single threads, a pipeline:** the game's guest threads (one at a time under the guest lock) and the player's
  render thread. Each needs its own fast core; the slower one sets the frame rate. Below 60 the music breaks too (the
  game makes one frame of sound per Present).
- **Placement is the launcher's** (`host/launch/sa2_cpu_place.h`; aarch64 or `SA2_XR`): rank by `cpu_capacity` (the
  Frame's X4 has the top capacity, not the top clock), no little cores, the game on the next cluster less SteamVR's
  real-time render thread's CPUs (2-3), launcher/relay/player on the rest (5-7). Overrides `SA2_CPU_PLACE=0`,
  `SA2_GAME_CPUS`, `SA2_PLAYER_CPUS`, `SA2_CPU_AVOID`. **Lessons:** a guest pinned to the X4 alone shared it with
  threads it could not leave (55 fps); confining the rest to 0-6 put the player on the A520s (39 fps).
- **The mono 2D window writes `view_ok` 2:** vr-level-culling keeps nothing beyond the game's view (−28% game CPU).
  The portal keeps the cone.
- **The VR player:** no draw-state census (`SA2_D3D9GPU_CENSUS=0`, −13% player cycles offline), sound stretched up to
  10% under a 40 ms queue (`SA2_AUDIO_STRETCH=1`); both set by the launcher for `SA2_XR` only.
- **Measure:** x86 headless replay (scratchpad `vb.sh`, `fakeview`); the Frame `scratchpad/frameprof/frame_perf.sh`
  (`suite` headless A/B, `live` per-thread CPU and run-queue wait). Never run it on the Frame without the user's go-ahead.
- **Round 4 (Wild Canyon, BUILD_NOTES "Performance round 4"):** the game is the limit there. `draw-distance` (the Frame's
  game.env) costs a third of its game frame: `SA2_DRAW_DISTANCE_SCALE=1.25` or drop it. Measure a stage headless by
  `SA2_PRACTICE=<level>` + `SA2_VR_CULL_TEST_TP=F:x,y,z;...` (player 1 put at a Frame session's places).
  The portal from outside sends its own cone (hands **v13** `view_cone_deg`, −23% game CPU in City Escape).
  `SA2_CPU_PLAN=game-top` puts the game on the X4 for the A/B.
- **Proposals for xr_gpu.cpp's owner:** cache `eye_P` per frame (a `tan` per eye per draw: 1.1% on the Frame);
  avoid copying each draw's `Values` in `draw_inner` (about 6% of the player offline).

## Open problems (update this list when they change)
- **Level culling.** On PC the visible level is the GC land system, not `ListGroundForDrawing` (whose list is empty: `VisibleModelCount` 0 after the split at `0x4929D0`).
  - The GC land draw is at `0x4941C0`. It drops geometry through the sphere test `0x4926D0` (called from return address `0x4942A6`) and the clip distance.
  - The `vr-level-culling` tweak handles this and is on in the VR release. It doubles the 3D draws in City Escape and Metal Harbor.
  - Still culled: anything beyond the clip distance ahead, and stage objects (Chao World's are handled by `vr-object-culling`).
- **Which player position matches the drawn Sonic** (this record's or the previous one's) is unmeasured.
- **No custom floor or shadow pass yet.**
  - The garden's sea, clipped by the box, is its base. A stage's clipped slab is its own base.
  - With an opaque runtime the surround is dim grey.
  - A soft shadow needs a custom SDL pass with its own shaders, after the eyes' draws.
- Projected shadows and texgens are wrong off the game camera in diorama (`SA2_XR_TEXGEN_FIX` is experimental).
- No real passthrough yet: the back buffer is X8R8G8B8.
- **Repeated frames' pose age.** At 72 Hz one frame in six is shown twice, the second time a display period old (≈14 ms). Depth (`XR_KHR_composition_layer_depth`) for positional reprojection is on the backlog (Frame, 2026-11-21: swim "a lot smaller now" with the pose lead); SteamVR's use of it is unknown.
- **60 vs 72 Hz.** The player asks for a multiple of 60 via `XR_FB_display_refresh_rate` (`SA2_XR_REFRESH=auto`); SteamVR on the Frame offers only 72 through it, Monado only 60. On the Frame set 120 Hz (or 60) in SteamVR: judder gone at both (Frame test 2026-11-21). A true re-render of the newest game frame at display rate needs a recorded command list (uploads copied, releases deferred), since dynamic VBs are rewritten in place within a frame; the plan is in `host/xr/README.md`. Not done.
- Stereo CPU cost: single-pass stereo built (2026-11-21); the Frame's numbers are open. A/B tool: a recorded stream replayed offline (README "Single-pass stereo"; the scratchpad `ab.sh`).
- **Modes are per level:**
  - `SA2_XR_MODE=auto` (and `diorama`) gives the garden diorama and seated stages; `SA2_XR_STAGE=diorama` opts into stage tables.
  - To test seated offscreen, force the garden seated with `SA2_XR_GARDEN=0`.
  - `diorama_` changes at run time. The XR thread reads `diorama_xr_`.
- **Glove fit is tuned live, not guessed:**
  - `SA2_XR_GLOVE_TUNE=1`, saved to `xr_tune.txt`.
  - Tests: `SA2_XR_TEST_TUNE`.
  - Read the "hand axes" log lines from Frame runs before changing defaults.
