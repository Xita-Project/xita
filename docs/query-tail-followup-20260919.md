# Campaign query-result wait follow-up

The retained perf44 campaign movement log reports 1,225,787 us of exact flare-result waiting over 60 frames (20.43 ms/frame). All 60 packets report no existing render-pass boundary after the last potential visibility writer. This is a dependency wait, not a measurement of GPU service time, and cannot be added independently to inclusive frame phases.

The next question is how much recorded work follows that last writer. Added `[query-tail]` counters for no-boundary packets: suffix draw commands, indices, UI batches, and maximum suffix draws per packet. These are conservative command counts, not submitted draw counts or timing. Clears are excluded, empty draws included, and UI at the writer cursor is excluded because replay executes it before that command. Invalid lists remain rejected before counting. Reports reset counters. The scan adds no scene boundary, fence, wait, resource release, or query-result substitution.

If little work follows the writer, splitting there cannot explain or recover the entire observed wait. If a substantial suffix exists, evaluate an explicit boundary with correct color/depth/stencil preservation and final resource ownership; its tile store/load cost may outweigh the earlier publication. Do not implement global stale query reuse: query IDs alone do not identify flare objects.

Validation: `python3 tools/test_query_boundary.py` passed, including ASan/UBSan production replay, exact result generation/history, notification failures, ownership retirement, and the new suffix ordering/reset assertions. These are local correctness tests, not hardware off/on comparisons. `git diff --check` passed.

Status: source instrumentation only; not built or deployed. Hardware FPS improvement is unproven. Continue cumulative builds and ordinary gameplay after full restart; no automated off/on/off runs. Halo 2 remains parked while CE performance is prioritized.
