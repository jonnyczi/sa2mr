# State the fresh-clone build prerequisites the getting-started guide left out

Branch: `fresh-clone-build-docs` (one commit on `b8cdfb1`)

## Problem

Following `docs/getting-started.md` from a fresh clone stops at four places the guide does not mention:

1. **The SA2 Mod Loader checkout.** `Documentation/scripts/s1_function_map.py` reads `SA2ModLoader/include/SA2Functions.h` under `private/predecessor-tools/sa2-mod-loader`. `tools/pins.json` pins the commit and path but no source, and the guide lists only "the unpacked executable and Ghidra". Without the checkout the script stops with a raw `FileNotFoundError`.
2. **Where the pinned tools go.** Ghidra, the Temurin JDK and Zig must be unpacked at their pinned paths, which `tools/env.sh` then puts on `PATH`. The guide does not say so.
3. **The ALSA headers package.** `tools/s5/build_host_deps.sh --fetch` downloads the pinned source archives but, by design, never the Debian ALSA headers package. The script says so when the package is missing, but the guide does not.
4. **The perf_f80 step.** `tools/s7/release_build.py` links the double-f80 build by default. The guide goes from the link tables to "Assemble a desktop build" without `tools/s8/perf_f80.py build`. `release_build.py` prints the command when the objects are missing, so this is a gap in the guide only.

There is also one receipt problem. `s1_function_map.py` writes `Documentation/receipts/s1-function-map-v1.json` into the tracked tree, and its `command` holds the `analyzeHeadless` command line and `JAVA_HOME` / `XDG_CONFIG_HOME` as they ran. Those are absolute paths into the checkout. A `git add -A` after building the map would stage them.

## Change

- `docs/getting-started.md`:
  - The entry-point table names the Mod Loader checkout, the JDK and the ALSA package, and adds the perf_f80 row.
  - One sentence says to unpack Ghidra, the JDK and Zig at their pinned `path`s.
  - One paragraph says to stage the ALSA package from `sdl3.alsa_headers.url` in `private/s5-tools/dl/`.
  - Step 2 clones the Mod Loader at the pinned commit before running the script.
  - A new paragraph gives the `perf_f80.py build --target x86_64-linux-musl --mode double` command.
- `s1_function_map.py`:
  - When the header is missing, the script stops with the repository URL, the pinned commit and the path.
  - The receipt's `command.analyzeHeadless` and `command.env` are relative to the checkout. `private/ghidra/run.json` keeps them as they ran.

## Validation

- `python3 tools/public_check.py` (with its method checks): 0 failures. `test_release_build.py`: 15 passed.
- On a root without the Mod Loader, `modloader_functions` stops with the message naming `https://github.com/X-Hax/sa2-mod-loader`, `611d686f…` and the pinned path.
- Applied to the `command` of a receipt written by a local run, the new receipt expressions give an `analyzeHeadless` line and an environment with only repository-relative paths: `private/predecessor-tools/…`, `private/ghidra/…`, `private/exe/…`, `Documentation/scripts`.
- The documented commands are the ones used to build a fresh clone through `LIFT_IR_OK`, `perf_f80.py build` (24,040 of 24,040 modules) and `RELEASE_BUILD_OK` on Linux x86-64. The extras state in that build was finished by hand; see the separate PR for `rebuild_image.py`.

## Limits and notes

- Whether the s1 receipt should be tracked is left to you. Upstream has never committed it, and `.gitignore` does not ignore it. With this change its paths are publishable, but it still carries a date.
- `release_build.py`'s `f80_command` and a few test expectations print `tools/slot.sh`, which is not in the public tree. The guide's command leaves it out. I did not change the printed command.
- I did not add a `url` to the `sa2-mod-loader` pin. The guide carries the URL, and the script's message names it.
