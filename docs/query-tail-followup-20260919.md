# Campaign query-result wait follow-up

The retained perf44 campaign movement log reports 1,225,787 us of exact flare-result waiting over 60 frames (20.43 ms/frame). All 60 packets report no existing render-pass boundary after the last potential visibility writer. This is a dependency wait, not a measurement of GPU service time, and cannot be added independently to inclusive frame phases.

The next question is how much recorded work follows that last writer. Added `[query-tail]` counters for no-boundary packets: suffix draw commands, indices, UI batches, and maximum suffix draws per packet. These are conservative command counts, not submitted draw counts or timing. Clears are excluded, empty draws included, and UI at the writer cursor is excluded because replay executes it before that command. Invalid lists remain rejected before counting. Reports reset counters. The scan adds no scene boundary, fence, wait, resource release, or query-result substitution.

If little work follows the writer, splitting there cannot explain or recover the entire observed wait. If a substantial suffix exists, evaluate an explicit boundary with correct color/depth/stencil preservation and final resource ownership; its tile store/load cost may outweigh the earlier publication. Do not implement global stale query reuse: query IDs alone do not identify flare objects.

Validation: `python3 tools/test_query_boundary.py` passed, including ASan/UBSan production replay, exact result generation/history, notification failures, ownership retirement, and the new suffix ordering/reset assertions. These are local correctness tests, not hardware off/on comparisons. `git diff --check` passed.

Deployment: cumulative perf.45 / b7abd3c built successfully and was verified, restarted, and boot-confirmed in slot 1 on the Vita. Runtime SHA-256: `f5d0e30b5adffe5fd7db7253c9f9baac87df31db493a96494373b4fb936fc7d3`. Package membership unchanged; only `game-a.self` and `boot-game.txt` changed. Receipts are in the private `query-tail-hardware` directory. Campaign navigation completed; gameplay observation is pending. This diagnostic update has no claimed FPS improvement. Continue cumulative builds and ordinary gameplay after full restart; no automated off/on/off runs. Halo 2 remains parked while CE performance is prioritized.


## Perf45 ordinary campaign observation

The launch observer completed and the screenshot confirms a rendered room, Marine, pistol and HUD. The saved campaign resumed into a different scene state from the earlier perf44 movement capture; these are not paired measurements.

Recent untraced 60-frame windows report 12.6–13.0 FPS. In the last captured window, existing query boundaries were ready/attached for all 60 packets and all 60 notifications were observed before final completion. No packets required a new boundary. Exact flare waiting was 127,533 us / 60 = 2.13 ms/frame. Therefore the earlier ~20 ms dependency wait is not universal, and an additional boundary is not justified for this current view.

Object batches took 1,673,775 us / 60 = 27.90 ms/frame; inclusive FA920 was 2,246,115 us / 60 = 37.44 ms/frame. Draw-HLE reported 11.8 ms/frame at 153 draws/frame. These nested/overlapping measurements must not be added as independent costs. Worker lock waits were 655,865 and 793,097 us per 60 frames on the two lanes; these overlap and identify contention, not recoverable frame time. Matching ELF symbolization used a 0x10000 load slide. Quaternion helpers and 4C980 were prominent waiting callers, not proven holders.

The cumulative marker snapshot remained active (2,506 / 2,135 records on the lanes in the last report), and private quaternion admissions were 1,313 / 1,067. Inspecting the current quaternion helper confirms shared input is already copied before the private-compute guard release; reimplementing that same release would add no optimization. The next CPU target is reducing repeated capture/guard transactions in the calling model preparation loops, with object identity and shared read ownership preserved.

A separate single draw-trace frame (6768) was requested after the ordinary capture; its timing is diagnostic only. It contains 171 recorded draw states, including 43 using VS09 / PS154066FD. Shader-family frequency is not GPU cost. Private draw-summary.json contains the full grouping; no asset contents are included here. No global stale visibility results or new render-pass splits were enabled.


## Hierarchy arithmetic boundary

Code inspection found the native 8E0F0 hierarchy batch already captures all poses, the parent worklist, and completed prefix matrices into local arrays, but holds its shared guard during the entire child-matrix calculation. Extracted that calculation into `hierarchy_snapshot` in xk_hierarchy.c. It receives only validated local arrays and returns a one-based failed work item; it does not read guest pointers, update shared counters, publish output, or acquire/release a lock. Parent-before-child order, the retained final original iteration, numeric decline accounting, and FP restoration are unchanged.

This is preparation for shortening the critical section, not an enabled threading optimization. A subsequent release must prove output/worklist privacy on the actual worker thread, keep shared counters serialized or move them to lane-owned storage, and preserve every failure path. The current helper still runs under the original guard. No hardware update was made for this refactor.

Host validation with both shipping hierarchy-normal flags enabled passed 222 full hierarchy comparisons per mode (enabled/unset/disabled/math-disabled), 117 admitted random probes in enabled mode, and 85 unchanged declines per mode. Full guest context and arena are checked against independent owned-XBE lifts. Private evidence: hierarchy-snapshot-tests and hierarchy-snapshot-tests.log. Vita-linked instruction correctness suite is running in hierarchy-snapshot-arm; do not treat it as passed until its process completes and result.json is inspected.
