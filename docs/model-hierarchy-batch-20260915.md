# Native model hierarchy batch, September 15

The next step after the [combined object-worker comparison](object-workers-combined-20260915.md)
is to reduce the work inside model preparation. A new optional native batch
implements the child-node portion identified in the
[pose boundary audit](pose-job-boundary-20260914.md). It keeps the current object
workers and shared transaction guard. It does not start another worker queue.

## What changes

For the supported Halo CE 3925 executable, the original routine handles the root
and attached-object branches. The batch then validates the remaining hierarchy,
snapshots its local poses and parent transforms, and computes child transforms
into temporary storage. It publishes results synchronously before returning to
the original loop. One final original iteration preserves the guest register,
floating-point scratch and call-stack effects of the loop.

The arithmetic kernels take ordinary arrays rather than guest contexts. They
retain the existing multiply/add order, intermediate precision and float spills.
This gives future scheduling work a bounded computational unit, while the
current implementation keeps guest publication under its existing lock.

Validation rejects cycles, repeated nodes, unavailable parents, out-of-range
links, overlapping output/input memory, noncontiguous host mappings, insufficient
preemption budget and unsupported numeric states. Rejection preserves guest
memory and context; a speculative numeric failure also restores native FP status.
Very short remaining tails use the original loop to avoid copying a large model
for too little work. Roots, exceptional inputs and unsupported executable hashes
retain their original path.

The feature requires `XV_NATIVE_MODEL_HIERARCHY=1` at build time. It defaults off
at runtime; the same-named environment variable or the remote `model-hierarchy`
comparison enables it. Disabled calls avoid the shared mutex. Build dependencies
track both enabling and disabling the hook, including generated shard changes.

## Validation and limits

Host differential tests compare the entire context and guest arena against both
the independent original lift and the current native-leaf implementation. They
cover shuffled node indices, already-consumed prefixes, root entry, all four
rounding modes and unchanged rejection paths. ASan/UBSan checks cover the same
fixtures. The production worker-pool fixture exercises 600 batches per run,
checks outputs and counter retirement, and passes with ThreadSanitizer.

The final Vita-compiled ARM suite passes 2,308 cases and additionally compares native FPSCR flags,
flush-to-zero/default-NaN modes, guest scheduling and page mappings. Fewer modeled
instructions in an accepted batch are not an FPS result: those tests model
memory-copy imports and do not account for hardware cache or synchronization
costs. Three preserved complete model-update calls also reproduce the full guest
context, memory and FPSCR using the linked candidate ELF. Their modeled
instruction counts change from 13,366 to 11,053; 4,036 to 4,036; and 6,312 to
6,524. This small sample shows both an accepted benefit and fallback overhead.
An additional 148 ARM comparisons cover the subsequent root fast return.
Root entry now avoids configuration reads and the shared mutex entirely,
following the first emulator run's high count of bounds-only calls.

Physical comparisons must keep resolution, graphics and worker settings
unchanged, and must confirm that batches are actually admitted.

Generated original code, owned executable data, replay fixtures and captured
logs stay in private validation storage. This change establishes neither general
object independence nor a stable 20/30 FPS result.

## Emulator gameplay

Both iterations complete an off/on/off comparison with valid camera checks.
The refined version reports about 1,620 batches and 8,010 prepared child nodes
per 60-frame window in the tested view. Returning before the root's mutex
reduces bounds-only entries from roughly 7,830 to 360 in those windows. This is
an observed reduction in unnecessary calls, not a hardware speed claim.

With the refined batch explicitly enabled after the comparison, normal and
charged plasma shots consume energy from 100 to 99 to 88, and camera movement
continues. The captured logs contain no STOP/FATAL/ABORT. Emulator testing uses
360p with a 20 FPS cap and therefore does not measure the physical performance
goal. Rocket pickup, death, driving and campaign still require broader checks.

## Hardware candidate

The updater has confirmed runtime `f2b1906a94c4` in slot B. Slot A was verified
before the upload and remains available for rollback. The package contract and
all non-runtime assets are unchanged. Hardware measurement uses the same binary
for the off/on/off arms; the new batch defaults off outside an explicit
comparison. Existing object workers, lightweight mutexes and private math remain
selected.
