# Rocket submission audit and fragment-constant failures

During this offline audit the September 15 hardware firing failure had no
recovered fault evidence. When the user reopened Xita, the saved log identified
an intentional worker STOP in a stream-volume call. See the
[owner handoff fix](object-stream-volume-20260915.md). The fragment guard below
addresses a separate error-handling defect; it is not the fix for that STOP.

## What the emulator trace shows

The installed lightweight-mutex runtime (`a8d89663…`) was exercised through
ordinary controller input on Blood Gulch: walking to the rocket launcher,
picking it up, firing, reloading, self-damage, and respawning. Screenshots
confirmed the equipped weapon, death countdown/body, and return to gameplay.

Eight bounded captures contain 11,721 recorded draws and 11,186 uploaded stream
descriptions. Comparing their live strides against the compiled shader layout
found no mismatch and no missing declared stream. Completion checks covered
11,038 draws across 75 traced frames, with no vertex/index hash changes between
recording and GPU completion. Some captures end before their final traced
frame's completion message, so the two draw counts differ.

This rules out those problems only for the captured emulator samples. It does
not prove correct physical GPU behavior, continuous coverage between traced
frames, or that untraced draws cannot encounter an invalid layout. Heavy trace
logging changes timing; emulator frame rates here are not performance results.

Private evidence is under `validation/engine-restructure-20260914T2300Z/`
`rocket-stream-audit/`, including screenshots, log hashes, `analyze.py`, and
`result.json`. Two initially misnamed interaction captures sent neutral input
because of button-name case handling. They are explicitly excluded from these
counts; the corrected helper rejects unknown buttons, and `pickup-actual` and
`firing-actual` contain the verified interactions.

## Submission error handling

The mesh render loop previously continued to `sceGxmDraw` after a failed
`sceGxmReserveFragmentDefaultUniformBuffer`. It also ignored errors from each
`sceGxmSetUniformDataF` call. That can submit a draw without its intended
fragment constants.

`bind_fragment_constants` now requires a successful reservation, a non-null
buffer, and successful writes for every active uniform. A failure skips that
draw and logs the frame, command, failing step, and return code (first eight
failures). Successful draws keep the same uniform values and write order.
Programs without these uniforms make no reservation. This change adds no
full-GPU wait or additional constant-buffer allocation.

This is a verified error-handling defect, not an identified cause of the
hardware firing failure, and no FPS improvement is claimed.

## Validation

`python3 tools/test_fragment_constants.py` exercises the production helper
under AddressSanitizer and UndefinedBehaviorSanitizer. All 222 checks pass:
all active-uniform combinations, constant contents, failed reservations,
null returned buffers, failure at each write, fresh retry after failure, and
both alpha-test override settings.

The Vita native build completes. Package verification checks all 1,588 members;
only `game-a.self` and `boot-game.txt` differ from the updater-compatible base.
Candidate runtime SHA-256:

`f393d3fd68f462fc21a0a4f598f52769db746ec1eeefce7455a8d3506b6cb9d3`

The private CE emulator boots that exact candidate, displays the normal main
menu, loads Blood Gulch, and completes turning/walking with no fragment-uniform
failure or worker STOP in its captured log. Weapon interaction was not confirmed
on this candidate; the complete traced rocket sequence above used `a8d89663…`.
The fragment-only candidate was not installed on hardware. The subsequent
stream-volume candidate includes this guard and retains the rollback slot.
