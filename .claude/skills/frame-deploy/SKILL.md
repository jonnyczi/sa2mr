---
name: frame-deploy
description: Build the native aarch64 Linux release of OpenSA2 (cross-compiled on x86_64 against Valve's sniper arm64 SDK sysroot), verify it, and deploy/run it on the Steam Frame over SSH. Use when asked to build for ARM64 or the Steam Frame, package a release, deploy to or test on the headset, or debug a Frame-only issue.
---

# Steam Frame: aarch64 build and deploy

**Native Linux ARM64 only. Never the APK/Lepton route** (user decision). Model: `../xelee/tools/package_steam_frame.sh` and `frame_deploy.sh`, and `../xelee/docs/steam-frame.md`.

## Build (cross-compiled on this x86_64 host)
- **Prerequisite:** the x86_64 build is done (see `opensa2-build`). The lift, tables and the x86 glslang are reused.
- `build-env/arm64/build_release.sh` builds the images, the sniper arm64 sysroot (`build-env/arm64/sysroot`), the arm64 host deps, the lifted image and the release.
  - Output: `external/OpenSA2/private/generated/s7/release/linux-aarch64/` and `sa2mr-linux-aarch64.tar.gz`.
  - Shell into the cross toolchain with `build-env/cross/run.sh '<cmd>'`. The arm64 pins override is `build-env/arm64/pins.json`.
- **Packaging rules (xelee):**
  - Bundle the `ldd` deps into `lib/`, except glibc/libm/libdl/libpthread/librt/libvulkan/libz.
  - No symbol newer than GLIBC_2.31.
  - Ship `lib/openxr/libopenxr_loader.so.1`. Steam's environment provides the SteamVR runtime.
  - Split out the debug info.

## How it's built (verified 2026-11-21)
- **`sa2-game`** is the translated image (`perf_f80.py --target aarch64-linux-musl --mode double`, about 8 min), linked **static musl** with zig. It has no glibc dependency.
- **Players, launcher and installer** use Debian's `aarch64-linux-gnu-g++-14` against the sniper sysroot, with static libstdc++. The newest symbol is GLIBC_2.30.
- **`sa2mr/arm64/`** (tracked in the fork; `private/sa2mr/arm64` is a symlink to it) holds the drivers:
  - `release_build_arm64.py`, which reuses `release_build.py`'s steps. Use `--only tables,game,players,install,launch,assemble,package` to rerun parts.
  - `build_deps.sh` (SDL3, DXVK, zstd, OpenXR loader).
  - `xwrap/`: compiler wrappers plus the CMake/meson cross files. Its `bin/` and `lib/` symlinks are made by `xwrap/links.sh` (build_deps.sh and release_build_arm64.py run it): the fork's public_check refuses tracked symlinks.
  - `release_build_arm64.py --no-game` assembles and packages without `bin/sa2-game` (the installer's app); `--f80-cached` links from objects whose folded IR is gone (the on-device relink).
  - `hot/hot_order.txt` is tracked (only `sub_` names, user decision); the perf profiles stay in `private/sa2mr/hot-profiles/`.
- **Single steps:**
  - `build-env/cross/run.sh 'bash sa2mr/arm64/build_deps.sh'`
  - `build-env/cross/run.sh 'source tools/env.sh; .venv/bin/python sa2mr/arm64/release_build_arm64.py --only players'`
- **Gotchas already solved:**
  - GCC, not clang: the tree's duplicate-symbol check is GCC-specific.
  - The cross toolchain's own glibc is hidden.
  - `-fno-use-linker-plugin`.
  - `-lpthread -lrt` and `-lstdc++ -lgcc` at link time.
  - wayland-scanner 1.18 to match the SDK.
  - The sysroot is mounted under `/usr/lib/gcc/` so the header-provenance check accepts it.
- **Edited upstream files (x86-only FPU guards):** `host/d3d9gpu/test/shaderc.cpp`, `host/dxvk_smoke.cpp`.
- **Last QEMU result:** `TEST_D3D9GPU_OK 5445`, `SA2_INSTALL_OK`, and `SA2_SELFTEST_OK` (title at 3085, menu at 3572), with lavapipe in about 27 min.

## Verify without the headset (sparingly)
- `build-env/arm64/build_release.sh test` runs install plus selftest **under QEMU**. It's slow and CPU-heavy, so only do it for occasional verification, never as the dev loop.
- Cap it with `taskset -c 0-7` and tell the user it's running. Everyday testing happens on x86_64.

## Deploy and run on the Frame (`FRAME_HOST`, default `steamos@frame.lan`; Developer Mode + SSH)
```
build-env/arm64/frame_deploy.sh deploy     # tarball -> ~/Applications/sa2mr (keeps an edited share/game.env)
build-env/arm64/frame_deploy.sh install    # bin/sa2-install --source steam --yes (SA2 installed via Steam on the Frame)
build-env/arm64/frame_deploy.sh selftest
build-env/arm64/frame_deploy.sh run [args] # inside the running Steam client's env (display, audio, XR runtime)
build-env/arm64/frame_deploy.sh log | fetch DIR | stop
```
- **The Frame is a real device the user is wearing.** Confirm before running things on it, and pull logs with `fetch` instead of guessing.
- **Once the user has reported on a test, stop the game** (`frame_deploy.sh stop`) unless you still have questions that need it running. This is a standing user instruction.
- **Verified on the real Frame (2026-11-21):**
  - SteamOS 0.4.5.
  - `SA2_INSTALL_OK` from a copied game folder: `rsync` the PC's SA2 folder to `~/sa2-gamefiles` (about 28 min over Wi-Fi), then `bin/sa2-install --source folder ~/sa2-gamefiles --yes`. Use this if SA2 isn't installed via Steam there.
  - `SA2_SELFTEST_OK`.
  - Turnip Adreno 750 has no 16/8-bit buffer access; the 32-bit fallback is automatic.
  - **VR works** on SteamVR/OpenXR 2.18.2 with the Frame controller bindings. Launch with `FRAME_ENV="SA2_XR=1 [SA2_XR_MODE=diorama]" build-env/arm64/frame_deploy.sh run`.
  - Settings are in the Frame's `~/Applications/sa2mr/share/game.env`. `deploy` keeps an edited copy. Use it for `SA2_TWEAKS`, `SA2_FOV_SCALE`, `SA2_REC_CAMERA=1`.
  - **Flat mode has no gamepad on the Frame:** the controllers act as laser pointers, which shows the touch overlay. Use VR for input.
  - Controller input needs the OpenXR session *focused*: the SteamVR dashboard must be closed.
- **CPUs (measured 2026-11-22):** A520 cpu0-1 (capacity 216), A720 cpu2-4 (855, 3.15 GHz), A720 cpu5-6 (803), X4 cpu7
  (1024, 3.05 GHz). SteamVR's RenderThread is SCHED_RR 80 on 4-6. The launcher places the game on the X4 (7) and itself, the
  relay and the player on 2-6 (log "CPU placement"; round 5; `SA2_CPU_PLAN=game-mid` is the older game 2-3 / player 5-7);
  don't put `SA2_GUEST_HOST_CPUS` in game.env or `taskset` in `FRAME_ENV`. The aarch64 game is built for Armv8.2-A
  (`-mcpu=generic+v8_2a`, `lift_arm64.py`; cortex_a720 measured no faster) and linked hottest-first (`hot_order.py`, −4%
  on the Frame); the launcher checks `share/cpu-needs.txt` against the CPU's HWCAPs and refuses by name
  (`SA2_CPU_CHECK=0` tries anyway). A check inside the game is too late.
- **Wild Canyon (round 4):** game-CPU bound on the A720s (13-17 ms a frame) with `draw-distance` on; `frame_perf.sh wcsuite`
  is its headless A/B (placement, draw distance, alt builds).
  - **Results (frame-perf5, 2026-11-22), game ms a tick on an A720:**
    - draw distance 2×: 11.5; 1.25×: 9.2; normal: 8.4. On the X4 at 2×: 9.7.
    - The generic armv8 build is the same as cortex-a720 (so `-mcpu` gains nothing). The hot-function-ordered build is −4%.
  - **Live Wild Canyon with normal draw distance and auto-placement: 59.9 fps, 24 frames over 25 ms in 23k, the relay at 60.0.** This is the working Frame setup (the user's decision: normal draw distance).
  - **City Escape through the truck chase with 2160² eyes and the player's draw thread (round 10, frame-r10, 2026-11-22): 59.8 fps, the worst 5-second window 57.**
    - The player main thread uses about 227 ms/s and the draw thread about 231 ms/s, both on 2-3. Before the split, the main thread used 860-930 ms/s.
    - The game's per-frame CPU median is about 9 ms, with at most 10 frames over 16.7 ms in a 600-frame window.
    - The lesson from rounds 6-9: the dips came from the player's main thread being CPU-bound, not from the GPU. To find a bottleneck, join `game_frames.tsv` with `pace.tsv` before blaming the GPU.
- **Profiling on the headset:** scratchpad `frameprof/frame_perf.sh` (push, info, suite, live, fetch; a bundled Debian
  arm64 perf). Ask before running anything on the Frame.
- **Frame specifics to expect:**
  - Mesa Turnip on Adreno 750 with a single queue (`tu_emulate_second_queue`).
  - SteamVR picks 72/90/120/144 Hz. **The user must set 120 Hz (or 60 Hz) in SteamVR** for the 60 fps game; 72 Hz judders (user-verified 2026-11-21). `XR_FB_display_refresh_rate` offers the app only 72 Hz.
  - The Frame's `game.env` needs `vr-object-culling` in `SA2_TWEAKS` (added 2026-11-21).
  - Passthrough is grayscale (`ALPHA_BLEND`).
  - Controller profile `valve/frame_controller_valve` (bumper/shoulder name variants).
  - No hand tracking, no scene mesh.
  - **CPU (profiled 2026-11-21, City Escape replay, heavy part):** cpu0-1 Cortex-A520 (2.27 GHz), cpu2-4 A720 (3.15 GHz), cpu5-6 A720 (2.96 GHz), **cpu7 X4 (3.05 GHz, capacity 1024) = the fastest core**. The game's CPU time per frame: default scheduling 12.7-13.0 ms, pinned to cpu4 12.6 ms, **pinned to cpu7 10.0 ms (-22%)**, pinned to cpu0 49 ms. A memfast build made no difference. The CRI reverb (`__wrap_sub_843460`) is 13.6% of the game's CPU on the Frame. **Pinning live made things worse (2026-11-22):**
- Pinning the game to 7 alone gave 55 fps, the game behind on most frames.
- Adding `taskset -c 0-6` for the launcher and player gave 38.9 fps: the player's main thread ran on the A520s more than half the time.
- The player main thread is the other heavy single thread, and the compositor's RT RenderThread sits on 4-6.
- The default (no pin) holds about 59 fps. If you pin, rank cores by `cpu_capacity`, never by max clock, and keep both heavy threads off 0-1. **The game on 2-3 with the player under `taskset -c 5-7` works** (frame-place2): 58.8 fps over a 5-minute session, the relay at a steady 60.0 fps, the game's CPU 8-10 ms a frame. The user reported "much better" frame rate and music. That run's `game.env` keeps `SA2_GUEST_HOST_CPUS=2-3`; launch with `FRAME_ENV="SA2_XR=1 SA2_XR_MODE=auto taskset -c 5-7"`.
- Turnip renders the VR player direct to memory (`TU_DEBUG=sysmem`, set by the launcher for the player only on arm64;
  `SA2_XR_TU_SYSMEM=0` off): 40-50% less GPU a frame than binning (round 13). For the player's GPU time alone, headless:
  scratchpad `frameprof/frame_perf.sh gpubench` (variants; delete its 2 GB stream in `~/sa2perf/work` afterwards).
- Who is late (rounds 8-9): the launcher's second "relay perf" line gives the game's per-frame CPU (p99, max, frames
  over 16.7 ms) and the session's `game_frames.tsv` every frame. Join it with `pace.tsv` by frame: arrival to
  `ready_s` about 34 ms is normal (lead 2); more, with the game's CPU under 16.7 ms, is the player (frame-r8's dips).
  Dynamic resolution is off by default (`SA2_XR_RES_DYNAMIC=1`). The player's main thread is narrowed by
  `SA2_XR_MAIN_CPUS` (2-3 by default; the X4 with `SA2_CPU_PLAN=game-mid`). Watch the clocks in a live capture:
  the A720s ran at about 2.6 GHz of 3.15 when hot.
- Late GPU frames (round 7): the VR player skips frames the GPU has no room for instead of waiting, so the game and its music
  should hold 60 even when the headset's GPU runs out; check player.err "frames the GPU had no room for" and "dynamic
  resolution: ... the runtime throttled". `SA2_XR_SKIP=0 SA2_RELAY_LEAD=1` restores the old lockstep for an A/B.
- Choppy music = fps below 60 (the game makes one frame of sound per Present). The suite kit is in the scratchpad `frameprof/`, and results in `frame-perf3/`.

Record anything new you learn about the Frame in `BUILD_NOTES.md` (ARM64 section) and in this skill.
