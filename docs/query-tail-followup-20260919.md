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


## Perf46 hardware result and caller ownership

The campaign observer completed. The screenshot confirms the room, Marine, pistol animation and HUD on perf46. Recent ordinary windows are 12.2–12.8 FPS; the final window reports 12.8 FPS. Every captured gameplay hierarchy-snapshot report has zero admitted batches/nodes, despite roughly 3,091 hierarchy batches / 60 frames in the native routine. Therefore this private-output candidate is inactive on the observed hot path and has no demonstrated hardware benefit. Final object batch time was 1,686,731 us / 60 = 28.11 ms/frame. This is not a paired performance comparison.

Owned caller inspection explains the ownership mismatch: 8DDF0 loads the object pointer through the object table, reads its signed node-matrix offset at +1A2, adds that offset to the object pointer, and stores the resulting shared destination at [sp+24] at 8DE41. The native hierarchy reads that destination. Its worklist is private but its output matrices are not. Object-pose reports also show the optional enclosing pose scope disabled, so that scope does not explain the zero admissions.

Retain the private-output guard. The next viable shared-object design needs optimistic validation or explicit per-object ownership: capture all required poses, hierarchy links and parent matrices plus object identity; compute without touching guest memory; reacquire the guard; validate identity/generation, mappings and every input dependency before publication. Any mismatch must discard the private result, restore speculative FP status and use the original path. Account for validation cost and distinguish changed snapshots from numerical fallback. Do not simply permit shared output through the existing private-stack admission helper.


## Shared-writer audit and admission reasons

The recompiler adds shared transaction guards only to an explicit routine list in HaloHooks.object_shared. 8DDF0 itself does not receive that guard; its pose-coalescing scope is optional and disabled in the observed run. Thus a naive post-compute byte comparison under the math lock alone cannot establish that every possible writer was excluded. Before implementing shared-output publication, identify the writer/reader ownership scope as well as the lifetime/generation checks. Shared output is established by caller inspection, but other earlier admission gates may also reject calls.

Added hierarchy-ownership counters distinguishing idle/fast-path-inactive, foreign caller, nested transaction, invalid state, disabled private math, hold profiling, nonprivate output, nonprivate worklist, and admitted work. Existing restrictions and release/restore behavior are unchanged. Counters use the existing guarded/drained reporting discipline. Full production-worker integration under ASan/UBSan passed all 40 configurations × 600 callbacks, including exact 1,200 expected admissions in eligible configurations and report reset checks. These counters are not hardware evidence until a new build runs.


## Perf47 ownership evidence

Perf47 / 72f8cef verified, restarted and boot-confirmed in slot 1. Runtime SHA-256: `5d0e3ff60b4308a18794115ed96a2ab039dff0abb06f3aaf7fe36dd7be92b969`. Campaign observer completed; screenshot confirms world, Marine, pistol animation and HUD. Controls are released. Private receipts: hierarchy-ownership-hardware.

The last three 60-frame ownership reports are respectively `0/0/0/0/0/0/3094/0/0`, `0/0/0/0/0/0/3117/0/0`, and `0/0/0/0/0/0/3091/0/0` in idle/caller/nested/state/disabled/profile/output/stack/ready order. Every attempted batch passed the earlier gates and rejected at shared output. The worklist check comes later and was not reached. This rules out inactive workers, optional pose nesting and disabled settings as explanations for the observed zero admissions. It does not establish that widening output ownership is safe.

Recent ordinary gameplay reports 12.6–12.8 FPS, with final object batch time 1,689,110 us / 60 = 28.15 ms/frame and draw-HLE 11.9 ms/frame. No gain is claimed and no off/on comparison was run. Next work should target the shared object-matrix lifetime/publication boundary, retaining all earlier worker/transaction gates. Hardware remains perf47.


## Shared matrix reference inventory

Added `tools/audit_hierarchy_references.py` for the private generated-code directory. It found 57 lexical references to the node-matrix offset in 47 emitted functions, including alternate entry points. This is an inventory, not proof of complete alias coverage, unique original functions, or reader/writer classification. Private structured evidence: hierarchy-ownership-hardware/matrix-reference-audit.json.

Manually inspected consumers:

- 8B290 returns a matrix pointer; auditing only direct offset users misses its downstream callers.
- 172DE0 computes the object's matrix-array address and stores it in the caller-provided record at +0C (store instruction 172E48). This is a pointer escape; lifetime must cover later consumers.
- 8D650 feeds matrices into marker conversion and has a fallback that copies 13 words from another object's matrix array at 8D715.
- 48F50 obtains two node matrices and directly reads translation components, so matrix access is not confined to the native matrix helper.
- 8BA10 derives a parent object's node matrix for point transformation. 8D4A0 passes a selected matrix to matrix multiplication. 8F510 invokes hierarchy construction and subsequently uses a selected matrix in transform helpers.

These observations rule out assuming that the active callback is the only matrix reader. They do not prove all writes are serialized. Shared execution must retain generation/lifetime validation, capture every input dependency, and publish consistently for both native and translated readers; a worker marker alone is insufficient. No new hardware build or unsafe shared-output relaxation was made during this audit. Perf47 remains installed.

## Collision pointer lifetime follow-up

Extended the inventory to group original call-site addresses separately from emitted closure owners. The 35 emitted callers of 172DE0 represent seven unique guest call instructions (438F0, 4ABBC, 4C77D, 84C17, 171690, 1718AB, 171C42). The returned-pointer helper 8B290 has two unique direct sites, C5646 and C58A0. This reduces the manual audit surface without treating duplicated generated closures as independent game paths. Actual generated-code validation retains the earlier 47-function / 57-reference inventory. Indirect calls and downstream aliases still require manual analysis.

The descriptor built by 172DE0 contains object handle at +0, collision-tag pointer at +4, object+130 pointer at +8, and borrowed node-matrix array at +C. Copying its 16 bytes alone does not capture the referenced object data.

Confirmed consumers and minimum live ranges:

- 171690 builds a stack descriptor; 17169E passes it to 172E60. That consumer selects a node, loads the array at 172EE5, and passes the selected matrix to B5D60 at 172EF5.
- 1718AB builds a descriptor consumed by 172F40 at 1718D9. The matrix pointer is loaded at 172FD4 and inverted through B6210 at 172FE4. The original matrix pointer is retained in EBP and passed again to 868F0 at 173044. Capturing only the inverse does not close this live range.
- 4C77D builds a descriptor at [sp+20]; 4C7A3 passes it to 1731D0. That routine reads the matrix at 17327B and inverts it at 173282. Crucially, returning from 1731D0 is NOT the last use: the actor caller reloads descriptor +C at 4C7EF, selects the hit node, and calls B6560 at 4C83A. A reader scope ending at the collision query return would be too short.

These are static lifetime findings, not evidence of a reproduced hardware race. A snapshot must keep the same matrix version through query and result transformation, and must also capture or pin descriptor +4/+8 dependencies. The next implementation boundary must cover the enclosing consumer transaction, or redirect all its reads to owned snapshot storage; unlocking just 172DE0 or its immediate callee is insufficient. Existing shared-output rejection remains in place. No new VPK was deployed, and no performance gain is claimed.

## Retained-guard hierarchy assistance boundary

The current math-lock contention loop in xk_object_jobs.c retries a try-lock, optionally performs a bounded wait, and checks owner-service parking between attempts. This provides a possible assistance point for a waiting worker without releasing the publisher's shared-state guard. The existing worker threads have 512 KiB native stacks; guest stack ownership remains separate.

Separated hierarchy arithmetic into independent `hierarchy_locals(begin,end,...)` and the existing ordered parent composition. Local transforms read only captured poses and write one distinct 52-byte local matrix per validated node. Composition still follows the validated parent-before-child worklist. All guest publication and numeric failure handling remain unchanged. This adds a 64-by-13 float scratch array; the Vita compiler with shipping numeric flags reports 9,104 bytes of static stack for the inlined hierarchy entry. That is a function-frame measurement, not a whole-thread peak. No worker assistance is enabled by this refactor, and it is not a claimed speed improvement.

The assistance protocol should retain the existing shared guard for the whole capture/compute/publish transaction:

1. A live object worker holding the outer math guard offers a bounded, private local-transform range. Publish task arguments before a release-store of availability.
2. A different worker that failed to acquire the math lock may atomically claim that range, execute only the pure captured kernel, and release-publish completion. It must neither invoke guest code nor acquire another runtime lock.
3. The publisher computes a disjoint range. If no helper claimed the offer, it claims and finishes it locally; an unavailable helper must never be required for progress. Once claimed by a helper, the publisher joins before accessing results or returning from the stack frame.
4. Preserve the helper's floating-point control/status and execute with the publisher's admitted mode. Merge the task's exception flags into the publisher on success; the existing saved-status restoration must still cover numerical decline. Do not assume all worker FPSCR modes match.
5. Bound assistance so an owner-service park request remains responsive. Keep task state independent of guest service request/reply storage. Assert single publication ownership and exact completion before slot reuse.

This approach avoids widening the shared-output privacy gate and preserves the collision reader lifetime identified above. It parallelizes only local transform preparation, not dependent composition or the entire object update. Hardware admission, overlap, overhead and FPS must be established after the protocol is implemented.

Host owned-XBE comparison passed 222 comparisons per mode (enabled/unset/disabled/math-disabled), 117 admitted random probes in enabled mode, and 85 unchanged declines per mode. The production worker integration with ASan/UBSan passed all 40 configurations with 600 callbacks each, including captured-input poisoning and numerical-failure restoration. Private logs: hierarchy-two-stage-host.log and hierarchy-two-stage-workers.log. ARM instruction validation is tracked separately in hierarchy-two-stage-arm.

The full Vita-compiled ARM instruction suite completed successfully: 2,308 fixtures, including all tested rounding modes, prefix/shuffled worklists and remapped input pages (`hierarchy-two-stage-arm/result.json`). These are correctness/instruction checks, not Vita3K or hardware FPS measurements. Perf47 remains the last verified hardware deployment.

## Retained-guard assistance implementation

Added opt-in `XV_HIERARCHY_ASSIST=1`, requiring Halo CE native hierarchy and object workers, defaulting off. A single captured-task slot is published by the live outer-guard owner. A worker that cannot acquire the math lock may claim a bounded range of independent local transforms before retrying the lock. The publisher computes the other range and then joins; if nobody claimed the offer it executes the range locally. The shared guard is never released by this protocol. Owner-service parking remains ahead of the assistance check. The callback has no guest/context access, locks, service calls or waits.

The task protocol uses release/acquire publication, exclusive CAS claiming and explicit retirement. The stack-backed arguments/results remain alive until retirement. Completion publishes all writes; a helper performs no further task access after completion. The native callback temporarily adopts the publisher's floating-point mode, captures raised status bits and restores the helper's entire prior state. The publisher merges status after joining, before composition; the existing decline restores its original status. Counters report offers, helped ranges and local fallbacks after workers drain. These are admission counts, not timings.

The shared-output fixture now compares full context, private worklist, separate output matrices and native FP state against guarded reference execution. It cycles the four host rounding modes and includes an initial status bit, successful transforms and numerical decline. ASan/UBSan passed 40 configurations x 600 callbacks: 9,600 offers, 237 helper completions and 9,363 local fallbacks in this host run. This proves some real handoffs occurred; the ratio is not predictive of Vita timing. Earlier private-output fixtures also pass with assistance compiled in.

The standalone captured-task test passed ASan/UBSan and TSan: no-helper fallback, a deliberately held claimed task that cannot retire early, exact-once execution, and 20,000 raced slot/stack reuses. Both runtime translation units compile with the Vita compiler. Hierarchy static stack is 9,128 bytes plus called frames (not whole-thread peak). The object-worker compile still emits the previously observed inlined park_worker/service_state bounds warning; this change does not resolve that separate warning. Whole-worker TSan validation is recorded separately in hierarchy-assist-shared-tsan.log. No hardware update has been made yet.

Whole-worker TSan completed successfully for all 40 shared-output configurations x 600 callbacks, including different rounding modes. Before hardware deployment, run ARM callback/FP handoff validation and package the cumulative full build. The earlier 2,308 ARM fixtures covered the two-stage arithmetic, not this new cross-thread protocol. Perf47 remains the last verified installed version.

## ARM handoff validation and perf48 package

`tools/test_arm_model_hierarchy.py --assist` exercises the real Vita-compiled local-transform callback in a simulated helper FP context, deliberately changing rounding, flush/default-NaN controls and status. The harness checks that the callback restores that context before restoring the publisher context; production code then merges captured status. Full original/current/candidate comparisons passed 2,308 fixtures with 1,284 candidate handoffs. This validates ARM callback arithmetic/FP handling, not thread scheduling; the production host worker and TSan suites cover the latter within their tested scope.

The cumulative perf48 / 6796094 full VPK build succeeded with XV_HIERARCHY_ASSIST=1. Package member names are unchanged; only game-a.self and boot-game.txt differ from perf47. Runtime size 32,216,482 bytes; SHA-256 `602ac16bcb5a9189e76eaaa9f90d0fb299e2b7f5ad0b236846a6fc2d6e94b5b3`. The assistance offer/join and native callback symbols are present in the linked ELF. Private build/package/deployment receipts are in hierarchy-assist-hardware. Deployment confirmation and ordinary gameplay admission remain pending at this entry.

Perf48 deployment completed with verified=true, restart_requested=true and boot_confirmed=true in slot 0. Remote status independently confirms 0.2.0-perf.48 / 6796094. The known campaign button sequence completed; gameplay/load observation is in progress. This supersedes perf47 as the last verified installed build, but does not yet establish gameplay admission or an FPS gain.
