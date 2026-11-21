# Guard the x86 FPU code in shaderc and dxvk_smoke for AArch64 builds

Branch: `aarch64-host-fpu-guards` (one commit on `b8cdfb1`)

## Problem

Two host targets that the default build scripts compile use x86-only code without a guard:

- `host/d3d9gpu/test/shaderc.cpp` includes `<xmmintrin.h>` and uses `_mm_setcsr` for `--ftz-daz`. `host/d3d9gpu/build.sh` builds it as `d3d9gpu_shaderc`.
- `host/dxvk_smoke.cpp` reads the x87 control word with `fnstcw`. `host/player/build.sh` builds it.

So neither script builds for aarch64 Linux. With a GCC 14 aarch64 cross compiler, the first fails with `xmmintrin.h: No such file or directory`, and the second with `unknown mnemonic 'fnstcw'`. Linux arm64 is a row on the status page. These are the only two such failures in the two scripts.

## Change

The same guards that `d9g_shader.cpp` and `d9g_report.cpp` already use:

- `shaderc.cpp`: the include and the x86 `--ftz-daz` are under `__x86_64__ || __i386__`. On AArch64, `--ftz-daz` sets FPCR.FZ (bit 24), which flushes denormal inputs and outputs and so stands for FTZ and DAZ together. That is the register `d9g_shader.cpp` already uses in MXCSR's place.
- `dxvk_smoke.cpp`: `x87_cw()` returns 0 where there is no x87.

The x86 builds compile the same code as before.

## Validation

- x86-64: `bash host/d3d9gpu/build.sh` gives `D3D9GPU_BUILD_OK`.
- `test_s5_d3d9gpu_shader.py -k ftz_daz` (the FTZ/DAZ regression guard and its `no_fp_guard` control) passes.
- aarch64, cross-compiled (GCC 14 against a glibc 2.31 arm64 sysroot):
  - `host/d3d9gpu/build.sh` gives `D3D9GPU_BUILD_OK`.
  - `host/player/build.sh` gives `BUILD_OK`.
  - `d3d9gpu_shaderc`, `dxvk_smoke` and `test_d3d9gpu` are AArch64 ELF.
  - On main, both scripts fail as above.
- aarch64 under QEMU user emulation: `test_s5_d3d9gpu_shader.py -k ftz_daz`, with the translator built for aarch64, passes.
  - Under `--ftz-daz` the guarded translator prints the denormal defs as on a clean run.
  - With `no_fp_guard` they become zeros. So FPCR.FZ does flush, and the guard's control can fail on AArch64.
- `python3 tools/public_check.py`: 0 failures.

## Limits

- The shader-tools install (`spirv-val` and the rest, `tools/s5/build_shader_tools.sh`) was not built here. I ran only the `ftz_daz` case of `test_s5_d3d9gpu_shader.py`. That case does not call `spirv-val`, but the module fixture checks that the file exists, so a placeholder stood in for it. The rest of that file was not run on either architecture.
- This makes the two targets compile on aarch64 Linux. It does not add a Linux arm64 release target. That question is in a separate issue.
