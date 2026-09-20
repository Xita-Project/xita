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

Host validation with both shipping hierarchy-normal flags enabled passed 222 full hierarchy comparisons per mode (enabled/unset/disabled/math-disabled), 117 admitted random probes in enabled mode, and 85 unchanged declines per mode. Full guest context and arena are checked against independent owned-XBE lifts. Private evidence: hierarchy-snapshot-tests and hierarchy-snapshot-tests.log. Vita-linked instruction correctness suite completed successfully: 2,308 fixtures in hierarchy-snapshot-arm/result.json. These instruction tests do not measure hardware cycles or FPS.


## Gated hierarchy suspension

Added `XV_HIERARCHY_SNAPSHOT=1` (default off), requiring the Halo CE native hierarchy and object workers. Capture and all validation stay under the existing guard. A live worker can release it for `hierarchy_snapshot` only if output matrices and the worklist occupy its unchanged private stack pages, the guard depth is exactly one, and private math is enabled. Owner services, copied contexts, nested scopes, shared/foreign/remapped output, direction-flag state, and active hold profiling retain the guard. A per-lane suspension token requires reacquisition before either success publication or numerical-failure accounting. This permits captured arithmetic to overlap without publishing shared game state asynchronously.

The production worker ownership suite passed 40 configurations with 600 callbacks each, including both workers rendezvousing inside the suspended section, copied-context/shared/foreign/remapped/nested rejection, one/no-worker modes, and disabled private math/fast path. These exercise the production suspension helper. They do not yet execute the whole hierarchy inside those workers. Both modified translation units also compile with the Vita compiler and the new flag enabled. Before hardware deployment, add whole-hierarchy worker integration covering captured input independence and failure restoration. Hardware remains perf45; no FPS gain is claimed.


## Whole-hierarchy worker integration

`OBJECT_HIERARCHY_TEST_BUILD=1 python3 tools/test_object_private_math.py` now executes the production hierarchy inside real object callbacks in every configuration (40 configurations, 600 callbacks each). The copied-context guarded execution supplies expected complete context, output/worklist bytes and FP status. Success and computed-output numeric-failure cases alternate. During an accepted suspension, the fixture overwrites source poses with poison bytes, restoring them only upon resume; matching outputs establish that calculation uses captured inputs. Failure cases must leave the complete guest context/worklist/output unchanged and restore FP status. Both admitted and disabled/owner fallback configurations passed.

The initial integration fixture crossed the deliberately noncontiguous physical worker-stack pages and was correctly rejected for layout. It was corrected to keep its model/node/output/worklist spans inside one page. Runtime checks were not relaxed. Earlier worker tests still cover remapped and foreign output rejection. ASan/UBSan run of the same 40 integrated configurations also passed; evidence is in hierarchy-full-worker-sanitizers.log.


Perf46 / e86b8fb builds the cumulative configuration with XV_HIERARCHY_SNAPSHOT=1. Packaging retained member names and changed only game-a.self and boot-game.txt; both hierarchy suspend/resume symbols are linked. Runtime SHA-256: `a0791691c9d32971063778532f0566024a671bfd7c952c7179256189741381f9`. Remote deployment completed: verified, restarted, and boot-confirmed in slot 0; remote status confirms perf46 / e86b8fb. Receipts are in private hierarchy-overlap-hardware. Ordinary gameplay admission counters remain pending. This is not yet a hardware-validated gain.
