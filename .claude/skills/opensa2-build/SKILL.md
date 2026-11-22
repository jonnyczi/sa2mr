---
name: opensa2-build
description: Rebuild OpenSA2 (the SA2 static recompilation) from the user's Steam copy on this NixOS machine - translation, host deps, x86_64 release, XR release - and verify each stage with its receipt. Use when asked to build, rebuild, update or fix the OpenSA2 build, after pulling upstream OpenSA2 changes, or when a build step or receipt fails.
---

# Build OpenSA2 (x86_64)

Everything runs in the Debian 13 container through `build-env/run.sh '<cmd>'`, which mounts `external/OpenSA2` at `/work`. Start every command with `source tools/env.sh &&`. Run long steps in the background with logs in the scratchpad. `BUILD_NOTES.md` has the full history; read its "Deviations" section first.

## Only rebuild what changed

| Changed | Rerun from |
|---|---|
| upstream translation tools / pins | step 2 |
| runtime/, tweaks, game-side C | step 5 (release link) |
| host/player, host/d3d9gpu | step 5, or just the player build script |
| host/xr only | step 6 |

## Steps and their pass markers

1. **Inputs**
   - `private/exe/sonic2app.exe`: sha256 `4b1e09d7…`.
   - `private/exe/sonic2app-unpacked.exe`: `c97b116f…`. This is Steamless 3.1.0.5 run on the host via `steam-run python3 "~/.local/share/Steam/steamapps/common/Proton 11.0/proton" runinprefix Steamless.CLI.exe Z:<exe>`, then `tools/normalize_unpacked.py`.
   - The tools under `private/predecessor-tools` and `private/lifters/remill` are already installed.
2. **Function map:** `.venv/bin/python Documentation/scripts/s1_function_map.py` (Ghidra, about 6 min). Its sha256 differs from the pin (known, cosmetic); check 12,683 functions in the receipt.
3. **Translate:** `.venv/bin/python tools/s4/rebuild_image.py --jobs 20 && .venv/bin/python tools/s4/lift_ir_receipt.py`. Needs **`LIFT_IR_OK`**.
4. **Tables:**
   ```
   .venv/bin/python tools/s4/mb_edges.py --tag next+allarms --lift-tag next --arms-tag next
   .venv/bin/python tools/s3/whole_image/gen_table.py --tag next+allarms --out private/generated/s3/link/next+allarms/function_table_whole.c
   .venv/bin/python tools/s7/drm_unwrap.py --companion
   .venv/bin/python sa2mr/finalize_extras.py                # REQUIRED: rebuild_image leaves the extras state without "objects"
   .venv/bin/python tools/s8/perf_f80.py build --target x86_64-linux-musl --mode double --out private/generated/s8/pc-double/linux-obj --jobs 22
   ```
5. **Host deps** (once): `SA2_BUILD_JOBS=8 bash tools/s5/build_host_deps.sh --fetch`. The ALSA headers `.deb` must already be in `private/s5-tools/dl`. The SDL3 hash differs from upstream, so it was repinned locally in `tools/pins.json`.
   **Release:** `.venv/bin/python tools/s7/release_build.py` needs **`RELEASE_BUILD_OK`**. Output: `private/generated/s7/release/linux-x86_64`.
6. **XR release:** `bash host/xr/build.sh`, giving `private/generated/xr/release/linux-x86_64`. The flat d3d9gpu must still pass `bash host/d3d9gpu/build.sh` (**`D3D9GPU_BUILD_OK`**) and its tests (**`TEST_D3D9GPU_OK`**).
7. **Install and smoke test (host):** `steam-run bin/sa2-install --source folder "<SA2 dir>" --yes` (`SA2_INSTALL_OK`), then `build-env/play.sh --selftest`, which needs **`SA2_SELFTEST_OK`**. This opens a window briefly; tell the user.

## Rules

- Never commit generated or translated output, or game files. They live in ignored `private/`.
- Any upstream file you edit goes into `BUILD_NOTES.md` under Deviations, with the reason.
- If a receipt fails, find the first differing module or stage and report it; don't paper over it.
- aarch64 is a separate flow: see the `frame-deploy` skill.
