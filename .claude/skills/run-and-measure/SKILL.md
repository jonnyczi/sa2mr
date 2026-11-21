---
name: run-and-measure
description: Run the SA2 game (flat or VR) on this machine and measure it - frame pacing, audio, errors, screenshots - from OpenSA2 session logs. Use when testing a change in-game, when the user reports lag, stutter, no sound, a crash or a black screen, or before claiming a gameplay/render change works.
---

# Run the game and measure it

## Before launching
- **Tell the user.** The game opens a visible window and plays sound.
- **Never move or focus their windows** (`niri msg action ...` acts on whatever is focused). If a monitor or window test is needed, ask the user to drag the window and say when you start measuring.
- **Prefer headless checks when you don't need to see it:** `build-env/play.sh --selftest` (scripted title→menu), or `MONADO_NULL=1 build-env/monado.sh` for XR.
- **Check load with `uptime`.** A running QEMU arm64 job (the `/run/binfmt/aarch64-linux` processes) skews timings. Note it, or cap it.

## Launch
| What | Command |
|---|---|
| Flat | `build-env/play.sh` (`--selftest` for the check). Add `timeout N` for bounded runs. |
| VR | `build-env/monado.sh &`, then `XR_RUNTIME_JSON=<printed path> build-env/play-xr.sh`. Options are in `external/OpenSA2/host/xr/README.md`: `SA2_XR_MODE=diorama`, `SA2_XR_SCALE`, `SA2_XR_ANCHOR`, `SA2_XR_SHOT` (eye PNG dump), `SA2_XR_PROBE`. |
| Frame mimic | Add `XR_API_LAYER_PATH=external/OpenSA2/private/generated/xr/xr_sim_layer XR_ENABLE_API_LAYERS=XR_APILAYER_AURORA_frame_sim AURORA_SIM_FRAME=1 AURORA_SIM_BLEND=alpha` |
| Start in a stage | `SA2_TWEAKS=skip-intro,practice-stage` (City Escape), or `start-in-chao-world` |

## Measure (newest session = `ls -td ~/.local/share/sa2recomp/sessions/*/ | head -1`)
- **Pacing:** `build-env/pace-stats.sh [N|session]` reports fps, frames over 25 ms, how often the game ran *behind*, and fps per 5 s window.
  - Healthy flat is about 59.5 fps with only a handful over 25 ms.
  - A high "behind" count means the player back-pressured the game. Example: XR waiting on `xrWaitFrame`.
- **Audio:** `player.err` must say `audio: SDL alsa driver`. `dummy` means `--no-sound`, a test path, or an ALSA problem.
- **Errors:** `player.err` ("host failure", "verdict: FAIL", "device creation failed"), `game.log`, `launch.runrec`.
- **XR:** look at the `player.err` lines starting with `xr:`. They cover runtime, session state, the swapchain, `display at N Hz` flapping, and per-frame draw counts (world / flat / HUD).
- **Screenshots:** go in `media/` (eye dumps via `SA2_XR_SHOT`). Look at them before claiming a visual result.

## Known causes
- **The GPU appears twice, `Direct3DCreate9` returns NULL, and the player exits 3:** `VK_DRIVER_FILES` is unset (the scripts set it).
- **Judder on the 144 Hz Odyssey:** 60 fps doesn't divide 144. Suggest 120 Hz. The real fix is interpolating to the display rate.
- **XR lag that tracks Monado's 30↔60 Hz period:** the XR loop is not decoupled from stream consumption.

Report numbers, not impressions.
