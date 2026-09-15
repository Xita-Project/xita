# Experimental concurrent object updates

This branch changes the next performance experiment from small helper tuning to
concurrent execution of whole Halo object-update callbacks. It follows the
owner's explicit request to accept broken gameplay/rendering while investigating
multicore execution. It is **not a production-safe scheduling change** and does
not establish a frame-rate gain.

The bounded hardware trace of runtime `5a024a145c87ed5f3e59f5c1bbc61ada694960c0b8e8e487c4784c5b831d7e00`
measures about 17.26 ms/frame inclusive in `8FB70` in the campaign cryo room.
The selected scene callbacks still take about 35.90 ms/frame. These overlapping
elapsed-time scopes are not pure CPU-cycle measurements. The object pass is a
substantial first concurrency boundary, but cannot by itself account for the
whole gap to 30 FPS.

## Implemented prototype

- A build flag, `XV_EXPERIMENTAL_OBJECT_JOBS=1`, includes the prototype. Ordinary
  builds omit it. After the owner requested hardware deployment, the dedicated
  experimental build defaults it on; `XV_EXPERIMENTAL_OBJECT_JOBS=0` disables it.
- The second object pass queues its original `8FB70` callbacks. Two real SCE
  worker threads request cores 0 and 1. The guest owner services permitted kernel
  requests while joining the batch. This executes game routines, beyond the
  existing copy work. Earlier candidates also assigned callbacks to the owner;
  the event-service follow-up below changes that to avoid a lock/wait deadlock.
- Each lane owns a CPU context, a registered native thread stack and a separate
  64 KiB guest stack. The bounded queue holds 128 callbacks; overflow completes
  the pending batch before reuse. The next object pass and guest-fiber handoff
  join outstanding jobs.
- The pass hooks also cover copies inlined by the recompiler. Both the containing
  pass and the callback require supported executable signatures.
- Existing native math helpers remain enabled. A narrow recursive mutex protects
  their shared counters/caches. The whole callback is not under that mutex.
- Jobs use the immutable guest dispatch table. Unsupported HLE, unknown indirect
  targets, traps or exhausted instruction budgets stop with a diagnostic instead
  of entering the single-owner kernel/graphics path. Stateless memory comparison
  can execute directly. The non-yielding `NtSetEvent` is marshaled to the owner;
  other worker kernel calls remain rejected.
- The remote `object-jobs` test compares off/on/off at the same settings. It
  initializes worker reservations and native-helper locking before the first
  arm. They remain present in every arm, so this experimental baseline includes
  overhead absent from the ordinary build.

## Expected failure modes

The ordering and independence of mutable object state have not been proved.
Parent/child objects, spatial structures, animation callbacks, counters and
allocator paths can depend on one another. The stubbed callback return at the
queue site does not reproduce all volatile registers, x87 scratch state or
the original preemption budget. Separate contexts do not fix these dependencies.
The experiment intentionally exposes them; no claim of original/native context
equivalence applies to this pass.

A stopped experiment or missing work is not a performance result. The working
runtime is retained separately, and Vita3K validation precedes hardware use.
Do not enable this in a public release or imply that all three cores can run
arbitrary guest threads independently.

## Validation so far

The initial three-lane worker pool passed a host test with address/undefined-behavior
sanitizers and ThreadSanitizer: 600 synthetic callbacks and real native point
transforms execute exactly once, with three simultaneous
lanes, capacity overflow, private stacks, nested math locking, caller filtering,
profiling exclusion and restoration. Unsupported file-write HLE stops before
invocation. This tests the scheduling machinery, **not Halo's shared state**.

The frame acquisition checks, remote HTTP selection/exclusion tests and benchmark
controller checks also pass. Worker initialization failure rejects the experiment
before its first comparison arm, so a serial fallback cannot masquerade as a
multicore result. The Vita build passes and boots to the dashboard and original
Halo CE menu and campaign cryo room in the isolated emulator. The first concurrent
run creates both worker threads with the requested user-core masks, then stops
during the on-arm settling period when an object callback exhausts its loop
budget. There is no completed comparison or performance gain from this run.
The first list/datum synchronization fix below gets past that failure.
The worker affinity requests use the SDK user-core masks (`0x10000`/`0x20000`).
The native clipping helper also guards its counters and avoids owner trace globals
when called from an object worker.

Private build artifacts, logs and screenshots remain outside Git.

### First shared-state failure

The first two campaign attempts stop at the backward branch `56643` in `565E0`,
which traverses and removes cluster-list nodes. The same structure is populated
by `56670`; both use mutable datum allocation state. The experimental hooks now
hold the existing recursive shared-helper mutex over the list insertion/removal
transactions and datum allocate/free (`A9330`/`A92C0`). The four complete original
routine signatures are checked before emitting these hooks. These locks do not
cover the whole object callback. The current generated image contains one entry
body for each of these routines; this must be rechecked if discovery changes.

A focused test uses the original translated list/datum routines with the
production worker pool. It completes 28,800 remove/insert pairs, verifies every
object's membership, rejects cycles and checks both datum-pool counts. Address/
undefined-behavior sanitizers and ThreadSanitizer pass. Removing only the four
transaction guards in a private negative control produces a ThreadSanitizer
data race in datum allocation. This establishes the shared-list race and guard
coverage in the fixture, not safety of every game callback. The patched campaign completes the full
emulator off/on/off test with stable camera and restoration. Four logged
60-pass windows each complete 3,480 callbacks distributed across both workers
and the owner. The emulator is capped at 20 FPS, so its near-cap results do not
establish a hardware performance gain. Hardware validation remains pending.

### Hardware startup follow-up

The first default-on package was installed through Wi-Fi with the prior working
runtime preserved in slot A. Physical thread records confirm the two SCE workers
running on core 0 (`0x10000`) and core 1 (`0x20000`). The captured enlisted-player
screen has no object callbacks to submit, so this is thread-affinity evidence,
not completed gameplay-work evidence. The owner then reported a Blood Gulch
loading crash. The Vita was restored to the working slot while the next patch
was prepared; the first default-on package is not a successful hardware test.

Default-on emulator startup also exposed loop failures in collision traversals
`86F50` and `87EA0`. A gate that requires an active first-person or vehicle view,
fresh view data, and three distinct rendered frames prevents jobs during menu,
loading and scripted-camera states. It resets whenever that readiness is lost.
The gate's production-body tests pass, but the gate alone did not fix the
collision failure. The current candidate also guards the complete `96430`
object callback, the only direct child of `8FB70` whose audited call graph
reaches those traversals. This uses the same recursive shared-state mutex; other
parts of `8FB70` remain outside it. Its complete 1,025-byte signature is checked.
This is a pending concurrency experiment, not proof of an independent collision
subsystem or a hardware FPS gain.

### Isolating the remaining collision failure

Guarding `96430` did not resolve the startup failure. A diagnostic that keeps the
same queued contexts and private stacks but runs them through the owner alone
gets past the initial collision traversal: two windows complete 3,600 and 3,596
callbacks. It later stops at the unsupported `NtSetEvent` import (`1D665C`). This
is evidence that concurrency affects the collision failure; it does not prove
that delayed execution and stack relocation preserve all gameplay behavior.
Event signaling still needs a kernel ownership protocol before workers can use
it. The test does not fabricate a successful event result.

`XV_OBJECT_JOB_WORKERS=0` or `1` selects these isolation modes; the original default
was two workers plus the owner. The service follow-up changes the owner's role.
Tests run the production pool with all three worker
counts, verifying the expected concurrency, complete callback execution and
joins. These modes are diagnostics, not evidence of a speedup.

The two-worker diagnostic records the active indirect-call chain on failure.
It identifies `4C980`, reached through the `90900` callback table, as another path
to collision traversal outside `96430`. The failed lane's stack pointer is within
its reserved range and its bottom canary is intact. That observation does not
exclude every possible memory overwrite. The next candidate guards `4C980`
with the same recursive mutex; its full 976-byte code/jump-table span is checked.
The remaining object-pass work stays outside that callback guard. This candidate
gets through two emulator windows totaling 7,196 callbacks on all three original
lanes, then reaches the same `NtSetEvent` boundary as the serial isolation run.
That removes the observed initial collision failure in this reproduction; it
does not establish general collision correctness.

### Owner service for non-yielding events

The current candidate assigns callbacks to the two workers. The owner stays
available to execute an audited `NtSetEvent` request using the paused worker's
context, then wakes that worker with the real result and stack cleanup. Running
another object callback on the owner while servicing requests could deadlock:
it might wait for the same callback lock held by the requesting worker.
Ordinary main-thread engine work outside this pass still runs on its existing
thread. Zero-worker isolation mode executes callbacks and services on the owner.

Requests publish through release/acquire state, with coalesced semaphore wakes.
Each worker has a reply semaphore. Completion is acknowledged before draining
old notifications or reusing the batch. The only kernel service admitted is
`NtSetEvent`, whose current implementation updates event/waiter state without
switching guest fibers. Yielding, waiting, file, sound and graphics services are
not admitted by this bridge.

Address/undefined-behavior sanitizers and ThreadSanitizer pass for 600 callbacks
and 1,200 event services per worker-count configuration. Tests verify owner-thread
execution, unique previous-state outputs, real return values, stack cleanup,
requests with and without the shared callback lock, complete work and joins.
The original 28,800 list/datum transaction fixture also passes ThreadSanitizer
with the new owner/service arrangement.

Runtime `5bc6571ebd00631d7ff3ab42537b566338b3df748bc5397c975dd0382697c7c4`
completes initial emulator windows with 3,600 and 3,596 callbacks split between
the two workers, then stops at `NtYieldExecution` (`1D6640`, return `12AA9`) from
the indirect callback `39450`. The initially recorded path through floating-point
error handling was incorrect: it ended at `12A4D` (`RtlRaiseException`), not the
yield wrapper `12AA3`. Auditing the exact import callsite identifies the path
`39450 -> 95680 -> 10D920 -> 26B10 -> 32B00 -> 12AA3`.

`32B00` waits for a cache entry's completion byte. Its request path allocates an
entry, queues a 512-byte-aligned read through `33A20`, and signals the event at
`2E2D08`. Letting a worker call the normal guest scheduler would recursively
join its own unfinished batch. The next task is to establish a safe cache/I/O
ownership boundary; the static call graph is not yet a captured dynamic stack.
No yielding call was replaced with success.
The physical Vita remains on the working runtime; this is not a hardware FPS
result or a deployable gameplay candidate yet.


### Quiescent cache-file handoff (candidate)

The next candidate admits the single `NtYieldExecution` callsite under `32B00`
(return addresses `12AA9` and `32B60`). A yield request parks both workers at a
job boundary, native-lock acquisition, or their own service request. Mutex
acquisition uses a short try/wait loop so a lane waiting on another lane's held
callback lock can acknowledge the pause. The owner services events normally,
but holds replies during a cache handoff until every lane has acknowledged.

The owner then selects the existing `33AF0` cache-file thread using the game's
thread handle at `2E2D0C`. It executes that thread's real saved fiber, guest stack,
TLS and APC queue until its next yield, returning directly to the object-pass
owner. Other guest fibers are not selected during this interval. No read,
completion flag, or sample data is synthesized. Missing, suspended or unexpected
file threads fail the candidate. All other object-worker yields remain rejected.
The complete `32B00` cache transaction and `33A20` request publication are guarded
by the shared recursive mutex, with full original-body signatures checked.

The production pool passes address/undefined and thread sanitizers for 600
callbacks, 1,200 event services and 400 cache yields in each worker configuration
(2, 1 and 0). The file-step test uses the production kernel and fibers with a
synthetic read body; it verifies 100 actual event-wait/APC-delivery cycles,
restored owner context, no recursive object join, and invalid-target rejection.
It does not validate actual map bytes or Halo gameplay. Short native mutex waits
and pause barriers have a cost that must be measured after functional validation.


Runtime `872a179189b1d93c20f6ae328af49fd69e1660d18436f5a5402daab487e05a64`
passes the previous campaign cache-wait failure in Vita3K. The first saved running
capture records 97,692 completed callbacks across 28 reporting windows
(50,457 / 47,235 on the workers), 12 real event-service requests and 12 cache-yield
handoffs, with no object-job stop. The cryo-room technician and look-around
instruction render. This is emulator functional evidence, not hardware FPS or
proof of whole-campaign correctness. The physical Vita remains on working slot A
(`7f33dee4...`); the new candidate has not been installed there yet.


The same candidate completes a campaign off/on/off comparison at the emulator's
20 FPS cap (19.961 / 19.966 / 19.944 FPS, comparable view, restoration confirmed),
returns through Save and Quit, and loads solo Blood Gulch through normal menus.
Movement then reaches another explicit worker-HLE stop: `D3DResource_IsBusy`
at `184A20`, return `32084`, through `C3A00 -> 32060`. No new hardware update was
made. The next patch marshals that existing non-yielding handler to the owner;
it retains its existing result and stack cleanup, including its diagnostic
counter. It does not implement a new GPU fence or claim resource-lifetime bugs
are fixed. A fixture checks both busy and idle results through the bridge.
