# Render-state snapshot implementation

Status: the owned pose payload and an opt-in render-palette worker are implemented.
**Perf.37 is boot-confirmed and campaign logs verify palette reuse on hardware.**
This is not a complete simulation/render split. No FPS gain is claimed.

## Implemented ownership and payload

`recomp/kernel/xk_render_snapshot.c` extends the existing two-slot ownership
exchange with complete, copied model poses. Each published state has an explicit
world generation and simulation tick. Each pose contains its full salted object
handle, model identity, node count and up to 64 complete 13-float matrices.
There are no guest memory pointers in the published payload.

The consumer pins one immutable state while the producer fills the other slot.
A pinned slot cannot be overwritten; a busy producer returns without waiting.
The caller must handle that outcome explicitly, not silently advance simulation
against missing render state. Duplicate object identities, bad counts and a full
256-object capacity reject the whole attempted publication. An empty publication
represents an empty scene. Consumers reject a different world generation and
match both salted object identity and model identity, preventing a recycled
object-table index from selecting an unrelated pose.

This is caller-owned storage, with no runtime allocation or thread startup.
Two banks consume roughly 1.7 MiB; a gameplay allocation must be measured against
actual memory headroom. The caller must reset only after all readers stop.
This is a pose schema, not a claim that all render-visible dependencies fit in it.

## Validation

`tools/tests/render_snapshot.c` exercises 10,000 producer ticks with concurrent
consumer yields and full matrix-content checks. It also covers source mutation
after copying, pinned-slot backpressure, world transitions, changed object salts,
changed model identity, object removal, empty scenes, invalid inputs, duplicate
identities and capacity exhaustion. ASan/UBSan and TSan runs pass. The production
payload source compiles with VitaSDK ARM/Thumb and `-Wall -Wextra -Werror`.
These tests prove the exercised ownership rules, not engine integration or GPU
resource lifetime.

Reproduce the host test from the repository root:

```sh
cc -std=gnu11 -O2 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -pthread \
  tools/tests/render_snapshot.c recomp/kernel/xk_render_snapshot.c \
  recomp/kernel/xk_frame_snapshot.c -o /tmp/xita-render-snapshot-test
/tmp/xita-render-snapshot-test
```

Use `-fsanitize=thread` instead for the separate concurrency check.

## Integration boundary still to implement

The audited generated primary 5B4A0 reads the full object handle from the entry
pointed to by EDI, then follows the object table. Its descendants include cache
lookup/refresh through 5AE10 and model traversal through 5B190. A26B0 receives
pose input through ECX, loads a model resource, and writes shared render globals
before producing its stack-local palette. The palette buffer address alone is
not a safe cross-frame object identity. Copying those matrices and removing the
object-job join would leave these other accesses shared.

The next integration must:

1. Capture poses at a completed simulation boundary with the real world epoch,
   full object identities and all attachments needed by the selected draw path.
2. Pin a completed generation at render start and route the selected model-pose
   consumers through that view. Preserve live cache refresh until its inputs and
   writes have an explicit owner; do not mix old bone matrices with a new root.
3. Establish ownership for visibility, lighting, model/cache lifetimes and
   creation/removal. The current payload intentionally does not invent those
   layouts or claim to cover them.
4. Dispatch independent preparation against owned input/result buffers. Remove
   only joins whose readers have actually moved to immutable data. The existing
   command-buffer/GPU retirement path remains responsible for GPU resources.
5. Build an opt-in candidate, verify the running revision after restart, and
   compare ordinary checkpoint and crowded-campaign gameplay on physical Vita.
   Record missed publications, snapshot age, memory use, frame time and visual
   correctness. No predicted FPS increase substitutes for that test.

## Opt-in render-palette worker candidate

`XV_POSE_PIPELINE=1` now connects a bounded experiment to the presenting owner's
primary 5B4A0 model scope, the admitted A26B0 palette helper, D3D frame boundaries
and both audited map/BSP retirement entries. This is **render-palette overlap**,
not a complete double-buffered simulation and not asynchronous AI/physics.

The owner copies admitted pose and bind matrices into one of two input banks.
At frame end a Core 1 worker receives that bank and computes complete palettes
without guest pointers, game callbacks, shared object updates or GXM calls.
A separate two-slot result exchange lets the next render use a complete previous
frame's palette. Results require the same full object handle, model, pose-source
identity, bind-source identity, matrix count, FP control and exact bind matrices.
The previous pose is intentionally allowed to differ: this experiment introduces
one frame of visual pose latency. New identities, changed bindings, unavailable
results, exceptional inputs, over-capacity sets and results older than one frame
retain current-frame computation. The owner never waits for palette completion.

All bones, including the final matrix, come from the same completed palette.
The last current matrix call still reproduces the guest register continuation
into a temporary result, rather than mixing that matrix into the old visual pose.
The skipped prefix's FP exception accumulation is not an exact current-frame
replay; the admitted domain masks exceptions, but this remains an experimental
rendering change rather than an original-execution-equivalent optimization.
Original cache refresh, visibility, lighting, simulation joins and GPU retirement
remain in place. Different model variants for the same object fall back.

The feature requires the CE profile, native palettes, object jobs, owner-phase
tracking and the model-detail hooks. Ordinary builds leave it off. The emitted
scope and retirement entries check owned-image signatures. The retained-shard
updater `tools/apply_pose_hooks.py` uses the same profile hook output, preflights
all three sites and accepts only its exact prior blocks. Build configuration
changes rebuild the affected guest shards, palette helper and recording path.

The actual worker state machine passes ASan/UBSan and TSan tests with input
mutation, object/model/bind changes, deliberately delayed work, stale results,
world invalidation, stress, shutdown and restart. A production-palette fixture
covers 1/4/16/64 matrices and verifies whole prior palettes plus current register
continuation. VitaSDK compiles both the worker and integrated palette source.
These checks are prerequisites, not hardware acceptance. The hardware test must
show nonzero `[pose-pipeline] reused` counts and acceptable moving-model visuals
before any FPS result is attributed to this path.

## Perf.37 deployment

The updater verified source `efb502b`, runtime SHA-256
`c9ec52fd2bec2034d592b234ca4c7adb900f18408fb6e3d96137b6a1f6b63c6f`,
and confirmed slot 1. `/status` reports `0.2.0-perf.37 / efb502b` and a dashboard
capture shows that version. Perf.35 remains the confirmed fallback in slot 0.
Only `game-a.self` and `boot-game.txt` differ from the perf.35 package; the update
contract and packaged graphics assets are unchanged. Build and deployment
receipts are under `../pose-pipeline-hardware/`.

The first menu windows report 60 reused palettes per 60 frames with zero busy
skips. This proves execution on hardware, not a campaign performance improvement.
The ordinary saved-campaign sequence is running to qualify NPC poses and timing.

## First campaign smoke result

The preserved startup log reports `pose-worker` on core 1, affinity `00020000`,
priority 65. The campaign runs on perf.37 and the final six 60-frame windows
report 11,272 palette reuses, 533 reported fallback attempts, 8,929 captured jobs,
zero busy skips and zero capacity rejects. Repeated passes can reuse one captured
object, so these are not distinct object counts. A gameplay screenshot confirms
world, weapon and HUD rendering; longer moving-model correctness remains a user
playtest item. No STOP or fatal marker occurs in the retained smoke log.

The automatic collector expected the older saved camera and did not accept the
new position. It was explicitly stopped (its orchestration exits on that
termination); no subsequent settle/route script ran. The independent full log
confirms campaign gameplay at camera (-29.00,37.05,0.62), forward
(-0.98,0.21,-0.06). Its last six frame windows average 178.78 ms, about 5.59 FPS,
with 25.87 selected model calls per frame. This is a different view/workload from
the preceding baseline captures, so it establishes neither a gain nor a regression.
The implementation is active, but sustained 20 FPS is not achieved.

Receipts: `../pose-pipeline-hardware/gameplay-smoke.log`, `pose-summary.json`,
`gameplay-smoke.summary.json`, `gameplay-status.json`, and `load-check.png`.
All remote navigation has ended; the user can test perf.37. The next observation
should focus on moving NPC poses, camera turns and transitions, alongside FPS.

## User gameplay follow-up: no reported FPS gain

The user reports no FPS gain on perf.37. The subsequent capture confirms that
the candidate is running: the final six 60-frame windows contain 13,627 palette
reuses, 229 reported fallback attempts and 10,620 captured jobs, with no busy
skips or capacity rejects. Those windows average 153.53 ms per frame (6.51 FPS).
Selected model preparation still spends 24.07 ms per frame in its traversal
child, including a nested 10.92 ms draw-wrapper measurement. These scopes must
not be added together. This is not a matched A/B capture and does not establish
a numerical gain or regression against perf.35.

The experiment therefore remains unqualified and default-off in ordinary
builds. Successful palette reuse alone is not evidence that work was removed
from the frame's critical path. This test covers model-palette preparation,
not a full simulation/render snapshot split. Next work should identify the
remaining traversal/draw-wrapper cost and update dependencies before extending
buffering to more state. Perf.37 remains installed for now; no rollback or
settings change was performed during this log collection.

Receipt: `../pose-pipeline-hardware/user-play-1789859498966021178/`, containing
the status, gameplay log and summary.
