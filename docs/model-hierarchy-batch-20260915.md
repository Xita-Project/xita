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
The complete 2,308-case suite also passes after rebuilding with the same
matrix-NEON compile flag as the hardware package; that optional matrix path
remains disabled at runtime in this comparison, matching the Vita configuration.
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


Six physical comparisons at 960×544 show no consistent whole-frame improvement:

| View / trial | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Base 1 | 10.177 | 10.118 | 9.849 |
| Base 2 | 10.058 | 10.071 | 10.137 |
| Base 3 | 10.122 | 10.066 | 10.136 |
| Valley 1 | 10.844 | 10.848 | 10.772 |
| Valley 2 | 10.842 | 10.874 | 10.839 |
| Valley 3 | 10.816 | 10.437 | 10.807 |

Pooled from exact frame counts and elapsed times, the base view measures
10.0788 FPS off versus 10.0852 on (+0.064%). The valley view measures 10.8200
off versus 10.7155 on (−0.966%). Every trial passes camera consistency and
restores the configured defaults. No STOP/FATAL/ABORT appears in the captured
comparison logs. These results do not justify enabling the batch by default.
The existing multicore configuration remains active.

Two subsequent hardware plasma-firing checks complete, with energy changing
from 100 to 89 to 78. These happen after the batch is restored off; they validate
ordinary operation of this build, not sustained combat with the batch enabled.
The short first trigger hold also consumed 11 energy, so it is not presented
as proof of a single uncharged shot. Driving, rocket pickup/death and campaign
crash coverage remain incomplete.

## Next boundary: private quaternion work

The trace resolves the physical lock return PCs against actual calls in this
ELF, accounting for its `0x23000` load displacement. In base-view report windows,
quaternion helper waits sum to 9.60 ms/frame across both lanes with the batch off
and 8.91 with it on. Matrix helper waits sum to 5.30 and 4.83 ms/frame. Those lane
waits overlap and must not be added to frame latency. Object-batch wall time
changes from 26.49 to 26.01 ms/frame, much smaller than the change in helper count.

The batch reduces quaternion calls from about 767 to 504 per frame in these
windows. About 440 calls per frame still release their lock only after input
capture because their output and scratch are on the calling worker's private
stack. This proves output ownership, not input ownership: the quaternion input
and shared constants still need an audit. The next candidate is a fully private
quaternion path that avoids acquiring the shared mutex only when every required
input, output, configuration and counter has a valid lifetime. Nested shared
transactions must keep their existing scope. No additional lock bypass is
implemented or claimed safe by this report.
