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
  worker threads request cores 0 and 1. The guest owner also executes jobs when
  joining the batch. This executes game routines, beyond the existing copy work.
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
  of entering the single-owner kernel/graphics path. Only the stateless memory
  comparison HLE is currently allowed.
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

The production worker pool passes a host test with address/undefined-behavior
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
