# Pi movement abort: animation permutation traversal

The current perf291 host harness aborts during the recorded a30 pod-exit route.
This is a reproduced host failure, not proof of a physical Vita failure.

The replay added only fatal-path caller logging to the host runtime. All other
base objects matched the previous clock-corrected harness; the navigation
observer was unchanged. The worker limit remains one million backedges per job.

The ARM return address `0x125afbc` resolves against the exact private
`perf291-host/navigation/harness-fault` ELF to retail `A3080`, at the backedge
following `A3122`. The indirect parent is `4C980`, lane 1. Both captures show
ECX, EDX and ESI zero at the fault. The replay exited 134; logs are retained
privately in `navigation/fault-replay.log`.

The private Halo CE Universal reference identifies the corresponding operation
as `animation_choose_random_permutation_internal` in
`source/models/model_animations.c`. It chooses a random value, walks animation
permutations, and follows the next index until the normalized weight accepts the
choice or the index is NONE. This agrees with the retail loop's weight read at
entry+0x44 and next-index read at entry+0x38. It is not a synchronization wait.

At the retail loop, ESI is the animation-array base and EDX contains the current
index. Their zero values are consistent with traversing an invalid/missing array
at index zero. This does not establish why the array became invalid: the entry
graph argument, tag lookup, mapping, loaded data, and possible cross-thread
mutation still need capture. Do not attribute the cause solely to threading.

Next capture the input graph/index, graph pointer, animation block count/pointer,
and bounded loop-entry data using the retained build's image-address semantics.
Then determine whether the same inputs fail on the owner path. Do not suppress
the animation, change its random-number sequence, raise the budget, or retry a
partially executed object update without a rollback proof.

The older handoff's description of this routine as an owner-only wait is not
supported by this routine mapping. Preserve existing guards while investigating.
No production fix or hardware performance improvement is claimed.

## Entry capture confirms wrong tag class

The read-only entry replay also exited134. At the failing worker context it
received graph ID00000000,index0,kind1. Tagtable803A6024 entry0 has class73636E72
(`scnr`), graphpointer8094D004, count0,data0. Earlier entries on the same worker
contexts had valid `antr` tags and nonzero animation arrays. Owned a30 map parsing
independently identifies entry0/data8094D004 as the scenario. Thus bad input is
present before A3080 executes; a race inside its traversal is not required to
explain the failure. Source of that input is not yet established.

Guest stack identifies caller returnA31C5, then41C7A. At41C6A the graphID is
loaded from [EBX+44], and ESI passed to A3130 is a unit animation-state pointer.
The fault stack saved ESI262 suggests a null base for that state pointer. Trace
the enclosing object/weapon lookup; do not return NONE as a workaround.
Private full record: perf291-host/navigation/animation-entry.log.

## Object entry and owner-only replay

The next read-only probe at 41B40 confirms handle E4C0000B still reaches the
animation update, but its header entry 800B942C has salt zero and object pointer
zero. Thus the animation routine receives a cleared object slot upstream of its
own traversal. This does not yet establish when or which thread cleared it.

The same instrumented binary and 24 frame-based movement commands were run with
only XV_EXPERIMENTAL_OBJECT_JOBS changed to 0. It reached camera frame 6224 and
ended at its deliberate 240-second timeout (exit 124), with no bad animation
entry, null object entry, or instruction-budget abort. Worker-enabled replays
aborted near frame 1400. Paths are slightly different, so this supports worker
lifetime/order dependence rather than proving an isolated race or a Vita fix.
Private evidence: navigation/ownercheck.log and ownercheck-summary.json.

The reference objects_update scans live identifiers before each sequential
update; object_update also recursively visits children and siblings. Xita's
queue copies guest contexts and executes them later. A stale queued handle is
a candidate explanation, not established fact: next probe at 4C980 entry checks
whether the slot is already cleared before that update body starts. Do not
silently skip jobs, retry partially executed updates, or relax guards.

## Guarded update entry: invalid before animation body

The next replay exited 134. At 4C980 entry, after its existing shared-transaction
guard, handle E4C2000F already had salt 0000 and object pointer 00000000. Caller
was 90995. The same context then reached 41B40 and A3080 with the invalid data.
This excludes the A3080 traversal as the origin and narrows the lifetime window
to before 4C980 entry. It does not yet prove the object was invalid at job start.
Private record: navigation/updateentry.log; only code_008.o changed relative to
the previous instrumented binary (updateentry-object-audit.json).

Next private probe stores header salt/pointer at queue admission and job start,
and reports these snapshots only when 4C980 observes an invalid slot. No guest
state writes, skipped jobs, relaxed guards, or lifetime fix are introduced.

## Lifetime capture and lifecycle ordering candidate

Lifetime replay exited134. Lane1/job12/handle E4CB001F had queued pointer80128338,
saltE4CB, but at job start both were zero. Thus it was valid at admission and
cleared before execution. Full private log: navigation/lifetime.log.

Retail second-pass call902DA returns902DF, then902E5 tests deletion bit8 and
902F8 calls8E830. The reference agrees: the second pass updates newly created
objects and then deletes objects marked for deletion. It is not an independent
requested-update list as the old scheduler comment claimed. Deferring these
calls until pass finish can move an update beyond its object's deletion.

Candidate change in recomp/kernel/xk_object_jobs.c executes return902DF calls
inline, draining previous queued work first; return90299 regular-pass calls
remain parallel. It preserves the original guest context and does not silently
skip stale jobs. The obsolete SITE2 toggle cannot restore unsafe deferral.
The captured queue source is not logged, so the next full replay must establish
whether this candidate resolves the observed failure; do not claim it yet.

ARM host build passed. Only xk_object_jobs.o differs from the lifetime diagnostic
(lifecycle-object-audit.json). Same movement replay is being run in fresh private
Pi directory runs/codex-perf291-lifecycle-20260927. Hardware is unchanged.

## Vita candidate prepared, not deployed

Private object-lifecycle-292/xita-perf292c.vpk builds successfully from an isolated
copy of the retained perf291 stage. Object comparison confirms only
xk_object_jobs.o plus four version-identification objects changed. Packaging
retains perf291 assets/launcher/Halo2 and replaces game-a.self and boot-game.txt
only. Receipt and SHA256 values are in object-lifecycle-292/package-receipt.json.
No hardware deployment yet; the Pi lifecycle replay remains live past frame3300
without the previously reproduced abort. Collect its terminal outcome first.

Lifecycle replay completed its full 240-second timeout (exit124), last camera frame 6138, zero lifetime-invalid reports and no instruction-budget abort. Private lifecycle.log/lifecycle-summary.json retain the evidence. This supports the fix for the reproduced route; broader gameplay and hardware performance remain unverified.

## Hardware update initiated

Before deployment, Vita status confirmed perf291, CPU444, and the NPC encounter
was visible. Current protected test-save udata/tdata were copied read-only to
object-lifecycle-292/save-before-update; all15 files and the file set matched two
reads. Full pre-update log captured: before-update.log (58,654,732 bytes).

Authorized update --apply started for xita-perf292c.vpk. Upload/apply session81081
is pending; do not assume installed or restart another update without inspecting
that session and device update-status. Matching launch-normal.py prepared in the
candidate directory with perf292 assertion, same32 environment entries and
XV_TEST_SAVE=a30-perf211. Hardware FPS and stability remain unverified.

Update session81081 completed0. Device verified34858926 bytes and confirmed
slot0 boot SHA d576f9cc44ba4b486c801b0da7d87c5ad4880b1d193b3cdfa89162c3cabc4f69.
update-status agrees; deployment.json retained. Perf292 launch-normal.py passed
its version/dashboard assertion and started same-settings a30 sequence (session
80915). Collect its final screenshot and game readiness before timing claims.
Pre-update last1200 samples: mean59.557ms (16.79FPS), p9574.059, p9983.041,
max126.846;1186over50ms,7over100ms. NPC encounter visible, active combat not
established across the whole interval. No equivalent post-update sample yet.

Hardware launch sequence80915 completed0, confirmed perf292/CPU444, but its final
screenshot shows loading. Collector95449 is live waiting for loaded1/active1/
director1 before taking its45-second sample. Latest readiness logs show cache
I/O and continuing present/thread telemetry, not active gameplay. Do not count
loading FPS or restart merely because observation is slow. Collector has a
600-second readiness deadline; inspect terminal outcome and screenshot.

## Initial perf292 hardware result

A30 loaded successfully. Collector95449 completed0 after loaded/active/director
telemetry and lifepod screenshot confirmation.900 complete settled frames:
mean48.114ms=20.784FPS,p9558.693,p9963.860,max68.723;235over50ms,noneover100ms.
Same360p/444MHz settings. Similar to perf291's earlier pod sample; this is not
an established speedup and does not meet sustained20FPS combat/cutscene scope.

AR-fire plus pod-exit gameplay collector39161 started at timing frame6875.
Keep monitoring its existing session; it releases input in finally and will
save bracketed intervals/screenshots. Do not compare this pod sample directly
to the pre-update NPC encounter. No hardware crash observed so far, but15-minute
active stability and NPC encounter qualification remain outstanding.

Gameplay collector39161 completed0. AR fire85frames mean63.409ms=15.771FPS,
p9578.224,max94.433. Pod-exit movement84frames mean64.376ms=15.534FPS,
p9581.150,max302.838 (one>200ms). Settled outdoors938frames mean48.213ms=
20.741FPS,p9558.677,p9983.227,max103.632;167>50ms,2>100ms. Screenshot confirms
outside pod, no heavy NPC combat. These closely resemble perf291's prior AR
and outdoor means; no material FPS gain established. Lifecycle fix remains a
correctness improvement; next target remains NPC/AI path and firing preparation
cost, plus checking lifecycle ordering over sustained gameplay.

## Outdoor advance and firing attribution follow-up

Perf292 route56805 completed, then corrections91908 and39697 completed; all
inputs released. First route image faced cliff; corrected view faces open ground
and trees. Radar contacts appeared in the intermediate view but visible NPC
combat is not yet confirmed. Collector47216 is sampling this advanced outdoor
position; do not call it combat solely because files use encounter names.

Existing firing log around frame6900 shows scene helper wall increasing from
47.66 to60.20ms across neighboring60-frame reports, helper CPU46.37 to56.94ms;
FA920 inclusive elapsed36.08 to47.79ms/frame. Reports overlap firing boundaries
and nested/wait time, so these are attribution clues, not additive exact costs.
This suggests both scene preparation and game update costs, requiring focused
measurement before a native replacement. Keep existing qualified gains stacked.

Advanced outdoor collector47216 completed0:780frames mean53.559ms=18.671FPS,
p9566.771,p9989.955,max267.103;425over50ms,2over100ms,1over200ms. Nearby
report intervals show helper wall43.6–44.2ms, helper CPU34.7–36.9ms and
completion-noticed delay6.4–8.6ms. Inclusive owner FA92040.9–46.4ms/frame.
Thus this sample includes owner/scene scheduling pressure; timers overlap.

Follow-up movement/fire48613 and turn75939 completed, controls released.
encounter-open.png shows two radar contacts; encounter-contacts.png faces a tree
and still does not prove visible multi-NPC combat. Keep hardware alive and avoid
calling the existing sample a verified combat result. No crashes observed.

Next useful profiling target remains measured path refresh15A290 and its edge
builder139420. Old Pi edge census searches up to1024 prior keys per call and
uses stderr: do not transplant it into hardware as low-overhead timing. A
hardware census should use bounded constant-time bookkeeping, sparse timing,
and existing asynchronous reporting, while preserving original execution.
