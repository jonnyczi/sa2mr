# OpenSA2 source build: notes (NixOS host)

Host: NixOS with no toolchain, so everything runs in a Debian 13 container whose tool versions match `external/OpenSA2/tools/pins.json`.

```bash
(cd build-env && docker build -t opensa2-build .)
build-env/run.sh '<cmd>'   # mounts external/OpenSA2 at /work and the SA2 Steam folder read-only at /sa2
```

`run.sh` uses `seccomp=unconfined` and `apparmor=unconfined`. Remill runs under `unshare -Urm`, and the installer tests use bwrap, so both need unprivileged user namespaces.

## Steps done (2026-11-21)

| # | Step | Result |
|---|---|---|
| 1 | Install SA2 via Steam (app 213610) | `~/.local/share/Steam/steamapps/common/Sonic Adventure 2` |
| 2 | `sonic2app.exe` hash | ✅ `4b1e09d7…` matches pin |
| 3 | Steamless 3.1.0.5 under **Proton 11.0** on the host: `steam-run python3 ".../Proton 11.0/proton" runinprefix Steamless.CLI.exe Z:<exe>` | ✅ the unpacked exe matches the pin `c97b116f…` (normalize: already pinned) |
| 4 | Tools under `private/`: Remill 5.0.7 (`tools/spike/install_remill.sh`), Ghidra 12.1.3, Temurin 21.0.12.1+1, Zig 0.15.2, SA2 Mod Loader @ `611d686f` | ✅ |
| 5 | `Documentation/scripts/s1_function_map.py` | ⚠️ the map's sha256 is `023dedf4…`, not the pinned `11b82ae8…`, but all counts match (12,683 functions, same regions and seeds), so the difference is cosmetic |
| 6 | `tools/s4/rebuild_image.py --jobs 20` + `lift_ir_receipt.py` | ✅ **LIFT_IR_OK**: all 12,683 map / 5,711 arms / 5,646 extras modules match upstream |
| 7 | `mb_edges.py`, `gen_table.py`, `drm_unwrap.py --companion` | ✅ |
| 8 | `tools/s5/build_host_deps.sh --fetch` (ALSA headers .deb fetched by hand, hash ✅) | DXVK ✅, glslang ✅, **SDL3 hash differs** (see deviations) |
| 9 | `private/sa2mr/finalize_extras.py` | needed before `perf_f80.py` (see deviations) |
| 10 | `tools/s8/perf_f80.py build --target x86_64-linux-musl --mode double --out private/generated/s8/pc-double/linux-obj` | ✅ |
| 11 | `tools/s7/release_build.py` (x86_64-linux) | ✅ RELEASE_BUILD_OK |
| 12 | `steam-run bin/sa2-install --source folder <SA2> --yes` | ✅ 4191/4191 files verified |
| 13 | `bin/sa2recomp --selftest` via `build-env/play.sh` | ✅ **SA2_SELFTEST_OK**: title at tick 3085, main menu at 3573 |

## Deviations from upstream

- **SDL3 library hash.** Ours is `b3bd7ab6…`; the pin is `dcd59404…`. SDL3 configures its optional backends from whichever X11/Wayland dev packages are present, and upstream doesn't document theirs. The local `tools/pins.json` `sdl3.library_sha256` was repinned to our build. This doesn't matter for the Frame, where SDL3 is rebuilt for aarch64 anyway.
- **Extras state.** `rebuild_image.py` leaves `private/generated/s4/link/extra_functions.next+allarms.json` without `objects`/`registry_c`. `cross_build.populations()` reads `objects`, so it raised `KeyError`. `private/sa2mr/finalize_extras.py` reruns `lift_extra.py`'s own finalize block (rejection pass, registry, objects) without lifting anything. Result: 5,646/5,646 registered, 0 rejected. **Worth reporting upstream.**
- **Installer sandbox tests.** 5 tests fail in the container (bwrap) unless it runs with the unconfined options above. Not yet re-run.

## ARM64 (Steam Frame) status

`release_build.py` only targets x86_64-linux and x86_64-windows. OpenSA2 *does* cross-build the lifted image for `aarch64-linux-musl` (`perf_f80.py --target`, `tools/s7/arm64_link.py`), but only for QEMU replays, not as a product. Building the arm64 product (game, player, OpenXR host) is our first porting task. Build it in the Steam Linux Runtime sniper arm64 SDK, as `../xelee/tools/package_steam_frame.sh` does.

## Running on this machine
`build-env/play.sh` (add `--selftest` for the scripted check). Two NixOS-specific fixes:
- `steam-run`: the binaries expect an FHS layout (glibc and the Vulkan loader from the host).
- `VK_DRIVER_FILES=/run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json`. Without it, steam-run shows the Radeon 890M twice, the d3d9gpu shader translator refuses the ambiguous pair, `Direct3DCreate9` returns NULL, and the player exits 3.

## ARM64 build (Steam Frame, 2026-11-21)

Output: `external/OpenSA2/private/generated/s7/release/linux-aarch64/` (same layout as linux-x86_64, plus `bin/sa2_player_xr`, `lib/libsa2_xr_vk.so`, `lib/openxr/libopenxr_loader.so.1`) and `…/release/sa2mr-linux-aarch64.tar.gz`. One command reproduces everything: `build-env/arm64/build_release.sh` (add `test` for the QEMU install and selftest).

All compilation is cross-compilation on x86_64. QEMU is used only for the final checks.

| Part | How it is built |
|---|---|
| Lifted image | `perf_f80.py build --target aarch64-linux-musl --mode double` into `private/generated/s8/pc-double/linux-aarch64-obj`. 24,040/24,040 modules, census ok, about 8 min. |
| `bin/sa2-game` | `private/sa2mr/arm64/release_build_arm64.py`. It runs release_build's `step_game` with `link_s4.TARGET = aarch64-linux-musl` plus `-ffp-contract=off` (as in arm64_link.py). The result is a static musl executable, like the x86 game, so it needs no glibc. |
| Players, installer, launcher | The tree's own build.sh scripts, with `private/sa2mr/arm64/xwrap` first on PATH. That is Debian's `aarch64-linux-gnu-g++-14` (14.2) with Valve's sniper arm64 SDK as sysroot (`build-env/arm64/sysroot`, image digest `966ad3ef…`). Headers, crt files, libgcc and libstdc++ come from the SDK (`-nostdinc`, `-B`). libstdc++ is linked statically. `-lpthread -lrt` are added because glibc 2.31 keeps them out of libc. |
| Host deps | `private/sa2mr/arm64/build_deps.sh`: SDL3 3.4.16 with the project patch and vendor string, DXVK 3.1.1 d3d9 (compile_commands + dxbc-spirv archive), static zstd 1.5.7, and the OpenXR loader 1.1.47. They go into `build-env/arm64/s5-tools`, which `build-env/cross/run.sh` mounts over `private/s5-tools`. `build-env/arm64/pins.json` (written by `arm64_pins.py`) is mounted over `tools/pins.json` and carries the arm64 library hashes. |

Checks enforced by `release_build_arm64.py assemble`: every ELF is AArch64, the game is static, no file needs a symbol newer than GLIBC_2.31, and no system library is bundled. Only SDL3, libpng16, DXVK and the OpenXR loader are bundled.

Verified under QEMU (`build-env/arm64/test`: Debian 13 arm64, lavapipe, Xvfb, capped to 8 cores):
- `TEST_D3D9GPU_OK 5445 checks`
- `SA2_INSTALL_OK` (4191/4191 files)
- `SA2_SELFTEST_OK … title at tick 3085, held 300 frames, main menu at tick 3572 after 2 Enter(s)`, in 25 min of wall time. The final packaged build passed again (`selftest2`, 27 min)
- every binary's libraries resolve inside the sniper runtime image

Problems solved along the way:
- Host wayland-scanner 1.23 emits `wl_proxy_marshal_flags`, which the SDK's libwayland 1.18 lacks. Fix: build an x86 wayland-scanner 1.18.
- CMake's find_library didn't find the multiarch directory. Fix: set `CMAKE_LIBRARY_ARCHITECTURE`.
- The cross toolchain's own trixie glibc was being picked up. Fix: hide `/usr/aarch64-linux-gnu/{include,lib}` behind tmpfs.
- The SDK's arm64 LTO plugin can't load. Fix: `-fno-use-linker-plugin`.
- `g++ -shared` doesn't link libgcc.a, which the outline atomics need. Fix: pass `-lstdc++ -lgcc`.
- extract_refs.py only accepts headers under `/usr/lib/gcc/`. Fix: mount the sysroot under that path.
- clang was tried first and dropped, because the tree's duplicate-symbol check is GCC-specific.

Upstream files edited (x86 guards only, the same pattern the tree already uses): `host/d3d9gpu/test/shaderc.cpp` and `host/dxvk_smoke.cpp`.

Not verifiable without a Frame: Turnip/Adreno rendering, SteamVR OpenXR, Frame controllers via Steam Input, PipeWire audio, and real-time performance. Deploy with `build-env/arm64/frame_deploy.sh deploy|install|selftest|run|log|fetch` (FRAME_HOST, default `steamos@frame.lan`).

## VR player (host/xr), x86_64 (2026-11-21)

Build it with `build-env/run.sh 'source tools/env.sh; bash host/xr/build.sh'`, which produces `private/generated/xr/release/linux-x86_64`. That release symlinks the flat release's game and libraries and adds `sa2_player_xr`, `libsa2_xr_vk.so`, the OpenXR loader, its own `share/game.env` (with `SA2_REC_CAMERA=1`) and a launcher that honours `SA2_XR=1`. Run it with `build-env/monado.sh &` and then `build-env/play-xr.sh`. Details: `external/OpenSA2/host/xr/README.md`.

Upstream files edited (all gated; the flat paths are unchanged):
- **Weak hooks:**
  - `host/d3d9gpu/d9g_entry.cpp`: `sa2xr_wrap_gpu`.
  - `host/d3d9gpu/d9g_device.cpp`: `sa2xr_note_shader`.
  - `host/player/player.cpp`: `sa2xr_pad`, `sa2xr_pad_changed` and `sa2xr_camera`. It also accepts the `SA2S_CAMERA` kind, and checks audio by the header's audio table instead of the version number.
- **Small d3d9gpu API additions:**
  - `host/d3d9gpu/d9g_draw_state.h`: `DrawRequest::edited()` and `DrawRequest::edit()`.
  - `host/d3d9gpu/d9g_gpu.h` and `d9g_gpu_sdl.cpp`: `native_texture()`.
- **Stream version 3:**
  - `host/sa2_stream.h`: `SA2S_CAMERA` and the v3 version and schema.
  - `host/sa2_present_rec.c`: includes the new `host/sa2_camera_rec.h`; with `SA2_REC_CAMERA=1` it writes v3, otherwise the stream is byte-identical.
  - `host/player/stream.cpp` and `stream.h`: read v3.
  - `host/player/mixer.cpp`: audio is checked by the header's audio table.
- **Launcher:** `host/launch/sa2_launch.cpp`. `SA2_XR=1` starts `sa2_player_xr` and passes the `SA2_XR*` variables through.
- **Container:** `build-env/Dockerfile` adds `libopenxr-dev` and `libopenxr-loader1`.

The game binary in the flat release was relinked with the recorder change (`release_build.py --only game`). `release.manifest` was not regenerated, so the `sa2-game` hash listed in it is stale; `--only assemble` refreshes it. The selftest still passes (`SA2_SELFTEST_OK`, main menu at tick 3580). The d3d9gpu tests pass (5445 checks) and so does `test_stream` (147 checks).

## First run on a real Steam Frame (2026-11-21)
- Headset: SteamOS 0.4.5, aarch64, SteamVR running (`/opt/steamvr/bin/linuxarm64/vrcompositor`). SA2 was not installed through Steam on the Frame. Instead, the PC's verified game folder was copied with `rsync` to `~/sa2-gamefiles` (3.25 GB at about 1.9 MB/s over Wi-Fi, roughly 28 min).
- `frame_deploy.sh deploy` → `bin/sa2-install --source folder ~/sa2-gamefiles --yes`: **SA2_INSTALL_OK**.
- `frame_deploy.sh selftest`: **SA2_SELFTEST_OK** (title at tick 3085, main menu at 3580).
- GPU: `Turnip Adreno (TM) 750`. It lacks `uniformAndStorageBuffer16BitAccess` / `8BitAccess`, so d3d9gpu falls back to 32-bit state words automatically. The selftest runs unpaced, so it says nothing about real-time smoothness yet.
- **Flat interactive on the Frame:** 59.9 fps (6,000 frames, 25 over 25 ms), PipeWire audio. The Frame controllers don't act as a gamepad for a plain desktop window: they act as laser pointers, and clicks arrive as touches, which shows OpenSA2's touch overlay. Use the VR player for controller input.
- **VR on the Frame works** (`FRAME_ENV="SA2_XR=1" build-env/arm64/frame_deploy.sh run`):
  - SteamVR/OpenXR 2.18.2. The Frame controller bindings were accepted (shoulders as `bumper`, D-pad, View) and the controllers drive Sonic.
  - Display at 72 Hz. Blend modes are opaque and alpha-blend.
  - Recommended view is 1728², but the eyes currently render at 1280×720.
  - Input needs the session in the *focused* state, so close the SteamVR dashboard.
- **Culling in VR:** level geometry far from the game camera's view is missing.
  - The `fov` tweak (`SA2_FOV_SCALE=1.4`) and `draw-distance` (objects only) help a little.
  - The level geometry needs a new culling tweak (in progress).
  - Frame `share/game.env` currently has `SA2_TWEAKS=skip-intro,practice-stage,fov,draw-distance`, `SA2_FOV_SCALE=1.4`, `SA2_REC_CAMERA=1`.

### `vr-level-culling` tweak (2026-11-21)

A new runtime tweak, `runtime/tweaks/vr-level-culling.c`, wraps `sub_4926d0`, `sub_4941c0`, `sub_47cae0` and `sub_47c2e0`. The VR release's `game.env` turns it on through `host/xr/build.sh`; it is off by default everywhere else.

Lines added under the tweak rules:
- `runtime/sa2_tweaks.h`: its line in the tweak list.
- `host/android/java/com/sa2recomp/app/GameSettings.java`: its switch, in the "Story and presentation" group.
- `docs/downloads.md`: its row in the tweaks table.

`test_tweaks.py`: 132 passed. Flat selftest: `SA2_SELFTEST_OK`, main menu at tick 3576.
- **`vr-level-culling` on the Frame (2026-11-21):** the user confirmed that missing level geometry and textures are much better. Frame `game.env`: `SA2_TWEAKS=skip-intro,practice-stage,fov,draw-distance,vr-level-culling`.

### `vr-chao-garden` tweak, garden diorama and passthrough (2026-11-21)

- **New runtime tweak:** `runtime/tweaks/vr-chao-garden.c`, which acts in start-in-chao-world's wrap.
  - `runtime/tweaks/start-in-chao-world.c` gets two added calls. Each tests vr-chao-garden's bit first, so start-in-chao-world is unchanged with it off.
  - Rule lines added in `sa2_tweaks.h`, `GameSettings.java` (Chao group) and `downloads.md`.
- **Camera record:** regions 4 (`CurrentLevel`) and 5 (`CurrentChaoArea`) appended in `host/sa2_camera_rec.h`. Readers ignore regions past those they know.
- **XR player:**
  - garden mode (static, scale 0.0022);
  - passthrough: alpha clears, coverage blend, `SOURCE_ALPHA` layer flag;
  - dim surround when opaque;
  - checkerboard eye dumps.
- **Checks:** `test_tweaks.py` 134 passed; `test_stream` 147 passed; flat `SA2_SELFTEST_OK`.

### Frame garden fixes (2026-11-21, after the first Frame garden test)

- **World shift (the island moved as Sonic walked):** the player used the view struct's lookAt taken at Present, a frame away from the view the game draws with. The camera record now carries regions 6/7: the Ninja matrix stack base pointer `0x0267053C` and its 3×4 matrix, built by `CameraApplyView` `0x0077E0C0` during display.
  - `host/sa2_present_rec.c` (upstream edit) emits the record after BeginScene/SetRenderTarget, just before each frame's first draw.
  - The player uses that matrix (`SA2_XR_VIEW=8`, default). `SA2_XR_STILL=1` measures level pieces at rotation 0.00000 / translation 0.000 units while Sonic walks.
  - `SA2_XR_VIEW` now accepts only 0 or 8. A diagnostic test window drawn with the transposed matrix (candidate 9, since removed) showed stretched, exploding polygons, which the user took for a broken game.
- **Head-relative steering:** `SA2_XR_STEER=head|camera` (head by default in diorama mode). Verified under Monado with `SA2_XR_TEST_STICK` and `SA2_XR_STEER_LOG`: stick right → Sonic walks at the head's right (90–102°) while the game camera swings by 60°. In camera mode the walk follows the camera.
- **Chao World objects popping in and out:** a new runtime tweak, `runtime/tweaks/vr-object-culling.c`, wraps `sub_54ac70`, the garden objects' camera test, and `sub_42b400`/`sub_42f640`, Ninja's model-clip frustum tests. In Chao World it answers "in view"; it is display only.
  - Draws stay at 421–431 while walking (234–393 before). See `media/chao_garden_objects_before_after.png`.
  - Rule lines are in `sa2_tweaks.h`, `GameSettings.java` (VR group) and `downloads.md`.
  - `host/xr/build.sh` adds it to the VR release's `game.env`. **The Frame's hand-kept `game.env` needs `vr-object-culling` added to `SA2_TWEAKS`.**
- **60 vs 72 Hz:**
  - The player asks for a refresh rate that is a multiple of 60 through `XR_FB_display_refresh_rate` (`SA2_XR_REFRESH=auto|Hz|0`). Monado offers only 60, and SteamVR's support on the Frame is unknown. Setting 120 Hz in SteamVR divides evenly.
  - True re-rendering at display rate is not done. It needs the frame's whole command list recorded, because dynamic vertex buffers are rewritten in place within a frame. The plan is in `host/xr/README.md`.
- **Swim with head motion (Frame test 2):** each frame was drawn with the last XR loop's pose, two display frames before it was shown. SteamVR reprojects rotation only, so the near table swam with head translation.
  - Fix, as in xelee e26c1ced: pose lead (`SA2_XR_POSE_LEAD=1`). The views are located for the display time of the frame's first showing (measured lead), and the projection layer keeps the pose each image was drawn with.
  - Monado with `SA2_XR_TEST_MOTION=0.1,0.5`: pose age 33.3 ms → 0.1 ms, head error 6.7 mm → 0.01 mm mean.
  - Frame confirmation: the swim is "a lot smaller now". Depth submission (`XR_KHR_composition_layer_depth`) for the repeated frames is on the backlog.
  - The Frame test also found: judder gone at 120 Hz and at 60 Hz in SteamVR; SteamVR's `XR_FB_display_refresh_rate` offers only 72.
- **Checks:**
  - `test_tweaks.py` 135 passed.
  - `TEST_D3D9GPU_OK` (5445 checks).
  - `test_stream` 147 passed.
  - Flat `SA2_SELFTEST_OK`.

### Chao hands and the world grab (2026-11-21)

- **New files:**
  - `host/sa2_vr_hands.h`: the hands channel, an mmap file.
  - `runtime/tweaks/vr-chao-hands.c`: the game tweak.
- **Upstream edits:**
  - `host/launch/sa2_launch.cpp`: `SA2_VR_HANDS=<session>/vr_hands.bin` for the game and the player, only with `SA2_XR=1`.
  - `runtime/tweaks/garden-ring-trickle.c`: its wrap of `ALW_Control_Main` also hands the frame to `vr-chao-hands` when that tweak is on. It is unchanged with that tweak off.
  - Rule lines in `sa2_tweaks.h`, `GameSettings.java` (VR group) and `downloads.md`.
  - `host/xr/build.sh` turns the tweak on in the VR release.
- **XR player:**
  - Grip pose, squeeze and haptic actions. On Touch the squeezes grab instead of acting as shoulders.
  - World grab with one or two hands (`place_`), clamped to 0.0022–0.06 m/unit. The garden's placement is saved in `$XDG_CONFIG_HOME/sa2recomp/xr_garden_place.txt`.
  - Default garden scale 0.004 m/unit, with the box at 318 units.
- **Verified** with scripted hands: the game-side `SA2_VR_HANDS_SCRIPT` and the XR-side `SA2_XR_TEST_HANDS` under Monado. Throwaway profiles and temporary Chao and items only (not in the save).

  | Action | Result |
  |---|---|
  | Pick up | happiness +1 |
  | Gentle drop | no change |
  | Throw | happiness −5 |
  | Pet (fresh Chao) | happiness +1, alignment +0.05 |
  | Fruit | taken, eaten (hunger 3000 → 688, fruit gone) |
  | Red drive | absorbed (powerrun +0.05) |
  | Animal kind 16 | absorbed (animal bit `0x10000`) |
  | Hat | worn (headgear 1) |
  | XR hand → game | grabbed the Chao at its position |
  | Sonic while holding | walked under the keyboard |

  Eye dump: `media/chao_hands_lift_drop_throw.png`.

### Gloves, two-hand holding and a held Chao facing the viewer (2026-11-21)

- **New files:**
  - `host/xr/xr_gloves.{h,cpp}`: a procedural glove (original, no game data) and an SDL_gpu pass.
  - `host/xr/shaders/glove.{vert,frag}` and `gen_spv.sh`.
  - `host/xr/xr_glove_spv.h`: generated SPIR-V, kept in the tree because the cross image has no glslangValidator.
- **Upstream edits:**
  - `host/d3d9gpu/d9g_gpu.h` and `d9g_gpu_sdl.cpp`: `Gpu::external_pass`, which closes the backend's pass and runs a decorator's callback with the device, command buffer and targets. It defaults to a no-op, and the flat player never calls it.
  - `host/xr/build.sh`: compiles `xr_gloves.cpp`.
- **Channel v2** (`host/sa2_vr_hands.h`):
  - The head's position (game units).
  - Per-hand `in_reach`.
  - A test-only `test_chao` position.
- **`vr-chao-hands`:**
  - Now wraps `Chao_Display` (`sub_54ff80`) to set a held Chao's yaw toward the head. The yaw is `atan2(dz, dx) − 0x4000`, because a Chao's model faces (−sin a, 0, cos a).
  - Still takes its frame from `garden-ring-trickle`'s wrap.
  - New test commands: `follow`, `hunger`, `sonichold`.
- **Verified under Monado:**
  - Gloves are drawn with depth, open and closed, with the in-reach tint (`media/vr_gloves_and_held_chao.png`).
  - A held Chao faces the head from behind it and from its palm side (drawn yaw = yaw toward the head in the log).
  - Two holds at once: a Chao and fruit, then two Chao; one was thrown while the other was kept, then that one was put down gently.
  - **Limit (the game's rule):** a held Chao does not take an item. Its item checks run only in its free behaviours. Put it down to feed it.
- **Checks:** `TEST_D3D9GPU_OK` (5445), `test_stream` 147, `test_tweaks` 136, flat `SA2_SELFTEST_OK`.

### Sonic's gloves from the game files, real petting, facing fixed (2026-11-21)

- **Gloves:**
  - New `host/xr/xr_sonic_hands.{h,cpp}`. At run time it reads the user's `resource/gd_PC/sonicmdl.prs` (PRS, then Ninja chunk models 16 and 18) into memory. Nothing is written anywhere, nor shipped.
  - The launcher (upstream edit) sets `SA2_XR_INSTALL` for the VR player only.
  - The procedural glove (`xr_gloves.cpp`, now with an outline hull) stays as the fallback.
  - The axes are measured from each mesh. Assuming them hid the fingers from the wearer's view, and the user reported "no fingers".
- **Petting:**
  - The hand now carries a `ChaonadeExec` (the game's own pet sphere), and `vr-chao-hands` wraps `sub_476fd0`. The Chao's own code plays the petted behaviour.
  - Scripted stroke: happiness 0 → 3, alignment +0.05 each.
  - The Frame log had shown pets with no effect: a direct behaviour call ends at once, and gives no reward after a hold.
- **Facing:** yaw = atan2(dx, dz). The Frame showed the old formula reversed sideways. Checked from the left, right and front (`media/held_chao_faces_head_left_right_front.png`).
- **Test runner:** kills its whole run by process name and start time. One orphaned player window had confused the user.
- **Gloves, second round** (the user saw "no fingers" twice):
  - The model files hold **one** relaxed hand: 16 and 18 are the same mesh, 18 on the arm chain. The "open hand" and "fist" told apart earlier were only different viewing angles.
  - The fist and the open state are now made by bending that hand's fingers about the knuckle line, and the fit is measured in the hand node's frame.
  - The "missing fingers" were test poses whose eye sat on the forearm axis, where Sonic's large cuff hides the hand. The palms-down check confirms the thumbs point inward.
  - Load check logged: `open L/R 315 triangles 11.6 cm, fist L/R 315 triangles 13.2 cm … ok`.
  - Checked offscreen only: the dump tool `private/sa2mr/xrtools/glove_dump.cpp` with a renderer, and a null-compositor Monado with `--video-driver offscreen`.
- **Gloves, third round (Frame):** they were upside down, on the wrong hands, and the fist bent backwards. One cause: the measured palm side was the back of the hand.
  - Flipped. Verified offscreen against a synthetic grip built to the spec (right palm −X, thumb +Y, fingers −Z): `media/vr_gloves_spec_grip_offscreen.png`.
  - Check line: `open L/R 315 / 11.7 cm, fist L/R 315 / 12.0 cm … ok`.

### Draw marks and the Chao status bubble (2026-11-21)

- **Stream version 4** (upstream edits):
  - `host/sa2_stream.h`: `SA2S_DRAW_MARK` = 8, tags `SA2S_MARK_CHAO_BUBBLE` and `SA2S_MARK_CHAO`.
  - `host/sa2_present_rec.c` includes the new `host/sa2_drawmark_rec.h` (`sa2_rec_draw_mark`, written only with `SA2_REC_DRAWMARK=1` and the camera).
  - `host/player/stream.{h,cpp}` reads version 4 and the mark.
  - `host/player/player.cpp`: weak hook `sa2xr_draw_mark`.
  - `host/xr/build.sh` adds `SA2_REC_DRAWMARK=1` to the VR `game.env`.
- **`vr-chao-hands`** wraps the bubble's main (`0x564C80`: keeps it up for the hands' Chao) and its display (`0x565200`: brackets its draws with marks).
- **The XR player:**
  - draws the marked draws flat into the HUD;
  - a second quad layer crops the bubble's rect and floats it by its Chao, facing the head.
- **Verified offscreen:**
  - the marks bracket exactly the bubble's draws (118 when fully up);
  - the HUD crop is the game's bubble (`media/chao_bubble_floating_quad_content.png`);
  - the logged quad pose sits by the Chao.
- **Checks:** `test_stream` 147, `TEST_D3D9GPU_OK` 5445, `test_tweaks` 136, flat `SA2_SELFTEST_OK`.
- **Bubble follow-up:** the marked draws now go to the bubble's own target and swapchain, not the HUD. The main HUD quad no longer duplicates the bubble. Offscreen: the HUD dump is empty while the bubble target holds the bubble (`media/chao_bubble_floating_quad_content.png`).

### Held objects at hand scale (2026-11-21)

- **Stream v4 marks** gain an anchor (centre and radius) and the tag `SA2S_MARK_HELD`. `SA2S_MARK_CHAO` is kept for the room Chao.
- **`vr-chao-hands`** marks each held object's own draws while held and for 30 frames after:
  - a Chao: `Chao_Display`, plus a new wrap of the emotion ball's display `0x5501A0`;
  - items: 14 display wraps (the callers of `0x54AC70` other than the Chao's own, and the Chaos Drive's `0x545150`).
  - It also publishes each held item's distance to the nearest free Chao (`held_near_chao`, hands file v4).
- **XR:**
  - MV' = V · E · V⁻¹ · MV for marked draws, with no box clip.
  - k is the same for every object (a Chao 18 cm tall: ×8.18 at 0.004 m/unit, assuming a 5.5-unit model height).
  - Eased over 0.2 s; the hold target is in front of the palm by the object's scaled radius; the bubble sits beside the big Chao.
- **Verified offscreen** (null-compositor Monado, offscreen video):
  - each hand alone holding a Chao, and two Chao at once (`media/held_*`);
  - grow and shrink sequences (×1.15 → 8.18; ×8.05 → 1.54).
  - Test hooks: a second test Chao (`d` frame), `SA2_XR_SHOT_EASE`, and a log of grip presses that find nothing.
- **Checks:** `test_stream` 147, `test_tweaks` 136, flat `SA2_SELFTEST_OK`. The flat stream is unchanged with the marks off: the flat `game.env` sets neither `SA2_REC_CAMERA` nor `SA2_REC_DRAWMARK`.
- **Design note for "Chao in Your Room":** in `host/xr/README.md`, for review.

### Chao in Your Room (2026-11-21)

- **Built** to the approved design (details: `host/xr/README.md`, "Chao in Your Room"). Not yet on the Frame.
- **Hands file v5** (`host/sa2_vr_hands.h`): room commands (out/back with a count), the pen mapping (`hand_pen`, `head_pen`, `hand_pen_use`, `pen_r`), and the game's room Chao, its position and the pen centre.
- **`vr-chao-hands`:**
  - the pen (`SA2_VR_PEN`, default 45,0,-100 r 15) with an XZ clamp and an edge reset;
  - `SA2S_MARK_CHAO` marks on the room Chao's display, emotion ball and held item;
  - new wraps: `sub_540fd0` (its shadow is not queued) and `sub_45a260` (the character's hold target never sees it);
  - a direct-give fallback (`room_offer`) and the test commands `tp:X:Z`, plus the water level in `info`.
- **XR:**
  - STAGE space and play-area bounds, logged; touch calibration (double grip) for the floor and tables;
  - release outside the box → room, inside → back; the room circle (0.4 m) is kept wholly outside the garden box;
  - pen mapping for draws, hands, velocity and the bubble; a blob shadow from the glove pass (a disc mesh and a blended pipeline; glove shaders regenerated);
  - test hooks: the `r` hand frame and `SA2_XR_TEST_HEAD_PITCH`.
- **Lessons:**
  - The first pen (by the pond) was under the water level (-2). The Chao swam every frame (`0x00561630`), so it never fed and was pushed at the edge for ever.
  - Clearing the Chao's status 0x40 for good also broke its eat. The character's hold test is wrapped instead.
  - The room circle first reached into the garden box, and the garden's water hid the Chao.
  - A `lock_guard` held across `head_room()` deadlocked the XR thread.
- **Verified offscreen** (`media/xr-test/roomB`, null-compositor Monado, offscreen video):
  - pull-out across the box edge;
  - the Chao walking on the floor;
  - feeding in the room (the game's own take, eat, happiness +1);
  - the bubble beside it;
  - picking it up and putting it back.
- **Checks:** `test_tweaks` 136, `XR_BUILD_OK`, `RELEASE_BUILD_OK`.

### Frame round 2 fixes (2026-11-21)

From the Frame test of the combined build:

- **Per-level mode.** `SA2_XR_MODE=auto` is the default; `diorama` means the same.
  - Chao World is the garden diorama with hands; the action stages are seated (immersive).
  - The stage table is opt-in: `SA2_XR_STAGE=diorama`.
  - City Escape had run on a table at the garden's saved scale.
  - In seated levels the gloves are drawn with no world grab and no game input; the layer is opaque.
  - The garden's placement file is the garden's only.
- **Gloves live tuning.** `SA2_XR_GLOVE_TUNE=1` (see `host/xr/README.md`):
  - yaw, pitch and roll in 15° steps, mirror swap, and the bend side;
  - held size and the bubble's distance and side;
  - logged and saved to `xr_tune.txt`;
  - the grip and aim pose axes are logged (aim pose actions added, with a binding fallback without them).
- **Glove fit.** The default mirrors the mesh for the right hand: the mesh is Sonic's left hand, and each glove showed on the opposite hand.
- **Sizes and the bubble.**
  - `SA2_XR_HELD_CHAO_HEIGHT` is 0.30 m (was 0.18).
  - The bubble sits at least 0.65 m from the head, 8 cm further to the side.
- **Touch calibration** is a deliberate gesture: grip and trigger held, the controller still for 1 s. A double grip while grabbing the world had taken a table.
- **Verified offscreen:**
  - `media/xr-test/tune3`: the tuning steps;
  - `media/xr-test/seat1`: a seated level with gloves (the garden forced seated);
  - `media/xr-test/roomC`: Chao in Your Room at the new scale.
- **Checks:** `XR_BUILD_OK`, `test_tweaks` 136, public check.

## Frame tune fixes and single-pass stereo (2026-11-21)

Part A, from the Frame tune tests (`host/xr` only):
- **Cutscene hands:** the gloves are Sonic's event e0000 hands, read from the user's install and hash-checked. 0x2a79c is the right hand. The fallback is the in-game hand.
- **Defaults** are the user's final tune: yaw 0, pitch −15, roll 15, held 0.34 m, bubble 0.60 / −0.07. In the tuning mode, A swaps the cutscene meshes.
- **Held objects** sit on the palm and grow upward.
- **World grab:** one hand only moves the world.
- **Seated levels** submit with the OPAQUE environment blend mode.

Part B, single-pass stereo (`host/xr/README.md`, "Single-pass stereo"):
- **New files:** `host/xr/xr_stereo.{h,cpp}`, the vertex SPIR-V epilogue. `host/xr/build.sh` compiles it.
- **Upstream edits** (the flat path is unchanged; `NonIndexed::instances` defaults to 1):
  - `host/d3d9gpu/d9g_draw_state.h`: `draw::NonIndexed` gains `instances`.
  - `host/d3d9gpu/d9g_gpu_sdl.cpp`: a non-indexed draw records and draws `instances`. It drew 1 before; indexed draws already carried theirs.
- **Measurement path:**
  - `sa2recomp --record-session`, then `--debug-export`, then `tools/s7/replay_export.py --release <xr release> --install /sa2 --env SA2_TWEAKS=practice-stage,... --env SA2_PRACTICE=13 --env SA2_REC_CAMERA=1` gives a deterministic stream.
  - `sa2_player_xr STREAM --hidden` with `SDL_VIDEO_DRIVER=offscreen` replays it unpaced.
- **Checks:** `XR_BUILD_OK`, `D3D9GPU_BUILD_OK`, `TEST_D3D9GPU_OK 5445 checks`, `test_stream` 147 passed, flat `RELEASE_BUILD_OK` and `SA2_SELFTEST_OK` (offscreen; menu at tick 3577), `RELEASE_BUILD_ARM64_OK`.

## Game-side performance (2026-11-21)

See `host/xr/README.md`, "Game-side performance". Live x86, City Escape: stream 1.2–1.3 MB → 0.56–0.68 MB a frame, D3D9 calls −27%, game CPU about −30%.
- **Upstream edits:**
  - `host/sa2_present_rec.c`: redundant float constants left out of VR streams (`SA2_REC_DEDUPE`), and the integer stream-stats line. Both are on only with `SA2_REC_CAMERA=1`, so flat streams are unchanged.
  - `host/launch/sa2_relay.{h,cpp}` and `sa2_launch.cpp`: the "relay perf" line (`SA2_RELAY_PERF`; on with `SA2_XR=1`).
  - `tools/s5/objcheck.py` and `Documentation/scripts/tests/test_s5_stream.py`: `sa2_rec_draw_mark` (the earlier draw marks' entry point) is an allowed definition. The recorder object check had failed on it since the marks were added.
- **Our files:** `runtime/tweaks/vr-level-culling.c` (the seated view cone, `SA2_VR_CULL_CONE`), `host/sa2_vr_hands.h` version 6 (`view_fwd`), `host/xr/xr_gpu.cpp` (`write_view_cone`).
- **Checks:**
  - `test_tweaks` 136 passed.
  - `test_s5_stream.py` 277 passed; 14 fail on the build container's missing `zstd` binary, which is environmental.
  - Recorder object check OK; flat `SA2_SELFTEST_OK` (menu at tick 3582).
  - A deduplicated stream replays pixel-identical.
- **Frame profile (2026-11-21, `frame-perf3`, City Escape replay with no player):**
  - The game's CPU time per frame in the heavy part is 12.7–13.0 ms with default scheduling and **10.0 ms pinned to cpu7**, the one Cortex-X4 core and the fastest (`SA2_GUEST_HOST_CPUS=7`).
  - It is 12.6 ms on cpu4 (A720) and 49 ms on cpu0 (A520).
  - memfast gives no gain. The CRI reverb is 13.6% of the game's CPU there.
  - All replays matched. The ranked proposals are in the performance agent's report: the window cone, `-mcpu`, D3D9 dispatch, the recorder, the reverb and the code layout.

## 2D window, Sonic out of the window, first person (2026-11-21)

See `host/xr/README.md`, "2D window, Sonic out of the window, first person", and `docs/vr-game-hooks.md` section 7.
- **New tweak `vr-sonic`** (`runtime/tweaks/vr-sonic.c`; wraps `sub_717840` Sonic_Main and `sub_4ec770` CameraUpdateLocation). The VR release's `game.env` turns it on through `host/xr/build.sh`. **The Frame keeps its own `share/game.env`**, so add `vr-sonic` to `SA2_TWEAKS` there by hand.
- **Upstream edits** (added lines only; each is unchanged with vr-sonic off):
  - `runtime/tweaks/fast-stage-load.c`: its wrap of `0x0043A780` hands the call to `sa2_vr_sonic_pause` first.
  - `runtime/tweaks/hide-upgrade-models.c`: its wrap of `0x00720090` (Sonic_Display) hands the call to `sa2_vr_sonic_display` first.
  - `runtime/sa2_tweaks.h`, `GameSettings.java` ("Story and presentation" group) and `docs/downloads.md`: the tweak's lines.
  - `host/sa2_stream.h`: `SA2S_MARK_SONIC` 4.
  - `host/sa2_vr_hands.h`: version 7.
- **Our files:** `host/xr/xr_gpu.cpp` (the window, Sonic, the vignette), `xr_gloves.{h,cpp}` (the vignette ring), `host/xr/build.sh` (vr-sonic in the VR `game.env`).
- **Checks:**
  - `test_tweaks` 137 passed; `test_stream` 147; `TEST_D3D9GPU_OK` 5445; flat `SA2_SELFTEST_OK` (offscreen, menu at tick 3584); public check 0 failures.
  - Offscreen XR runs under the private null-compositor Monado: `media/xr-test/pause1`, `pause2`, `chaopause3`, `run1`, `winmove1`, `winkeep`, `fpfwd`, `fpdown`, `fpvig`; key pictures `media/vr_*` of 2026-11-21.
- **Test scripts** (scratchpad): `run_vs.sh` (a stage or the garden under x9 Monado with the Frame-mimic layer, game.env swapped for the run only), `grab_w.sh` (the window grab test), `monado_null.sh` (a private null-compositor Monado that never touches the user's).

## Room Run v0: a generated course in the room, Sonic on it (2026-11-21)

See `host/xr/README.md`, "Room Run v0", and `docs/vr-game-hooks.md` section 8.
- **Entry:** the "Sonic out of the window" flow. With `SA2_VR_ROOM_RUN=1` (game.env) or `SA2_XR_ROOM_RUN=1`, letting go of Sonic in the room puts him on the course (command 4) instead of the flat floor.
- **roomrun (root repo):** new `include/roomrun/playmesh.hpp`, `src/playmesh.cpp` (`rr::buildPlayMesh`: the course as a continuous walled road with collision chunks, colour groups and the centreline), `roomrun-gen --play-obj`, and the "playable mesh" test. 10,761 checks pass; the 1000-room batch is unchanged.
- **Build environment:** `build-env/run.sh` and `build-env/cross/run.sh` mount `roomrun/` read only at `/roomrun`. `host/xr/build.sh` compiles the generator into `sa2_player_xr` when it finds it (`-DSA2XR_ROOMRUN`, the modules JSON embedded), else builds without Room Run.
- **Our files (OpenSA2):** `host/xr/xr_roomrun.{h,cpp}` (generation, the course file, nearest station), `host/sa2_roomrun_course.h` (the course file format), `host/xr/xr_gpu.cpp` (mapping, worker thread, drop, track steering, lap log, drawing, the shadow on the deck), `host/xr/xr_gloves.{h,cpp}` (a course vertex buffer drawn first, opaque), `host/xr/build.sh`, `host/sa2_vr_hands.h` version 8, `runtime/tweaks/vr-sonic.c` (command 4: the course file into DynCol pieces, the respawn), `docs/vr-game-hooks.md` section 8. No upstream file changed in this step; no new tweak name.
- **Checks:** `XR_BUILD_OK`; `RELEASE_BUILD_OK` (game); `D3D9GPU_BUILD_OK`, `TEST_D3D9GPU_OK` 5445, `test_stream` 147; `test_tweaks` 137 passed; flat `SA2_SELFTEST_OK` (offscreen, menu at tick 3576); public check 0 failures.
- **Verified offscreen** (`media/xr-test/rr1`–`rr3`, `media/roomrun_v0_*`): the course drawn in the room; Sonic dropped on it ran 4.4 laps under the scripted stick with track steering, 0 respawns, on the deck within ±1.2 units, up from 0.81 m to 1.28 m and down.
- **Test script** (scratchpad): `rr_w.sh` (the window grab test with `SA2_VR_ROOM_RUN=1`, the stick along the course).
- **arm64:** `release_build_arm64.py --only game,players,launch,assemble,package`: `RELEASE_BUILD_ARM64_OK`, the generator compiled into `sa2_player_xr` (xr-build.log "== roomrun (/roomrun)"), tarball sha256 `44ef13bc…`. Not deployed. **Frame `share/game.env`:** add `SA2_VR_ROOM_RUN=1` (keep `vr-sonic` in `SA2_TWEAKS`); no new tweak name.

## Frame feedback on the modes (2026-11-21, `frame-modes1`)

See `host/xr/README.md`, "Fixes from the Frame's first test of these modes", and `docs/vr-game-hooks.md` section 9.

- **Stick:**
  - The seated view recentres on a mode change.
  - The stick is turned only in play (GameState 16 via `game_state`).
  - First person writes the camera angle word E4 (the stick's heading). Its view eases toward the motion, it has snap turns, and the eye is at the head node.
- **Vignette:** off by default.
- **2D window:** drawn into the eyes at the far plane (`SA2_XR_WINDOW_LAYER=eyes`, new `shaders/window.{vert,frag}`), so the gloves and Sonic stay over it. The drop-back zone is 4 cm.
- **Restart:** vr-sonic resets on any GameState other than 7, 16 or 17 and takes its stale collision pieces out. A Restart keeps the task address and the dynamic collision list.
- **Room Run:** at least 2 × 2 m about a small play area, with a 0.3 m free circle.
- **Files:**
  - `host/sa2_vr_hands.h` version 9
  - `runtime/tweaks/vr-sonic.c`
  - `host/xr/xr_gpu.cpp`, `xr_gloves.{h,cpp}`, `xr_roomrun.cpp`, `xr_glove_spv.h`
  - `shaders/gen_spv.sh`, `shaders/window.{vert,frag}`
  - `host/xr/README.md`, `docs/vr-game-hooks.md`
  - No other upstream file.
- **Checks:** test_tweaks 137 passed; public check 0 failures; flat `SA2_SELFTEST_OK` (menu at tick 3578). d3d9gpu and the player are not touched in this step.

## Frame feedback, round 2 (2026-11-21, `frame-modes2`)

See `host/xr/README.md`, "Fixes from the Frame's second test", and `docs/vr-game-hooks.md` section 10.

- **First person:**
  - The right stick turns smoothly.
  - The eye is locked to his head at any speed: it is moved after Sonic_Main, and the head node is matched with the feet at that display.
  - On the board the eye is at the head's own eye point (its up and face axes).
- **2D window:** it keeps the live level while Sonic is out. The camera is held at the level, and his display is drawn from a view shifted next to him, which fixes the stretched legs.
- **Putting him back:** he goes back where he is dropped on the window, onto the ground there, falling back to his saved spot. From the room, the floor goes first because of the ride.
- **Room steering:** head-relative by default.
- **Room Run:**
  - He drops straight from the hand.
  - The floor is ground.
  - Course decks get an underside, a ceiling.
- **Root repo:** `roomrun/src/playmesh.cpp` (the deck underside in the collision) and `roomrun/tests/test_main.cpp`, now 8 collision triangles per station. The roomrun tests pass.
- **OpenSA2 files:**
  - `runtime/tweaks/vr-sonic.c`
  - `host/sa2_vr_hands.h` (version 10)
  - `host/sa2_stream.h` (`SA2S_MARK_SONIC_VIEW` 5)
  - `host/xr/xr_gpu.cpp`, `host/xr/README.md`, `docs/vr-game-hooks.md`

## Frame feedback, round 3 (2026-11-21, `frame-modes3`)

See `host/xr/README.md`, "Fixes from the Frame's third test", and `docs/vr-game-hooks.md` section 11.

- **Put-back after a room visit "fell into the void":** Sonic landed on the street (the Frame's camera records show him standing at his saved spot), but the game camera carried itself by his 5000-unit drop and ended under the street, looking at its underside. vr-sonic now holds the camera (its location and the camera's own work at `0x01DCFF0C`) at the camera saved when he was taken, for 3 updates after every put-back, and keeps his saved spot if he is taken again before a put-back has landed.
- **Room Run ramps behaved as steps and ledges:** the collision walls shared their pieces with the decks (in the game, a wall touched on a slope sank him half a unit into the deck without ground), one-station creases (up to 24 degrees), full-width twisted quads on bends (34-degree slivers) and abrupt width changes. `buildPlayMesh` now puts the walls in pieces of their own, smooths the profile, limits slopes on the steepest facet, splits the deck into 2 lanes, narrows the walkway gradually, and sorts each piece's triangles by Z for the game's collision cache.
- **Root repo:** `roomrun/src/playmesh.cpp`, `roomrun/include/roomrun/playmesh.hpp`, `roomrun/tests/test_main.cpp`, `roomrun/tools/roomrun_batch.cpp` (a play-mesh summary line), `roomrun/examples/batch_1000.txt`. The generator's own output is unchanged.
- **OpenSA2 files:**
  - `runtime/tweaks/vr-sonic.c`
  - `host/xr/xr_gpu.cpp` (the `k` test-hand frame; stick-down runs back along the course under track steering), `host/xr/xr_roomrun.cpp` (two diagnostics)
  - `host/xr/README.md`, `docs/vr-game-hooks.md`
  - No other upstream file.
- **Checks:** roomrun tests pass; 1000-room batch identical to before apart from timing, plus the play-mesh line; test_tweaks 137 passed; test_stream 147 passed; public check 0 failures; flat `SA2_SELFTEST_OK` (menu at tick 3577).

## Room Run objects (2026-11-21)

See `host/xr/README.md`, "Room Run objects", `docs/vr-roomrun-objects.md` section 8 and `docs/vr-game-hooks.md` section 12.

- **What:** the game's own rings (lines, circles, singles), springs, dash panels, dash ramps, grind rails and (opt-in) GUN Beetles/Hunters spawned on the generated course, drawn into the room at the course's scale, removed on put-back, restart, death and level end.
- **Root repo:**
  - New: `roomrun/include/roomrun/objects.hpp`, `roomrun/src/objects.cpp` (`rr::placeObjects`, the game-physics flight check).
  - Edited: `roomrun/CMakeLists.txt`, `roomrun/tests/test_main.cpp` (game-objects test), `roomrun/tools/roomrun_batch.cpp` (objects line), `roomrun/tools/roomrun_gen.cpp` (`--objects`), `roomrun/examples/batch_1000.txt`.
- **OpenSA2 files:**
  - `runtime/tweaks/vr-sonic.c` (spawning, display trampolines, the ring batch wrap `sub_6c2af0`, rail assist, removal)
  - `host/sa2_roomrun_course.h` (version 2: objects and rails)
  - `host/sa2_stream.h` (`SA2S_MARK_RR_OBJECT` 6)
  - `host/xr/xr_roomrun.cpp`, `host/xr/xr_roomrun.h`, `host/xr/xr_gpu.cpp`
  - `host/xr/README.md`, `docs/vr-roomrun-objects.md`, `docs/vr-game-hooks.md`
  - No other upstream file.
- **Checks:** roomrun tests pass (15036 checks); 1000-room batch identical apart from timing, plus the objects line; test_tweaks 137 passed; test_stream 147 passed; `TEST_D3D9GPU_OK` 5445; flat `SA2_SELFTEST_OK` (menu at tick 3582); `RELEASE_BUILD_OK`, `XR_BUILD_OK`.

## Room Run, round 2 (2026-11-21, Frame test `frame-rrobj1`)

See `host/xr/README.md`, "Room Run, round 2", and `docs/vr-roomrun-objects.md` section 9.

- **What:** variety on small courses (hop springs, added dash panels), enemies on by default (Beetles only), grind paths 7 units up with no assist and more of them, throwing Sonic (room and window), held Sonic turning with the hand, the course leaving the room with Sonic (put-back, restart, menus, view change).
- **Root repo:** `roomrun/src/objects.cpp`, `roomrun/include/roomrun/objects.hpp` (variety, enemies, rails, `grindRailGroup`), `roomrun/src/assembler.cpp` and `roomrun/include/roomrun/params.hpp` (a rail-share cost, off), `roomrun/tests/test_main.cpp`, `roomrun/tools/roomrun_batch.cpp` (`--variety`), `roomrun/examples/batch_1000.txt`, new `roomrun/examples/variety_500.txt`.
- **OpenSA2 files:** `runtime/tweaks/vr-sonic.c` (no assist, throws, hand yaw, view-change put-back, `course_shown`), `host/sa2_vr_hands.h` (version 11), `host/xr/xr_gpu.cpp`, `host/xr/xr_roomrun.cpp`, `host/xr/README.md`, `docs/vr-roomrun-objects.md`. No other upstream file.
- **Checks:** roomrun tests (16345 checks); 1000-room batch: generation unchanged, objects line changed (more grind rails, springs now include hop springs); test_tweaks 137; test_stream 147; public check 0 failures; flat `SA2_SELFTEST_OK` (menu at tick 3580); `RELEASE_BUILD_OK`, `XR_BUILD_OK`.

## Walk-through window (portal), throws and jumps into the window, objects' size (2026-11-21)

See `host/xr/README.md`, "Walk-through window (portal)".

- **What:** the VR / 2D VIEW window in the stages is a portal into the level:
  - stereo through the glass, with five clip planes per eye (two-pass as D3D9 planes; single-pass in the
    stereo block's new rows);
  - the head can go inside: the world all round, the room through a stencil hole;
  - the reach-in grab of Sonic in 3D;
  - throws and jumps into the window from the room (`frame-rrobj2`).
  The objects' size was investigated, and no change was made: they are the game's size, and their collision
  is the game's fixed shape.
- **OpenSA2 files:**
  - new: `host/xr/xr_portal.{h,cpp}`, `host/xr/shaders/portal.{vert,frag}`;
  - changed: `host/xr/xr_gpu.cpp`, `host/xr/xr_gloves.{h,cpp}`, `host/xr/xr_stereo.{h,cpp}`,
    `host/xr/shaders/glove.{vert,frag}`, `host/xr/shaders/gen_spv.sh`, `host/xr/xr_glove_spv.h`,
    `host/xr/build.sh`, `host/xr/README.md`.
  - No upstream file: everything is under `host/xr`.
- **Checks:** `XR_BUILD_OK` against roomrun 0.2.0; the patched vertex modules pass `spirv-val` (vulkan1.3);
  `TEST_D3D9GPU_OK` 5445; `test_stream` 147; `test_tweaks` 137; flat `SA2_SELFTEST_OK` (offscreen); public
  check 0 failures.
- **Build note:** while another agent was mid-edit, `roomrun/src/generator.cpp` did not compile for a while.
  The XR build can take a snapshot instead with `SA2_ROOMRUN=/work/<dir>` (e.g. a `git archive HEAD roomrun`
  under `private/`).

## Portal fixes from the Frame's test (2026-11-22, `frame-portal1`)

See `host/xr/README.md`, "Fixes from the Frame's portal test".
- **Inside showed what's behind the window:** the crossing now leaves on any return to the front side (stepping out over the
  frame's edge kept the head "inside", and the next crossing turned that into the view from behind).
- **Drops and throws snapped to the floor:** the flat floor's command 2 now carries the hand's height, and vr-sonic lets him go
  there.
- **No course in a 2 × 2 m area at 15 cm:** the generator's last-resort loop now steps down from 1.5 m to 0.6 m. The player tries
  8 more seeds before keeping a fallback, then grows the rectangle.
- **Files:**
  - OpenSA2: `host/xr/xr_portal.{h,cpp}`, `host/xr/xr_gpu.cpp`, `host/xr/xr_roomrun.{h,cpp}`, `host/xr/README.md`,
    `runtime/tweaks/vr-sonic.c`. No upstream file.
  - Root: `roomrun/src/generator.cpp`, `roomrun/src/skeleton.cpp`, `roomrun/include/roomrun/skeleton.hpp`,
    `roomrun/examples/batch_1000.txt`.
- **Checks:** roomrun tests pass; the 1000-room batch gives 1000/1000 at both scales, 100% completable. `test_tweaks` passed;
  public check 0 failures; `RELEASE_BUILD_OK` (game) and `XR_BUILD_OK`.

## Portal and Room Run, second Frame test (2026-11-22, `frame-place1`)

See `host/xr/README.md`, "The Frame's second portal test" and "Room Run objects at the toy course's size".
- **Crossing:** latched. In through the opening from the front, out only back through it; round the frame keeps you in. This
  replaces the first fix, which left on any return to the front.
- **Verticality:** `rr_generate` keeps the most vertical of the user's seed and the next 8 when the first isn't vertical enough
  (stacked share plus a quarter of the deck range, at least 0.2). The Frame's case moved from a flat circle to a 6.37 m coil.
- **Objects:** springs, dash panels and enemies at 0.5 of the game's size (`SA2_XR_ROOM_RUN_OBJECT_SCALE`). The player scales
  their marked draws; vr-sonic gives each a scaled private copy of its collision shapes. The channel is version 12
  (`rr_object_scale`).
- **Files:** `host/xr/xr_portal.{h,cpp}`, `host/xr/xr_gpu.cpp`, `host/xr/xr_roomrun.cpp`, `host/xr/README.md`,
  `host/sa2_vr_hands.h`, `runtime/tweaks/vr-sonic.c`. No upstream file.

## Portal and Room Run, third Frame test (2026-11-22, `frame-place2`)

See `host/xr/README.md`, "The Frame's third portal test".
- **Crash when Sonic left the course:** the game HeapFree'd our arena copy of a spring's or Beetle's collision shapes when the
  course was removed. Heap-owned shape arrays are now scaled in place; only static ones (the dash panel's) are copied.
  Verified offscreen with no trap: removal by drop in the window, pause-menu Restart, Exit to menu, all with objects alive.
  `SA2_VR_RR_COL_COPY=1` reproduces the old trap.
- **The game instead of the room near the glass from inside:** the hole's mark pass now writes its true depth, clamped to
  0..1, from the fragment shader (`portal.vert`/`portal.frag`). It no longer clamps the vertices' clip z.
- **No enemies:** the Beetle was spawned but its display was skipped by the game's camera-location visibility test. Room
  objects' display trampolines now move the camera location next to the object for the call, as they already did the view.
- **Files:** `host/xr/shaders/portal.{vert,frag}`, `host/xr/xr_glove_spv.h`, `host/xr/xr_gpu.cpp` (logging),
  `host/xr/README.md`, `runtime/tweaks/vr-sonic.c`. No upstream file.
- **Checks:** `test_tweaks` 137 passed; public check 0 failures; flat `SA2_SELFTEST_OK` (offscreen); `RELEASE_BUILD_OK`,
  `XR_BUILD_OK`, `GLOVE_SPV_OK`.

## Every character, room sounds (2026-11-22, `frame-place3`)

See `host/xr/README.md`, "Every character, and sounds in the room", and `docs/vr-game-hooks.md` section 13.
- **Characters:** vr-sonic wraps Knuckles_Main (`0x00728D70`, Knuckles and Rouge) and the walkers' main (`0x00741530`)
  as well as Sonic_Main. First person hides each character's head node.
- **Upstream file modified:** `runtime/tweaks/hide-upgrade-models.c` (already modified for Sonic). Its Knuckles, Rouge
  and both walker display wraps now hand the call to `sa2_vr_sonic_display` first when vr-sonic is on; behaviour with
  vr-sonic off is unchanged.
- **Sounds:** vr-sonic wraps `0x00438230` (volume) and `0x004383A0` (pan) so that sounds in the room are placed next
  to the camera.
- **Files:** `runtime/tweaks/vr-sonic.c`, `runtime/tweaks/hide-upgrade-models.c`, `docs/vr-game-hooks.md`,
  `host/xr/README.md`.
- **Checks:** `test_tweaks` 137 passed; public check 0 failures; flat `SA2_SELFTEST_OK` (offscreen); `RELEASE_BUILD_OK`.

## Performance round 3: placement, culling, call paths (2026-11-22)

Goal: a steady 60 fps on the Frame in City Escape's heavy parts (and Wild Canyon). The Frame logs (`frame-perf2`,
`frame-portal1`, `frame-place1..3`) show two heavy single threads, run as a pipeline: the game's guest threads (they
run one at a time under the guest lock) and the VR player's render thread. Each has to fit 16.7 ms on its own core.

**Measuring.** x86: the recorded City Escape session replayed headless (`tools/s7/replay_export.py`, VR settings, the
recorder on), pinned to one core, `perf stat` cycles and instructions over the whole run, "game CPU ms a tick" over ticks
1200-2700 (scratchpad `frameprof/replay_export.py`, a patched copy; `vb.sh`, with `fakeview` standing in for the VR
player's view file). Every replay below MATCHed the session. The Frame: `scratchpad/frameprof/frame_perf.sh` (headless
replays on the headset, a bundled arm64 perf, a live per-thread sampler with run-queue wait times).

**The Frame's CPUs** (`info.txt`): A520 cpu0-1 (capacity 216), A720 cpu2-4 (855, 3.15 GHz), A720 cpu5-6 (803),
X4 cpu7 (1024, but 3.05 GHz: rank by `cpu_capacity`, not the clock). Headless game CPU a tick in the heavy part:
X4 10.0 ms, unpinned 12.7-13.2, an A720 next to SteamVR's real-time render thread 12.6 (14.1 wall), an A520 49.

| Change | Where | x86 measured | Status |
|---|---|---|---|
| (a) CPU placement: game on the A720s off SteamVR's render-thread CPUs (2-3), launcher/relay/player on the X4 + the rest (5-7), never the A520s | `host/launch/sa2_cpu_place.h` (new), `sa2_launch.cpp`; test `host/launch/test_cpu_place.cpp`, `Documentation/scripts/tests/test_cpu_place.py` | the Frame's layout reproduced from a fake sysfs/proc (`CPU_PLACE_TEST_OK`); x86 (all 1024) places nothing | on by default on aarch64 and with `SA2_XR`; `SA2_CPU_PLACE=0` off, `SA2_GAME_CPUS`, `SA2_PLAYER_CPUS`, `SA2_CPU_AVOID` override; game.env's `SA2_GUEST_HOST_CPUS` wins |
| (b) the mono 2D window keeps no extra level pieces (`view_ok` 2) | `host/xr/xr_gpu.cpp` (`write_view_cone` only), `runtime/tweaks/vr-level-culling.c`, `host/sa2_vr_hands.h` (comment; layout unchanged, still v12) | game CPU 7.7-8.7 → 5.6-6.1 ms a tick (−28%), 785 → 562 KB and 15.2k → 9.8k D3D9 calls a frame | done; the portal keeps its cone (its view moves with the head) |
| (c) D3D9 dispatch: O(1) row table, one argument read, inline listed check | `runtime/sa2_d3d9.c` | instructions −8.7% (325 → 297 G), cycles about −5%; `d3d9_native` 5.9% → 2.3% of samples | done |
| (d) recorder: the per-call clear no longer zeroes the 8 blob slots | `host/sa2_present_rec.c` | `memset` 1.9% → 0.07% of samples; cycles about −2% (inside the noise) | done |
| (e) `fesetround` skipped when the mode is unchanged | `tools/s7/f80_shims.c` | none: the game changes the mode on nearly every call (`__fesetround` stayed at 2%) | dropped; that file is pinned by the arm64 receipts anyway |
| (f) `-mcpu=cortex_a720` for the aarch64 lifted image and runtime; the launcher refuses a CPU without its features | `private/sa2mr/arm64/lift_arm64.py` (new driver), `release_build_arm64.py` (writes `share/cpu-needs.txt`), `host/launch/sa2_cpu_needs.h` (new), `sa2_launch.cpp`, `build-env/arm64/build_release.sh` | not measurable on x86 or QEMU; QEMU `-cpu cortex-a76` is refused by name, a check inside the game came too late (its musl startup already died of SIGILL) | built; needs a Frame A/B |
| (g) the VR player's device keeps no draw-state census (`SA2_D3D9GPU_CENSUS=0`, set by the launcher for `SA2_XR` only) | `host/d3d9gpu/d9g.h`, `d9g_device.cpp`, `d9g_device_draw.cpp`, `sa2_launch.cpp` | the player replaying the City Escape stream (2,400 frames, synthetic eyes): instructions −5.7%, cycles 91.4 → 79.6 G (−13%) | done; the flat player still counts |
| (h) the VR player stretches its sound (up to 10% slower) while the device queue is under 40 ms (`SA2_AUDIO_STRETCH=1`, set by the launcher for `SA2_XR`) | `host/player/player.cpp`, `sa2_launch.cpp` | engaged under a dummy audio device | done; flat sound unchanged |

**All of it together** (the same replay, seated, no VR player's view file; two runs each): game CPU 8.9-9.9 → 8.1 ms a
tick, instructions 323-329 → 294-295 G (−9.6%); with the mono window's view, 5.7 ms a tick.

**Dropped after measuring:** memfast (the inlined guest-memory fast path): x86 −2.6% cycles inside the noise, the Frame
12.65 vs 12.95 ms a tick, and the binary 34% larger. On the Frame the memory accessors are a quarter of the game's
samples, but the cost is the loads' cache misses, not the call.

**Left (largest first):** the CRI reverb (7.5% of the game on the X4, 13.6% on the A520 that took the audio thread);
the player's per-draw work (`prepare_draw`, the XR wrapper's copy of each draw's `Values`, a `tan` per eye per draw in
`eye_P`: proposals for the xr_gpu.cpp owner); hot-function ordering for the 147 MB of code (the aarch64 build also
pays `__AArch64AbsLongThunk` veneers on calls to the runtime, about 1.3%).

**Checks:** `TEST_D3D9GPU_OK 5445`, `test_stream` 147, `test_tweaks` 137, `CPU_PLACE_TEST_OK`, the relay tests, the
d3d9 closed-world tests, the recorder tests (`test_s5_stream`: the 14 known zstd-binary failures; `test_s5_ctab_recorder`'s
hard-coded `/workspace` zig path is environmental), flat `SA2_SELFTEST_OK` (offscreen, menu at tick 3586), public check 0
failures. Upstream files edited this round: `runtime/sa2_d3d9.c`, `host/sa2_present_rec.c`, `host/launch/sa2_launch.cpp`,
`host/d3d9gpu/d9g.h`, `host/d3d9gpu/d9g_device.cpp`, `host/d3d9gpu/d9g_device_draw.cpp`, `host/player/player.cpp`
(each gated or behaviour-neutral for the flat player).
- **arm64:** `release_build_arm64.py --only game,players,launch,assemble,package`: `RELEASE_BUILD_ARM64_OK`, tarball
  sha256 `2ff0283c…` (not deployed). Under QEMU (8 cores, offscreen): `SA2_SELFTEST_OK` with the cortex_a720 game
  (menu at tick 3572); with `QEMU_CPU=cortex-a76` the launcher refuses by name. A generic-CPU game of the same tree for
  the Frame A/B: `private/generated/sa2mr-perf/rel-a64-generic/bin/sa2-game`.
- **The Frame after this build:** take `SA2_GUEST_HOST_CPUS` out of its `share/game.env` and `taskset` out of `FRAME_ENV`:
  the launcher places both (its log says "CPU placement: ..."). The aarch64 game is built for `cortex_a720`; the launcher
  refuses a CPU that lacks one of its features by name (`SA2_CPU_CHECK=0` tries anyway).

## Performance round 4: Wild Canyon (2026-11-22, Frame session `frame-perf4`)

**The Frame session** (auto-placement working: game 2-3, launcher/relay/player 5-7): 58.6 fps; Wild Canyon (level 16,
Knuckles, seated with some first person and 2D) dipped to 47.7-55.8 fps, with the game's CPU at 13-16.8 ms a frame on
the A720s and the relay rarely waiting on the player: the game is the limit there. 650-930 KB and 9.7-12.7k D3D9 calls a
frame.

**Measuring Wild Canyon on x86:** the City Escape export replayed with `SA2_PRACTICE=16` and vr-level-culling's new
measurement hook `SA2_VR_CULL_TEST_TP=F:x,y,z;...`, which puts player 1 at the Frame session's own places (from its
`game camera record` lines; scratchpad `wc_tp.txt`) at given frames; 760-800 KB and 10.5-11.7k calls a frame, close to the
Frame's heavy parts. Every replay MATCHed.

| Finding / change | Measured (x86, game CPU ms a tick over ticks 1200-2700) |
|---|---|
| **draw-distance** (in the Frame's hand-kept game.env, not the VR release's): objects load and run at twice the distance | 7.33 at 2× (the tweak), 6.56 at 1.5×, 6.17 at 1.25×, 5.2 off: the tweak is a third of Wild Canyon's game frame. New `SA2_DRAW_DISTANCE_SCALE` (1-4, default 2 as before) in `runtime/tweaks/draw-distance.c` |
| vr-level-culling in Wild Canyon (the ground-list path, `0x0047CAE0`): only 12 COLs a frame added, and the land draws' model clip off | the ground list now takes the seated view cone and the 2D window's own view; the land draws keep the model clip on in a seated level and ask the cone per model (vr-object-culling's frustum-test wraps call `sa2_vlc_model_kept`): 25 models a frame outside the game's view, 12 kept. Small in Wild Canyon (about −4%); flat-equivalent cost is within noise of VR seated |
| the walk-through window from outside: the cone narrowed to the pyramid from the head through the glass (hands file **version 13**, `view_cone_deg`; margin `SA2_XR_PORTAL_CONE_MARGIN`, 10°) | City Escape live under Monado: 650 → 380 KB a frame, game CPU 7.2 → 5.5 ms (−23%); the portal's eye dumps pixel-identical (3 frames) |
| VR player: `eye_projection`'s tangents memoised per view; `draw_inner`'s copy of a draw assigned over a kept one (no allocation per draw) | the player on the City Escape stream: instructions −6.2%, cycles −5%; eye dumps pixel-identical. With round 3's census off, about −17% in all |
| launcher: `SA2_CPU_PLAN=game-top` (the game on the X4, the player on 2-6) | for the Frame A/B: the game is the heavier thread in Wild Canyon (round 3: the X4 runs it 21% faster) |
| hot-function link order (`private/sa2mr/order_link.py`, objects hottest first) | x86: iTLB misses −14%, cycles unchanged; an arm64 build (`rel-a64-ord`) for the Frame A/B (it also removes far-branch veneers to the runtime) |

**Not done:** the CRI reverb (8.8% of the game in Wild Canyon, on the audio thread): a bit-exact NEON body is a project
of its own (the differential harness `tools/s8/native_kernels/cri_dsp_diff.c` has to hold it); `prepare_draw`'s
per-draw zeroing of uniform slots (`pack_uniforms`, about 2% of the player).

**Files:** `runtime/tweaks/vr-level-culling.c`, `runtime/tweaks/vr-object-culling.c`, `runtime/tweaks/draw-distance.c`
(upstream tweak: the scale setting), `host/sa2_vr_hands.h` (v13), `host/xr/xr_gpu.cpp` (`write_view_cone`,
`portal_cone`, `eye_projection`, `draw_inner`), `host/launch/sa2_cpu_place.h`, `host/launch/test_cpu_place.cpp`,
`host/launch/sa2_launch.cpp`; private drivers `private/sa2mr/order_link.py`.
**Checks:** `test_tweaks` (with draw-distance's) 150 passed incl. `CPU_PLACE_TEST_OK` and the relay tests;
`TEST_D3D9GPU_OK 5445`; `test_stream` 147; flat `SA2_SELFTEST_OK` (offscreen, menu at tick 3575); `XR_BUILD_OK`;
public check 0 failures.
- **The Frame's game.env:** drop `draw-distance` from `SA2_TWEAKS` (or add `SA2_DRAW_DISTANCE_SCALE=1.25`) for Wild Canyon.
