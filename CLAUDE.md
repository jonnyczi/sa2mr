# CLAUDE.md

Guidance for Claude Code working in this project.

## What this is

A VR / mixed-reality mod of **Sonic Adventure 2** for the **Valve Steam Frame**, inspired by `lucas-martinic/mario64mr`. Meta Quest comes later, once the Frame version works.

- **Product vision and decisions:** `BRAINSTORM.md`. Headline: "Chao in Your Room" (pull a Chao out of a garden portal into your room). Second: "Room Run", an auto-generated toy-scale course fitted to the player's traced boundary. Also: diorama/portal views of real stages.
- **Build history and deviations from upstream:** `BUILD_NOTES.md`. Read it before touching the build.
- **Game base:** `external/OpenSA2`, a git clone of `kittycache9/OpenSA2`, a GPL-3.0 static recompilation of the 2012 Steam PC `sonic2app.exe`. The game process records D3D9 calls into a stream and a separate player process renders it (`host/d3d9gpu`, SDL3 GPU and Vulkan). Our VR player is `external/OpenSA2/host/xr/` (see its `README.md`).
- **Room Run generator:** `roomrun/`, a standalone C++17 library plus CLIs, later linked into the XR host.

**Git (user decision, 2026-11-21): commit your work at verified, stable points.**
- **The project root** is a git repo on `main`. `media/` (game screenshots), toolchains, logs and everything under `external/` but the OpenSA2 submodule are gitignored.
- **Push the root's `main` to `origin` (`git@github.com:jonnyczi/sa2mr.git`, public) after each commit** (user decision, 2026-11-22).
- **`external/OpenSA2`** is a **git submodule** of the root (`.gitmodules`: `https://github.com/jonnyczi/OpenSA2.git`, branch `sa2mr`; `ignore = dirty`), the same checkout as before, with its own `.git` (54 GB ignored `private/`, local `pr/*` branches). Our changes go on its **`sa2mr`** branch; `main` tracks upstream.
  - Follow its `AGENTS.md`: no tool or assistant attribution trailers, and run `python3 tools/public_check.py` before every commit.
  - **Push `sa2mr` to the public fork** (remote `jonnyczi`, `github.com/jonnyczi/OpenSA2`) once `public_check.py` passes. The `pr/*` branches and `main` are not pushed there.
  - **The root pins the submodule to a `sa2mr` commit.** When you commit fork changes, push them, then bump the pin in the root (`git add external/OpenSA2`, or `git update-index --cacheinfo 160000,<sha>,external/OpenSA2`) and commit that too. CI checks out the pinned commit.
- **CI** (`.github/workflows/frame.yml`, the plan's Phase 3) builds the Frame toolchain and app and publishes releases on `v*` tags. It never has the game. `tools/release/sa2mr_scan.py` runs before every upload; `tools/release/root_check.py` guards what the root tracks (no `media/`, no binaries, `external/` only the submodule).
- **Never commit proprietary code or assets:**
  - no SEGA exe, or anything generated or translated from it;
  - nothing under OpenSA2's `private/`;
  - no extracted meshes, textures or shaders;
  - no game screenshots (and never in a release asset: upstream tracks `docs/screenshots/*.png` of the game in the fork, so `make_kit.sh` leaves them out and the scanner refuses images);
  - not the regenerated `Documentation/receipts/s1-function-map-v1.json`.

  Check `git diff --cached --stat` before each commit.

## User decisions (don't relitigate)

- **Steam Frame: native Linux ARM64 only. No APK or Lepton route.** Quest will need an APK later, so keep the XR host portable: pure Vulkan plus core OpenXR, with Frame-specific behaviour behind runtime capability checks.
- **Develop and test natively on x86_64.** Cross-compile aarch64 on the x86 host. Use QEMU arm64 emulation **only for occasional verification**, capped (for example `taskset -c 0-7`). It is very slow and loads the machine.
- **Sonic is 4–6 in (10–15 cm) tall** at toy scale (SA2 Sonic ≈ 10 units, so ≈ 0.012 m/unit).
- **Room Run auto-generates by default.** Manual building and editing (grind-rail pen) is optional.
- **Must work with the stock grayscale passthrough.** The Arcturus Vision color camera is only an optional enhancement.
- **Legal model:** never ship or commit SEGA code or data, or generated/translated output. The user supplies their own Steam copy, and the hashes are verified.

## Machine and environment (NixOS, no host toolchain)

- **x86_64 builds:** Docker image `opensa2-build` (Debian 13, matching OpenSA2's `tools/pins.json`), from `build-env/Dockerfile`. Run things with `build-env/run.sh '<cmd>'`, which mounts the repo at `/work`. Both containers also mount `roomrun/` read only at `/roomrun`, so `host/xr/build.sh` links the generator into the VR player. Always `source tools/env.sh` first.
- **aarch64 builds:** `build-env/cross/` (cross container `opensa2-cross`) and `build-env/arm64/` (`build_release.sh`, `frame_deploy.sh`, sysroot, pins, `test/` QEMU bench). The drivers are tracked in the fork at `external/OpenSA2/sa2mr/arm64/` (`private/sa2mr/arm64` and `private/sa2mr/finalize_extras.py` are symlinks to them, for old paths).
  - Output: `external/OpenSA2/private/generated/s7/release/linux-aarch64/` and `sa2mr-linux-aarch64.tar.gz`.
  - The lifted image is built for Armv8.2-A (`-mcpu=generic+v8_2a`) by `sa2mr/arm64/lift_arm64.py` and linked hottest-functions-first (`hot_order.py` writes the tracked `sa2mr/arm64/hot/hot_order.txt` from the private profiles in `private/sa2mr/hot-profiles/`); the launcher places the game on the X4 and the player on the other big cores itself (BUILD_NOTES "Performance round 5").
  - **aarch64 remill** (for translating on the Frame): `build-env/remill-arm64/run-cross.sh WORKDIR` (~8 min); `check/compare.sh` diffs its lifted IR against the x86 package (BUILD_NOTES "remill 5.0.7 for aarch64").
  - **On-device install (in progress):** `platforms/steam-frame/` holds `sa2mr_build.py` (the builder that translates the user's copy on the Frame, steps 0–14, resumable), `add_to_steam.py`, `sa2mr-vr.sh` and the VR `game.env`; tests in `platforms/steam-frame/tests/`. `build-env/frame-kit/make_kit.sh OUT` makes its source kit from the fork (deny-scanned). The trimmed map is `Documentation/sa2mr/function-map-trimmed.json` in the fork.
  - Passes `SA2_SELFTEST_OK` under QEMU. **Not yet run on a real Frame.**
- **Running host binaries** needs `steam-run` (FHS) and `VK_DRIVER_FILES=/run/opengl-driver/share/vulkan/icd.d/radeon_icd.x86_64.json`. Without it the Radeon 890M appears twice and d3d9gpu refuses to create a device. The scripts below set both.
- **SA2 game files:** `~/.local/share/Steam/steamapps/common/Sonic Adventure 2`.
  - `sonic2app.exe` sha256 `4b1e09d7…`.
  - Unpacked copy in `external/OpenSA2/private/exe/`.
- **Saves, config and sessions:**
  - `~/.local/share/sa2recomp/` (sessions under `sessions/`)
  - `~/.config/sa2recomp/sa2recomp.cfg`
- **aarch64 binfmt (QEMU, static, F flag)** is already set in the user's NixOS flake: `~/nixos/modules/hosts/jonnylaptop.nix`.
- **Monitors:** eDP-1 at 60 Hz and an Odyssey at 144 Hz (niri compositor). 60 fps content judders at 144 Hz; suggest 120 Hz.

## Common commands

```bash
build-env/play.sh [--selftest]          # flat game (x86_64 release); SA2_SELFTEST_OK = boots to menu
build-env/monado.sh &                   # simulated OpenXR headset (prints XR_RUNTIME_JSON)
XR_RUNTIME_JSON=... build-env/play-xr.sh   # VR player (SA2_XR_MODE=diorama, etc.; see host/xr/README.md)
build-env/pace-stats.sh [N|session]     # frame pacing of recent sessions (fps, >25 ms, game-behind)
roomrun/build.sh                        # build + tests of the generator; roomrun/run.sh ./build/roomrun-gen ...
tools/release/ci_app.sh OUT             # the Frame app tarball, no game (SA2MR_OPENSA2=<fresh clone of the fork>)
python3 tools/release/sa2mr_scan.py --profile app|toolchain|kit PATH   # SCAN_OK before any upload
```

Use the project skills for the multi-step workflows: `opensa2-build`, `run-and-measure`, `xr-dev`, `frame-deploy`, `roomrun`.

## Working rules

- **Don't take over the user's desktop.** Running the game opens a visible window and plays sound. Say so before launching. Never move or focus the user's windows with `niri msg action ...`: `move-window-to-monitor` acts on the *focused* window, which once moved the user's terminal. For headless checks use `--selftest`, `MONADO_NULL=1`, or SDL's offscreen/dummy drivers.
- **Verify with evidence.** Use the receipts and markers OpenSA2 prints (`LIFT_IR_OK`, `RELEASE_BUILD_OK`, `SA2_SELFTEST_OK`, `D3D9GPU_BUILD_OK`, `TEST_D3D9GPU_OK`), session `player.err` / `pace.tsv`, and screenshots in `media/`. Report failures honestly.
- **In OpenSA2, prefer new files over editing upstream ones.** Record every upstream file you modify in `BUILD_NOTES.md` (or `host/xr/README.md` for the XR hooks). Keep the flat player's behaviour unchanged; XR is gated by `SA2_XR=1` / `bin/sa2_player_xr`.
- **Keep docs current.** When a step, decision or deviation changes, update `BUILD_NOTES.md`, `BRAINSTORM.md` and this file in the same turn.
- **Long builds:** run them in the background and redirect logs to the scratchpad. The translation takes about 6 min; the double-f80 object build and the release link take a few minutes each.

## Reference projects (the user's own, in `~/Documents/projects/`)

- **`xelee`:** proven native Steam Frame build and deploy.
  - `tools/package_steam_frame.sh`, `tools/frame_deploy.sh`, `docs/steam-frame.md`.
  - OpenXR patterns in `extern/aurora/lib/xr/xr.cpp` and `xr_policy.cpp`.
  - Frame-mimic OpenXR layer: `extern/aurora/tests/xr_sim_layer`.
- **`mario64mr`:** camera/projection capture and per-eye rendering (`native/fullgame/unity_bridge.c`, `unity_mr.c`); room collision injection (`unity_room.inc.h`).
- **`VivecraftMod-openxr`:** another OpenXR app with passthrough on the Frame.
- **Frame SSH host:** `steamos@frame.lan`.
