# Query-bearing first-scene ownership prototype

This private scheduling model passes its bounded ownership tests. It is not production wiring or a GPU lifetime qualification. No runtime, Makefile, device, emulator, benchmark or graphics default was changed.

## What was exercised

Base: `449aba89e42b7c4031e7985f72f50c5fe7c1fd4b`, private worktree `query-bearing-prefix-20260917`. Reproduce with `python3 tools/test_render_prefix_ownership.py --out PRIVATE_DIRECTORY`.

The driver extracts the retained `new_cmd`, Clear merging, visibility Begin/End/read/wait/generation/publication, RTT registration/release, constant allocation/binding, depth-list/scene proof and RTT replay. The candidate is a generated resumable copy of that RTT replay, suspended only after its first original EndScene and before opening the next scene. A known successful command on a different target proves closure. The early consumer uses sealed command/UI/constant/visibility bounds, a captured completed-upload ticket, and no live recording counts. Original descriptors and owned payloads stay in their frame slot.

The physical visibility setup is separated from the original public `submitted` loop. The former runs exactly once before prefix writes; the latter runs only after the Present barrier. Constants copy only the new immutable high-water range. Neither final setup nor late queries can zero already-written counters. At full publication, the whole-list query planner reconciles the provisional cut. A matching query-free suffix may publish the exact first-scene results after Present; a late query or UI retains the provisional word and uses final completion instead. No word is reset while GPU-owned. Both paths retain original frame-slot ownership until final completion/error drain.

The asynchronous GXM model defers all vertex/index/constant/texture reads until a modeled completion; the producer mutates original guest buffers before that completion. Thus an immediate-submit stub cannot conceal a borrowed source pointer. Original and resumed rendering traces are compared by ordered calls, arguments and exact sampled payload hashes. The real uniform binding uses the original float4 offset/count convention.

## Results

`qualification/receipt.json` pins every extracted source/include, executable and log. ASan/UBSan passes **43 schedules and 16,770 comparisons**:

- 30 ordinary whole-vs-prefix comparisons: three frame identities including `UINT32_MAX`, prefix complete or still pending at Present, ordinary/late-query/late-UI shapes, a proof command that is an unsubmitted clear and subsequently merges, and six variants of the physically witnessed structural shape (44 first-scene commands followed by 77 draws across eight existing suffix scenes).
- Eight declines before GPU mutation: open query, UI, no target transition, invalid query slot/serial/target, absent queries, and rejected/provisional-command shape without a successful closure witness.
- Two actual RTT-registration schedules: drain after consumption or while the submitted prefix still awaits consumption; allocation failure, incompatible live target, release plus current-frame non-reuse, and eventual retired-entry reuse. The drain contract finishes only submitted work; no unfinished full-frame ticket is requested and no frame storage is released.
- Three begin/end/late-target errors retain ownership until the modeled pump error drain.

All 30 normal comparisons preserve rendered event/data order and final IDs, generations, pixels and complete four-entry histories. Current-generation reads remain INCOMPLETE before Present; all four prior generations remain readable and the old submitted generation remains waitable. One publication notification occurs per frame. Timestamp fields intentionally differ because submission is deliberately rescheduled.

There are **12 explicit late-query/UI final fallbacks**. Their provisional first-scene notification remains alive but is ignored for result publication. Its presence/position can differ from whole-frame replay; rendering and final result traces still match. Ordinary eligible cases preserve notification positions as well as original scene counts. No ordinary extra EndScene or Finish occurs. The initial two RTT allocations already call Finish; the test asserts that no other ordinary Finish is added.

Four separately compiled wrong candidates fail meaningful assertions: publication before Present, re-zeroing visibility at Present, retirement at prefix completion, and retaining the original one-shot constant-ready boolean. Their exact failures and executable hashes are recorded. Generation refuses source drift and Python optimization mode.

## Costs and limits

The model adds one bounded metadata cursor and no command-array or guest snapshot. The 44+77 shape uploads the same 1,920 constant bytes: 704 before the prefix (including the already-captured proof command's constants) and the remaining 1,216 at final publication. This changes upload granularity and adds a flush invocation; it is not a measured CPU cost or hardware gain. Early sealing can also reduce real uploader batching, which this model does not measure.

This is not a complete real recorder/pump experiment. The fixture's draw capture copies known inputs into owned arrays; it does not execute `record_draw`, texture decoding, the asynchronous upload worker, shader/reflection/depth-program qualification, real main.c packet/display ownership, real guest flare barrier, OS worker interleavings or actual GXM fences. Depth scene policy is retained, with a fixture shader-proof predicate. Publication tests use the real visibility/history primitives and explicitly model the Present consumer barrier. The drain fixture models marshaling to the pump; the retained production drain still calls Finish on the recording thread and cannot be used unchanged with a live partial packet. Prior full-packet contention and cancellation/shutdown/configuration are not covered by this model. There is no claimed frame-rate gain, hardware timing or production readiness.

## Minimal real integration boundary

1. In `xv_d3d.c`, offer at most the first scene only after a complete successful recording operation proves its target transition. Capture immutable counts and target identity; never offer inside `new_cmd` or before a possible rejection. Keep the first proof command unconsumed so later Clear merging remains safe.
2. In `xv_vertex_upload.c/.h`, seal and copy the exact upload ticket on the recording owner; pump waits that immutable token rather than rereading mutable per-slot state. Appends use disjoint storage, and final waits capture a second token.
3. Add a separate provisional request/acknowledgment in `main.c`. Reserve the packet/mesh/display generation while keeping `g_frame_requested`, final retirement and query-publication scanning unchanged until Present. The pump alone opens/ends scenes. At Present it resumes the existing cursor instead of replaying or clearing the prefix. The prior full-packet FIFO and display capacity still gate admission.
4. Marshal render-target drains through that provisional protocol. The pump must consume any released prefix, await its completion and acknowledge quiescence without requiring the unrecorded suffix. All resource references remain owned; allocation follows the acknowledgment. Shutdown, resolution changes, failure and OFF restoration must include this state.
5. Reuse a separately generation-owned provisional notification word; retain final notification ownership. Reconcile the complete query plan after the original flare drain, preserve one physical initialization, and use final completion on late queries/UI. Validate actual worker interleavings and hardware API call ownership before any deployment decision.

No data-ownership obstruction was found within the model. Those real synchronization boundaries, not another synthetic draw case, are the next qualification gate.
