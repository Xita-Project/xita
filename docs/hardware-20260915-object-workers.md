# Physical object-worker comparison, September 15

The experimental object workers execute real Halo callbacks on Vita cores 0
and 1. A completed Blood Gulch comparison shows a small improvement, with
performance still far below the 20 FPS goal. Stability remains unresolved.

Runtime `dbe5edfe630fd8e030d044b01c4232334ea0a00de87d8053ac6ef7e0fd142cbf`
was installed through the authorized Wi-Fi updater and confirmed booted in
slot B. Working slot A was preserved. The saved 640x360 graphics configuration
was retained; the device reported 444 MHz after rejecting the requested 500 MHz.
The match was started through Multiplayer, Split Screen, Blood Gulch, Slayer.

## Completed comparison

| Same-session arm | Measured FPS |
| --- | ---: |
| Object jobs disabled, before | 6.151 |
| Object jobs enabled | 6.619 |
| Object jobs disabled, after | 6.126 |

Each arm settles for 60 presented frames and measures 120. The view consistency
check passed and the original settings were restored. Live simulation continues
during the test; network status polling is enabled in all arms. The middle arm
is about 7.8% above the mean of the surrounding arms. This is one comparison,
not a representative-gameplay or stability result. In particular, both disabled
arms still pay the experimental runtime's native-helper mutex overhead, so they
are not a comparison against a normal build without the experiment.

The log identifies `xv_objects_c0` on core 0 with affinity `00010000`, and
`xv_objects_c1` on core 1 with affinity `00020000`. Each normal 60-pass window
completes 5,040 callbacks, split between those two lanes. Batch wall time is
approximately 1.3 seconds per 60 simulation passes (about 21.7 ms per pass).
Lane work times overlap, include waits, and must not be summed as CPU time.
The checked guest stack peaks are 94,952 bytes on both lanes.

In one settled frame-report window, game time is 157.9 ms and the reported wait
is 1.4 ms, with 122 draws per frame. Render-slot acquisition reports no busy-slot
waits. GPU completion latency is about 47 ms, but overlaps guest/submission work;
it is not an additional 47 ms of CPU blocking. These observations do not support
treating a per-frame full GPU finish as the dominant measured delay in this run.

## Stability observation

After the comparison, the first charged plasma-pistol shot completed with a
responsive status endpoint. Following the second shot, the status request timed
out; two additional observations also timed out. No restart was performed and
no post-failure log was obtained. A Wi-Fi timeout alone cannot distinguish a game
crash, a hang or a connection failure. The error screen and log are still needed.
Driving and campaign hardware validation are outstanding.

Before deployment, the same runtime completed an emulator Warthog sequence of
four seconds reversing, six seconds forward and eight seconds steering in the
field. Captures and vehicle-camera traces show movement; the checked stack peak
was 99,320 bytes. This is functional emulator evidence, not hardware performance.

## Reducing the experiment's locking overhead

The next candidate retains the shared recursive mutex for active transactions.
It avoids OS locking outside the worker interval: `running` is published before
waking any worker and cleared only after consuming every completion semaphore.
At that point the guest owner is the only caller of the guarded helpers.

For worker recursion, only the outer acquisition and final release enter the
OS mutex. Each worker maintains its own depth. Every entry still checks the
pause protocol, including recursive entries while holding the mutex, so owner
cache services continue to require acknowledged quiescence. A completed callback
with unbalanced lock depth stops the experiment. Owner service calls retain
their ordinary mutex behavior. `XV_OBJECT_LOCK_FAST_PATH=0` selects the previous
locking behavior for diagnosis; it is not a graphics option.

Counters distinguish idle-owner calls, actual acquisitions, nested scopes and
contended acquisitions. Wait time is collected only on contention and overlaps
between workers. Counters are consumed after the join.

Host validation covers nested cleanup while a shared non-atomic value remains
protected, real native point transforms, simultaneous events and cache requests,
and quiescent registration/vertex-pointer handoffs. The production pool passes
TSAN and ASAN/UBSAN with 2/1/0 workers; the previous locking path also passes
TSAN. Original bitmap-cache and spatial-list bodies pass TSAN, including 28,800
list remove/insert pairs. This does not prove all original object dependencies
safe under reordered updates. Hardware performance of the new lock path remains
unmeasured.

Private captures are under `validation/engine-restructure-20260914T2300Z` in the
worker-sizing workspace. The comparison receipt is
`physical-stack256-object-compare/result.json`. No game assets or private logs
are included in this document.
