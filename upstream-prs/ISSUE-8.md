<!--
DRAFT, NOT READY TO POST. Post only after a manual repro in the flat game:
pause in a stage, choose LEADERBOARDS, then press B, Esc and Start (each for 10 s or more) on the
keyboard and on a pad, and record whether the screen exits. Fill the "What happens" line from that run.
If it does exit, do not file this.
-->

# Pause menu: LEADERBOARDS stays on "Loading . ."

**Platform:** Linux x86-64, desktop build from `b8cdfb1` (fill in the CPU, GPU and driver used for the repro)

**Steps**
1. Start a stage, for example City Escape.
2. Pause, and choose LEADERBOARDS.
3. Wait, then press B, Esc or Start to leave.

**Expected:** the leaderboard screen shows an "unavailable" state, or backs out to the pause menu.

**What happens:** the screen shows "Loading . ." and _(fill in: stays there for N seconds and does not leave on B, Esc or Start / leaves after …)_.

**Notes from the executable**
- The menu entry calls `0x00411AE0`. When that returns 0, the entry sets GameState `0x26` (38) and calls `0x004390F0(1)`. Otherwise it calls `0x00411B30`.
- The runtime's Steam interface answers FindLeaderboard and the download calls with no call handle, so the screen appears to wait for a result that never comes.
- While the screen showed "Loading", GameState stayed 38 for at least 270 frames.

**Possible fixes:** take the 'unavailable' branch, back out of the screen when there is no call, or hide the entry. Any of them could be a runtime change or a tweak, depending on what you prefer.
