# Finish the restored extras state in rebuild_image.py

Branch: `fresh-clone-extras-finalize` (one commit on `b8cdfb1`)

## Problem

On a fresh clone, `tools/s4/rebuild_image.py` restores the extras' state with `track_extra_functions.py --restore`. The restored state holds `tag`, `lift_tag`, `arms_tag` and `rows`. The rebuild then compiles every extra's object and stops. It never runs the block that ends `lift_extra.py`'s `main`: the rejection pass, `emit_registry`, and the `objects` / `registry_c` update.

So after the getting-started sequence:

- `tools/s7/cross_build.py` `populations()` reads `state["objects"]` and raises `KeyError: 'objects'`. That stops `tools/s8/perf_f80.py build`, which makes the double-f80 objects the default desktop link reads. It also stops `tools/s8/android_game.py compile`.
- `tools/s7/release_build.py` (`link_inputs`) and `tools/s7/arm64_link.py` read the key with `.get("objects")`. With it missing they link no extras and do not say so.
- `extra_functions.next+allarms.c` is never written.

## Change

- `tools/s4/lift_extra.py`: the rejection pass, the registry and the state update move unchanged into `finalize(state, state_path, tag, lift_tag, arms_tag)`. `main` calls it, so its behaviour is unchanged. The registry path is now derived from `state_path`, which is `LINKDIR` in `main`.
- `tools/s4/rebuild_image.py`: after the extras are rebuilt, it loads the state and calls `LE.finalize` with the state's own `lift_tag` and `arms_tag`. The final summary also reports `extras_registered` and `extras_rejected`.
- Tests:
  - `test_fresh_checkout_lift.py`: a restored two-row record finalizes to its objects and registry and still projects to the tracked record. A row whose callee no module defines is rejected, and so is a row that calls it.
  - `test_fold_code_stamp.py`: a rebuild over a restored state ends with `objects`, `registry_c` and the registry file.
  - The stamp tests' fixture now provides the arm work list and the module ranges itself. On upstream main, `test_a_complete_rebuild_stamps_the_fold_code_it_read_before_it_started` fails on a fresh clone. With no `armlist.next.json` the rebuild runs one extra step (`armlist.py`). The test passes only on a checkout that already has a map build.

## Validation

- `pytest Documentation/scripts/tests/test_fresh_checkout_lift.py test_fold_code_stamp.py`: 26 passed. With the `rebuild_image.py` change reverted, the new rebuild test fails.
- `test_address_taken.py` and `test_state_local.py` give the same results on this branch as on main. Seven `test_state_local.py` cases fail on both, for want of local generated inputs.
- With a translated image whose modules match `lift-ir-v1.json` (`LIFT_IR_OK`):
  - restoring `Documentation/lifted-extras.json` and running `finalize` with that build's map and arm module lists gives 5,646 of 5,646 registered and 0 rejected;
  - its registry and object list are byte-for-byte the ones `lift_extra.py`'s own block wrote for that build;
  - `track_extra_functions.py`'s projection still equals `Documentation/lifted-extras.json`.
- The same block, applied by hand after `rebuild_image.py` on a fresh clone, let `perf_f80.py build --target x86_64-linux-musl --mode double` build 24,040 of 24,040 modules, and the desktop build `release_build.py` then gave `RELEASE_BUILD_OK`. The built desktop game reaches the main menu in `--selftest` (`SA2_SELFTEST_OK`) on Linux x86-64.
- `python3 tools/public_check.py`: 0 failures.

## Limits

- I did not rerun the whole `rebuild_image.py` (about six minutes) with this branch. The real-data check above runs `finalize` on the restored state directly, not inside a full rebuild.
- `release_build.py` and `arm64_link.py` still accept a state that has rows but no `objects`. A refusal there would catch the same gap at link time. I left it out to keep this change to one problem, and can add it here or separately.
