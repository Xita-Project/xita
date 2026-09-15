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
  builds omit it. Its runtime setting defaults off.
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
Halo CE menu in the isolated emulator. Actual concurrent Halo execution is pending.
The worker affinity requests use the SDK user-core masks (`0x10000`/`0x20000`).
The native clipping helper also guards its counters and avoids owner trace globals
when called from an object worker.

Private build artifacts, logs and screenshots remain outside Git.
