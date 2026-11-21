# Upstream candidates for OpenSA2 (non-VR)

Research only. Nothing has been opened, pushed, posted or committed. Researched 2026-11-21.

## Upstream state and rules (what a contribution must satisfy)

- **Upstream main is our base.** `kittycache9/OpenSA2` main is `b8cdfb1`, last pushed 2026-11-08. That is the commit our `external/OpenSA2` checkout sits on, so none of the items below has been fixed upstream since we branched. There are no open PRs.
- **Outside PRs are accepted.** heyjoeway has had five merged (#9, #17, #22, #23, #24): focused fixes and tools, each with a test.
- **CONTRIBUTING.md asks for:**
  - one behaviour or one build problem per change;
  - the existing types and contracts;
  - a check for the observed result where a test exists, with synthetic tests preferred;
  - `python3 tools/public_check.py` before submitting;
  - a PR that states the problem, the resulting behaviour, the validation and its limits.
- **They want:** reproducible failures, build-setup improvements and checked implementations of unfinished paths.
- **Evidence rules** (`docs/method/principles.md`):
  - Claims must use the claim vocabulary: builds / maps / lifts / reaches / plays on named devices …
  - Published text must not contain personal names or handles, absolute paths, machine names, device serials, excerpts of logs or agent transcripts, or references to non-public material. "When private notes inform a public page, write the page fresh."
  - Run `public_check.py --deny-file` with a deny list kept outside the repository.
  - Check packages with `tools/release/check_package.py`.
- **Platforms** (README and status page): Linux x86-64 and Windows are established, and Android arm64 is experimental on named phones. **Linux arm64 is "Cross builds and registered replays under QEMU. No general native-hardware support claim."** So they track that row but have no evidence from a real device.
- **Repository boundary:** no game executables, assets, translated or generated code, captures, or device records. Receipts (hashes, counts, verdicts) and projections are fine.

## Ranked summary

| Rank | Candidate | Form | Value | Ease | Size |
|---|---|---|---|---|---|
| 1 | Fresh-clone extras state is unfinished after `rebuild_image.py` (KeyError `objects`) | **PR** (with an issue text in the PR) | High: blocks every fresh-clone desktop **and Android** build | High | ~30 lines moved, ~8 added, plus a test |
| 2 | d3d9gpu refuses a GPU enumerated twice (identical deviceUUID/driverUUID) | **PR** | High for Linux users running under Steam, Flatpak or NixOS; related to open #36 | High: synthetic stub-driver test exists | ~40 lines, plus a test |
| 3 | x86-only FPU code in `host/d3d9gpu/test/shaderc.cpp` and `host/dxvk_smoke.cpp` breaks a native aarch64 Linux player build | **PR** | Medium: unblocks any arm64 Linux host build | Very high: mirrors `d9g_shader.cpp` | ~14 lines |
| 4 | Fresh-clone docs gaps: Mod Loader checkout for s1, ALSA headers .deb, the `perf_f80.py` step; s1 receipt written into the tracked tree | **PR** (docs plus small guards) | Medium | High | ~30 doc lines, ~10 code lines |
| 5 | Linux arm64 reaches the main menu on a native device (Steam Frame, Turnip Adreno 750) | **Issue** (offer a `release_build.py --target aarch64-linux` PR) | Medium to high: their status row has no device evidence | Needs maintainer buy-in | PR later ~200–400 lines |
| 6 | SDL3 library hash depends on which optional dev packages are installed | **Issue** (offer a PR) | Medium: reproducibility, plus the shipped SDL's features are undocumented | Needs their configure summary | ~20 lines |
| 7 | Function-map sha differs from the pin although the counts and LIFT_IR_OK match | **Issue** | Low to medium | Needs their map to diff | — |
| 8 | Pause-menu LEADERBOARDS sits on "Loading . ." (GameState 38) | **Issue, only after a manual repro** | Medium if confirmed (soft-lock) | Easy to report | — |
| 9 | Chao give/absorb notes as leads for open tweak request #51 (infinite drives/animals) | **Issue comment, written fresh** | Low to medium | Easy | — |
| — | Turnip/Adreno no 16/8-bit storage | Skip as its own item (already handled upstream); mention in #5 | — | — | — |
| — | Relay perf line, stream v3/v4, player audio-table check, objcheck/test_s5_stream edits, tweaks | Skip (VR-driven) | — | — | — |

---

## 1. Extras state left without `objects`/`registry_c` after `rebuild_image.py`

**What it is.**
- On a fresh checkout, `tools/s4/rebuild_image.py` restores `private/generated/s4/link/extra_functions.next+allarms.json` with `track_extra_functions.py --restore`. That writes only `tag`, `lift_tag`, `arms_tag` and `rows`.
- It then rebuilds every extra's object, but never runs the finalize block at the end of `lift_extra.py`'s `main()`. That block does three things:
  - the iterative rejection pass;
  - `emit_registry(...)`, which writes `extra_functions.<tag>.c`;
  - `state.update(objects=..., registry_c=..., registered=..., attempted=...)`.

**Evidence that it is real** (upstream main `b8cdfb1`, read in a scratch clone):
- `tools/s7/cross_build.py:188`: `json.loads(EXTRA_STATE.read_text())["objects"]` raises a KeyError. `perf_f80.py build` reaches it through `XB.populations()`, so the default **double-f80 desktop build cannot be made from a fresh clone**. This is the failure we hit (BUILD_NOTES step 9).
- `tools/s8/android_game.py:379` does the same `["objects"]` read, so **the Android game link fails the same way**.
- `tools/s7/release_build.py:531` and `tools/s7/arm64_link.py:259` use `st.get("objects")`. With the key missing, they **silently link no extras**: 5,646 modules on `--f80 native` and in the QEMU arm64 link. The game would then build but hit "control left the lifted code" at run time.
- The fresh-clone path is new upstream: commits `965f4da` "Build a fresh checkout's whole image with rebuild_image.py" and `b15a34d` "Restore the lifted extras' state from the tracked record". It is probably untested end to end.
- Our workaround, `private/sa2mr/finalize_extras.py`, is that block copied verbatim. It gave 5,646 of 5,646 registered and 0 rejected. After it, `perf_f80.py` built 24,040 of 24,040 modules, and `release_build.py` gave RELEASE_BUILD_OK and SA2_SELFTEST_OK.

**Already upstream?** No. There is no issue and no fix on main.

**Fit.** This is exactly "improve the build setup" and "make a failure reproducible" for their own documented getting-started path.

**Minimal patch.**
- `tools/s4/lift_extra.py`: move the finalize block (rejection loop, `emit_registry`, `state.update`, write) into `def finalize(state, state_path, tag, lift_tag, arms_tag) -> tuple[list, list]`, and call it from `main()`. Behaviour there is unchanged. About 30 lines move.
- `tools/s4/rebuild_image.py`: after the extras pool and before `stage_merged_build`, load the state and call `LE.finalize(...)` with `MERGED`, `TAG` and `TAG`. About 6–8 lines.
- Optional hardening: make `release_build.py` and `arm64_link.py` refuse a state that has `rows` but no `objects`, instead of silently linking none.
- Test: a synthetic case in `Documentation/scripts/tests/` (a restored state with fake rows and no game data) asserting that `objects` and `registry_c` exist after finalize.
- Validate with `check_lifted_extras.py` (the projection must be unchanged, since 0 are rejected), LIFT_IR_OK, and the perf_f80 build.

**Risk.** Low. It is the same code `lift_extra.py` already runs. One thing to check: the stamp and fold-code logic in `rebuild_image.py` does not cover the registry `.c`, and that is fine because the registry is not fold code.

**Recommendation: PR.**

## 2. Same GPU enumerated twice, so `Direct3DCreate9` returns NULL (exit 3)

**What it is.**
- `query_vulkan_device()` (`host/d3d9gpu/d9g_gpu_sdl.cpp` ~1580) and `query_device_facts()` (`host/d3d9gpu/d9g_shader_vulkan.cpp` ~330) match the SDL_gpu device by `deviceName` and require exactly one match.
- When two ICD manifests expose the same GPU, both return "2 physical devices named …". The renderer then refuses: "the shader translator's device facts could not be read", Direct3DCreate9 returns NULL, and the player exits 3.

**Evidence** (from this machine, through `steam-run vulkaninfo`):
- Two RADV manifests, `/usr/share/vulkan/icd.d` (the FHS env's Mesa) and `/run/opengl-driver/share` (via XDG_DATA_DIRS), point at two different Mesa 26.1.8 store paths.
- `vulkaninfo` lists GPU0 and GPU1 with **identical** `deviceName`, `driverName` radv, `driverInfo` "Mesa 26.1.8", `apiVersion`, `deviceUUID` `00000000-c500-…`, and `driverUUID` `414d442d-4d45-5341-…`. llvmpipe is duplicated the same way.
- Our workaround is `VK_DRIVER_FILES=...radeon_icd.x86_64.json` (BUILD_NOTES, "Running on this machine").
- Duplicate manifests also happen with Flatpak or Steam-runtime containers next to host drivers, and on distros with both `/etc` and `/usr/share` ICDs.

**Already upstream?** No. The related open issue is **#36**: an Adreno 740 where the facts query found "0 physical devices named Adreno (TM) A32". The owner replied that the renderer "looks up your gpu by name" and that they are looking into it. Same code, opposite failure (0 matches instead of 2). Our fix does not solve #36, but it is in the same function and shows the name-only match is fragile.

**Minimal patch.**
- In both loops, when the name matches more than one device, read `VkPhysicalDeviceIDProperties` (core 1.1; the facts path already uses Properties2) and `VkPhysicalDeviceDriverProperties`.
- Narrow to devices whose `driverName`/`driverInfo` equal what SDL already reported (`info_.driver_name` / `driver_info` from `SDL_PROP_GPU_DEVICE_DRIVER_*`, `d9g_gpu_sdl.cpp:88–90`).
- If the remaining devices all share `deviceUUID`, `driverUUID` and `driverVersion`, they are the same device and driver: take the first, and say so in `vk_query`, e.g. "2 identical devices (same UUIDs): the first".
- Otherwise keep refusing, with a hint to `VK_DRIVER_FILES`.
- About 40 lines across the two files.
- Test: `host/d3d9gpu/test/test_d3d9gpu.cpp`'s `stubvk::reset({device(...), device(...)})` already takes a device list. Add the case of two identical stub devices (accepted) and two with different UUIDs (still refused). The stub needs to fill `VkPhysicalDeviceIDProperties` if it doesn't already.
- Validate with `TEST_D3D9GPU_OK`, and the flat selftest under `steam-run` without `VK_DRIVER_FILES`.

**Risk.** Low. The rule only accepts devices that are indistinguishable by UUID and driver, so the facts read are identical whichever one SDL picked. The flat behaviour with a single device is unchanged.

**Recommendation: PR**, referencing #36 as related, not fixed.

## 3. x86-only FPU code in two host build targets

**What it is.**
- `host/d3d9gpu/test/shaderc.cpp` includes `<xmmintrin.h>` unconditionally and uses `_mm_setcsr` for `--ftz-daz`.
- `host/dxvk_smoke.cpp` uses `fnstcw` inline asm unconditionally.
- Both are compiled by the default `host/d3d9gpu/build.sh` (d3d9gpu_shaderc) and `host/player/build.sh:115` (dxvk_smoke), so a native or cross aarch64 Linux build of the players fails to compile.

**Evidence.**
- Our arm64 build failed there until it was guarded. With the guards: `TEST_D3D9GPU_OK 5445 checks` under QEMU arm64, and the players run on a real Steam Frame.
- The tree already uses the same pattern in `host/d3d9gpu/d9g_shader.cpp:25–32, 136–160` (an FPCR stand-in for MXCSR), `d9g_report.cpp:91–100` and `host/install/sa2_platform_posix.cpp:217`.

**Already upstream?** No.

**Minimal patch.**
- The guards are our diff to those two files, about 14 lines:
  - `#if defined(__x86_64__) || defined(__i386__)` around the include and `_mm_setcsr`;
  - the AArch64 `--ftz-daz` as FPCR.FZ (bit 24), matching how `d9g_shader.cpp` treats FPCR;
  - `x87_cw()` returning 0 off x86.
- Check: on AArch64, FPCR.FZ flushes denormal inputs and outputs for single and double precision, which matches x86 FTZ|DAZ. AH/FIZ are not needed.

**Risk.** Very low. x86 builds are unchanged byte for byte.

**Recommendation: PR.** It is small and has an obvious reason: Linux arm64 is in their platform table. Could be bundled with #5's issue or sent on its own.

## 4. Fresh-clone build docs gaps, and s1 receipt hygiene

Each of these was hit while following `docs/getting-started.md` on a fresh clone (BUILD_NOTES steps 4, 5, 8, 10).

- **Mod Loader checkout.**
  - `Documentation/scripts/s1_function_map.py` reads `private/predecessor-tools/sa2-mod-loader/SA2ModLoader/include/SA2Functions.h`. `tools/pins.json` pins commit `611d686f…` and the path, but has no URL.
  - Getting-started step 2 lists only "The unpacked executable and Ghidra". Without the checkout the script dies with a raw FileNotFoundError.
  - Patch:
    - one doc line: clone X-Hax/sa2-mod-loader at that commit into the pinned path;
    - a `url` in the pin, plus optionally a `header_sha256` (ours at that commit: `93d19525…`, which is content-addressed, so it must equal theirs);
    - a friendly existence check in `s1_function_map.py` naming the pin.
  - Ghidra and Temurin paths come from `tools/env.sh`, but getting-started doesn't say where to unpack them. One sentence fixes that.
- **ALSA headers.** `build_host_deps.sh --fetch` deliberately does not fetch `libasound2-dev_1.2.14-1+deb13u1_amd64.deb`, which is pinned with a URL under `sdl3.alsa_headers`. Getting-started doesn't say to stage it in `private/s5-tools/dl/`. One doc line.
- **perf_f80 step.** The default desktop build links the double-f80 build (`release_build.py --f80 double`). Getting-started's build sequence goes from the tables straight to "Assemble a desktop build" without `tools/s8/perf_f80.py build --target x86_64-linux-musl --mode double --out private/generated/s8/pc-double/linux-obj`. `release_build.py` does print that command when the build is missing, so this is a doc gap, not a trap. One doc step.
- **s1 receipt written into the tracked tree.**
  - `s1_function_map.py` writes `Documentation/receipts/s1-function-map-v1.json`. Upstream has never tracked it, and `.gitignore` doesn't ignore it.
  - The file contains absolute paths (the `analyzeHeadless` command and `JAVA_HOME`) and a date.
  - A `git add -A` would stage machine paths, which breaks their own publication rules. In our checkout it shows up as `??`.
  - Fix: write it under `private/generated/` (or gitignore it), or strip `command.env` to relative paths. About 2–5 lines.

**Already upstream?** No.

**Fit.** CONTRIBUTING's "Improve the docs: state the prerequisite before a command".

**Risk.** Nil to low.

**Recommendation: PR** (docs, plus the two small guards). Keep it separate from #1 so each PR is "one build problem".

## 5. Linux arm64 on a native device (Steam Frame)

**What we have** (BUILD_NOTES "ARM64 build" and "First run on a real Steam Frame"):
- **Build.**
  - Game: `perf_f80.py build --target aarch64-linux-musl --mode double`, 24,040 of 24,040 modules. The static musl `sa2-game` is linked by reusing `release_build.step_game` with the triple set to `aarch64-linux-musl` and `-ffp-contract=off`, as `tools/s7/arm64_link.py` does.
  - Host side: players, installer and launcher are cross-compiled with GCC 14.2 for aarch64 against Valve's Steam Linux Runtime 3 "sniper" arm64 SDK, pinned by image digest, with libstdc++ static.
  - Deps: SDL3 3.4.16 with the project patch, DXVK d3d9 and zstd built for arm64.
  - Checks: every ELF is AArch64, the game is static, nothing needs a symbol newer than GLIBC_2.31, and no system library is bundled.
- **Under QEMU arm64:** TEST_D3D9GPU_OK (5445), SA2_INSTALL_OK (4191 of 4191 files), SA2_SELFTEST_OK.
- **On a Steam Frame** (SteamOS 0.4.5, aarch64, Mesa Turnip "Adreno (TM) 750"):
  - SA2_INSTALL_OK, and SA2_SELFTEST_OK (title at tick 3085, main menu at 3580).
  - Flat interactive: 59.9 fps over 6,000 frames (25 frames over 25 ms), PipeWire audio.
  - In claim vocabulary: **"reaches the main menu on a named native arm64 Linux device"**. For flat play we do **not** have "plays on named devices": in a desktop window the Frame's controllers act as pointers and clicks arrive as touches, so OpenSA2's touch overlay appears and there is no gamepad.
  - Turnip has no `uniformAndStorageBuffer16BitAccess`/`8BitAccess`. d3d9gpu's existing 32-bit state-word fallback engaged automatically and rendered correctly. The same message appears in #36 on the proprietary Adreno 740 driver, so this is already handled and worth only a sentence.

**Already upstream?** Partly. They cross-build the lifted image for `aarch64-linux-musl` (perf_f80, arm64_link, arm64_replays) for QEMU replays. `release_build.py` has no `aarch64-linux` target, and the status page has no native-device evidence.

**Fit.** It moves their own Linux arm64 status row. But it is a product and platform decision: which sysroot or runtime, packaging, and whether to ship it. The maintainer should choose. Our driver (`private/sa2mr/arm64/release_build_arm64.py`, 415 lines) monkeypatches `release_build` and also carries XR pieces (`sa2_player_xr`, the OpenXR loader), which must be stripped.

**Patch shape, if they want it.**
- `release_build.py --target aarch64-linux`:
  - a toolchain wrapper (our `xwrap`, a GCC cross plus a sysroot);
  - the dep build (`build_deps.sh` equivalent);
  - arm64 library pins;
  - the ELF, GLIBC_2.31 and bundled-library checks.
- About 200–400 lines plus docs.
- Pinning the sniper SDK by digest fits their pin model.
- Lessons learned that would go into a PR description:
  - wayland-scanner 1.18 for the SDK's libwayland;
  - `CMAKE_LIBRARY_ARCHITECTURE`;
  - hiding the cross toolchain's own glibc;
  - `-fno-use-linker-plugin`;
  - `-lstdc++ -lgcc` for outline atomics with `g++ -shared`;
  - `extract_refs.py`'s `/usr/lib/gcc/` path assumption;
  - clang dropped because the duplicate-symbol check is GCC-specific.

**Risk.** It is the largest change, and the maintainer may prefer a different runtime. The Frame is also a niche device, so frame the target as "glibc 2.31+ aarch64 Linux" (SteamOS, Asahi, Pi 5, Snapdragon laptops).

**Recommendation: issue first.** Report the evidence in the claim vocabulary and ask whether they want a `linux-aarch64` target and which sysroot. Offer the PR, and send #3 regardless. Publication rules: no hostnames, user paths, serials or log excerpts. Write the issue fresh.

## 6. SDL3 library hash depends on the installed dev packages

**What it is.**
- `build_host_deps.sh` configures SDL3 with a few options forced (ALSA on; Pulse, PipeWire, JACK, sndio, XScrnSaver and XTest off). Every other optional backend is auto-detected from whatever headers the build host has.
- Our Debian 13 container gives the library sha `b3bd7ab6…`; the pin is `dcd59404…`.
- Our configure summary shows these auto-detected:
  - wayland(dynamic) and x11(dynamic) with xcursor, xdbe, xfixes, xinput2, xrandr, xshape and xsync;
  - **libdecor off**, dbus off, ibus off, libudev off, libusb off, liburing off, kmsdrm off, fribidi/libthai off.
- No absolute paths are embedded in the library (checked), so the difference is the feature set.
- The script's own comment admits it: "Other unavailable features are printed from configure."

**Why upstream should care.**
- A contributor can't reproduce the pin, which breaks their "pinned inputs, checkable receipts" model.
- The shipped DEB/ZIP's SDL feature set is undocumented. libdecor decides whether Wayland windows get decorations on GNOME. udev and dbus affect joystick hotplug and screensaver inhibit.

**Already upstream?** No.

**Minimal patch.**
- Pass every auto-detected option explicitly, for example `-DSDL_WAYLAND_LIBDECOR=… -DSDL_DBUS=… -DSDL_IBUS=… -DSDL_LIBUDEV=… -DSDL_HIDAPI_LIBUSB=… -DSDL_LIBURING=… -DSDL_KMSDRM=… -DSDL_FRIBIDI=… -DSDL_LIBTHAI=…` and the X11 sub-options.
- Extend the existing CMakeCache check (the one that already checks the ALSA rows) to fail when a wanted value isn't kept, which happens when a required dev package is missing.
- Optionally record the "Enabled backends" summary or the sha of `SDL_build_config.h` in the pin, so a mismatch names the differing define.
- About 20 lines.
- The values must be upstream's, and they will have to repin, which only they can do.

**Risk.** Medium: the chosen values change runtime behaviour.

**Recommendation: issue.** Ask for their configure summary or package list, and offer the PR. Low effort for them.

## 7. Function-map sha differs from the pin

**What it is.**
- Our `private/generated/function-map.json` is `023dedf4…`; the pin and getting-started say `11b82ae8…`.
- Inputs were the same pins: Ghidra 12.1.3_PUBLIC, Temurin 21.0.12.1+1, the same driver and Java script, and the Mod Loader header at the pinned commit.
- Counts are identical (12,683 functions, same regions and seeds), and LIFT_IR_OK passes for all 12,683 / 5,711 / 5,646 modules.

**What we checked.**
- `ExportSA2FunctionMap.java` uses TreeSet and TreeMap, and `s1_function_map.py` sorts by address, so ordering is deterministic.
- There are no analysis timeouts in `headless.log`.
- The likeliest source is Ghidra-derived per-function fields that nothing downstream reads: `calling_convention` and `signature_source` (the Decompiler Parameter ID analyzer), `fid`, and `ghidra_name_source`. A grep of `tools/` shows no consumer of `calling_convention`, `signature_source` or `ghidra_name_source`.
- This is a hypothesis; it can't be confirmed without their map.

**Diagnostic to offer** (the same approach the owner used for the exe regions in #41). These sha256 values are over sorted JSON rows `[address-ordered field lists]`, and only hashes and counts would be published:

| Projection | Hash |
|---|---|
| (address, size, region) | `c71b6ad4…cbd4` |
| lift-facing (address, size, region, jump_tables, direct_calls, import_calls, data_refs, fs0_sites, thunk) | `a31d8002…58f2` |
| Ghidra-derived (calling_convention, signature_source, ghidra_name, ghidra_name_source, fid) | `7c18ea82…2fca` |
| top-level fields | memory_map `168581df…`, imports `e06f98b0…`, entry_points `0563cb03…`, string_clusters `5f2ceafc…`, modloader_conflicts `eec0b175…` |

The script is in the scratchpad (`proj.py`) and would be rewritten fresh for an issue.

**Possible fix upstream:** pin a projection hash of the fields the lift consumes, or document that the full-map hash may differ while LIFT_IR_OK is authoritative.

**Already upstream?** No.

**Risk.** None as an issue.

**Recommendation: issue**, low priority. Could be merged with #4's PR description as a note.

## 8. LEADERBOARDS in the pause menu sits on "Loading . ."

**What it is.**
- Pause menu → LEADERBOARDS calls `0x00411AE0()`. On 0 it sets GameState `0x26` (38) and calls `0x004390F0(1)`; otherwise it calls `0x00411B30()`.
- OpenSA2's Steam interface (`runtime/sa2_iface.c` ~398–414) answers FindLeaderboard and the download calls with no call (no handle), so the screen never leaves "Loading".

**Evidence.**
- From our RE notes (`docs/vr-game-hooks.md` §2, written by us, not upstream): a probe in City Escape showed GameState 38 held from probe frame 760 to at least frame 1030.
- A B press injected about 100 frames in did not leave it.
- **The evidence is weak for a soft-lock:** the injection was a write to the press word, which may not be the path this screen polls. Nobody has tried real keyboard or pad input for 10+ seconds.

**Already upstream?** No issue mentions it.

**Fit.** A reproducible failure on an established path (pause menu, Linux), and a likely tweak or runtime fix. Possible fixes: make the leaderboard screen back out, return the "unavailable" branch, or hide the entry.

**Recommendation: issue, only after a manual repro** in the flat game: pause → LEADERBOARDS → press B/Esc/Start for 10 s, and record whether it exits. Use their bug template with platform, revision and steps. Text only, no screenshot of game data.

## 9. Chao give/absorb notes for tweak request #51

**What it is.**
- Open issue #51 asks for infinite animals and Chaos Drives. The owner accepted it onto the Chao tweak list.
- Our `docs/chao-internals.md` §3–4 has verified leads:
  - the drive/animal take path `0x00566BC0` → `0x00566C40`;
  - absorb `0x00566DF0`;
  - the take helper `0x0056CFB0`;
  - the stat rows `0x008A6240` / `0x00535F00`;
  - item modes and the held-only collision spheres.
- An infinite-drive tweak would keep or respawn the item after absorb.

**Fit and rules.** Addresses and behaviour descriptions are what upstream's own tweak sources contain, so no SEGA code or data is involved. Their rule says write fresh: don't paste, link or name our notes.

**Recommendation: optional issue comment on #51**, a few lines of fresh text with addresses and observed behaviour. Low effort, modest value. The rest of chao-internals and vr-game-hooks (hold pinning, first person, VR hands) is VR-specific: skip.

## Skipped

- **Turnip/Adreno 16/8-bit storage:** already handled automatically upstream (the same message appears in #36). Mention it in #5 only.
- **Relay perf line** (`sa2_relay.{h,cpp}`): a generic diagnostic, but added for VR pacing. It could be offered later if upstream asks about pacing (#26 mentions cutscene fps).
- **Stream v3/v4, camera and draw-mark records, player/mixer audio-table check, objcheck/test_s5_stream edits, the vr-* tweaks, the launcher XR path, the Android GameSettings lines:** all VR.
- **The `pins.json` SDL repin:** local only. It is the symptom of #6 and must never be sent.

## Safety checklist for anything sent

- None of the proposed patches contains SEGA code or data, or generated output (lifted IR, objects, function map, captures). They are host C++/Python build logic, docs, and hashes or counts.
- Before any PR: run `python3 tools/public_check.py --deny-file <local deny list with the home path, hostnames, user name>`. Also check commit messages and PR text for `/home/...`, `/work/...` machine paths, the Frame's hostname, session dates and log excerpts.
- AGENTS.md: commit with the repository's configured identity, and add no tool or assistant attribution trailers.
- Each PR must be prepared from a clean branch off upstream main, not from our checkout's working tree, which mixes in the VR edits.
