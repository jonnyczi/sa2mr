# Linux arm64: a native build reaches the main menu on a Steam Frame. Is a linux-aarch64 release target wanted?

The status page describes Linux arm64 as "cross builds and registered replays under QEMU" and makes no claim for native hardware. Here is evidence from one native device, and a question about whether you want a release target for it.

## What was built

- **Game:** `perf_f80.py build --target aarch64-linux-musl --mode double`, 24,040 of 24,040 modules. It is linked static the way `arm64_link.py` links it, through `release_build`'s game step with the aarch64-linux-musl triple and `-ffp-contract=off`.
- **Players, installer and launcher:** cross-compiled with GCC 14 for aarch64 against the Steam Linux Runtime 3 ("sniper") arm64 SDK sysroot, pinned by image digest, with libstdc++ linked static.
- **Dependencies:** SDL3 3.4.16 with the project patch, DXVK d3d9 and zstd, built for arm64.
- **Checks:**
  - every ELF is AArch64;
  - the game is static;
  - nothing needs a glibc symbol newer than 2.31;
  - no system library is bundled.

## Evidence, in the project's claim vocabulary

- **Under QEMU user emulation:** `TEST_D3D9GPU_OK`, `SA2_INSTALL_OK` (4,191 of 4,191 files) and `SA2_SELFTEST_OK`.
- **On a Steam Frame** (SteamOS, aarch64, Adreno 750 on Mesa Turnip):
  - `SA2_INSTALL_OK`;
  - `SA2_SELFTEST_OK`. The build **reaches the main menu** in the self-test.
  - In a flat interactive session, the renderer held about 60 fps over 6,000 frames with PipeWire audio.
- **Not claimed: "plays on named devices".** In a desktop window the headset's controllers act as pointers, so input arrives as touches, the touch overlay appears, and there is no gamepad.
- **Turnip:** it has no `uniformAndStorageBuffer16BitAccess` or `8BitAccess`. d3d9gpu's 32-bit state-word fallback engaged on its own and drew correctly. This is the same message as in #36.

## The question

`release_build.py` has no `aarch64-linux` target. Would you take one? If so, which runtime or sysroot would you want it to target? The build above targets glibc 2.31 or later on aarch64. That covers SteamOS on arm64 and should also cover other aarch64 distributions (Asahi, Raspberry Pi 5, Snapdragon laptops), though I have not run it on those.

If yes, I can send a PR of roughly 200–400 lines plus docs:

- a cross toolchain wrapper;
- a dependency build;
- arm64 library pins;
- the ELF, glibc and bundled-library checks.

Points that a PR description would cover:

- the SDK's libwayland needs wayland-scanner 1.18;
- `CMAKE_LIBRARY_ARCHITECTURE`;
- hiding the cross toolchain's own target glibc so that only the sysroot's is used;
- `-fno-use-linker-plugin`;
- `-lstdc++ -lgcc` for outline atomics with `g++ -shared`;
- `extract_refs.py` assumes system headers sit under `/usr/lib/gcc/`.

The two x86-only FPU spots in `shaderc.cpp` and `dxvk_smoke.cpp` are a separate small PR, because they block any aarch64 Linux host build whatever you decide here.
