# Periodic gameplay stalls — September 15

The 60-frame profiling report caused substantial pauses on the physical Vita.
Grouping its small synchronous writes reduced the measured median report cost
from **383.057 ms to 11.277 ms** in stationary Blood Gulch samples. The fix is
installed through the updater. One 145.840 ms outlier remains; this does not
resolve every camera-turn, firing or combat stall.

## Physical evidence

Both builds use native 960×544, the existing object workers and rendering
optimizations. Index reuse remains disabled. The report timer includes formatting,
reporting and synchronous writes, excluding its own final timing line. Previously,
this reporting time was hidden in the following game interval.

| Report sample | Separate writes | Batched writes |
| --- | ---: | ---: |
| Initial menu report | 181.970 ms | 8.097 ms |
| Stationary Blood Gulch median | 383.057 ms (7 reports) | 11.277 ms (6 reports) |
| Blood Gulch range | 335.726–682.846 ms | 10.143–145.840 ms |

The baseline gameplay reports are 389607 / 347484 / 383057 / 682846 / 335726 /
377710 / 410982 us. An earlier baseline report reached 1114356 us. The batched
reports are 10143 / 145840 / 10304 / 11216 / 14272 / 11338 us, frames 3840–4140.
The batched capture follows a 50-second interval without remote requests or input.

These are report wall times, not isolated disk-syscall measurements. The two
sessions spawn at different stationary positions, so this is **not a controlled
comparison of overall gameplay FPS**. The typical report pause falls by about
97%; the remaining outlier needs more investigation. At the observed gameplay
rates, reporting every 60 frames aligns with the user's recurring 4–8 second
hitch. That establishes a substantial source of periodic stalls, not the cause
of every dropped frame.

## Implementation

Only the periodic report's calling-thread writes are grouped in a fixed 32 KiB
buffer. End-of-report writes them synchronously before gameplay resumes; there
is no background queue, new worker or per-frame allocation. Other threads and
calls outside the scope retain immediate logging. Oversized reports spill
complete chunks without dropping text. An explicit owner flush writes pending
report bytes before syncing. `XV_PROFILE_BATCH=0` restores separate writes;
batching is enabled by default.

Host tests cover grouping, foreign-thread calls, nested scopes, explicit flush,
overflow, oversized records, short writes and failures; ASan/UBSan passes. The
actual present path passes its 120-frame delayed-consumer test in both modes.
Native compilation and package verification pass, with only the runtime and its
digest file changed. The CE emulator enters Blood Gulch and fires a charged plasma
shot; its saved log contains 127 completed batched reports and no fatal marker.
These checks do not clear the previously reported combat crashes.

The updater confirms physical slot B running
`b64e2b984f0d328d5a0ec7f44a94a442f54aba7022e3e43e955b090c30ac22e9`.
The preserved slot A remains unchanged. The timing-only baseline was
`8785a7e301ad8ececc351b6fd2ba54f06c838dbe6caaf84d3af767a086985d27`.

## Other gameplay costs remain

The preceding user run used runtime `c761d36856c9b88b12253ef708a9bd7588a29413bb6f36f28aab1060accdd861`.
After the user returned to native resolution, 34 settled 60-frame windows ranged
from 7.2 to 15.5 FPS (median rounded window 12.0). Twenty-nine had no texture
decoding. Both object-worker lanes completed thousands of jobs. A slow window
recorded 41.19 ms/frame in object batches and 16.7 ms in draw HLE, with a nearby
22.95 ms exact-visibility wait report. These scopes overlap and cannot be added.
The log has no per-shot timestamps to attribute a specific AR hitch.

Continue investigating those object/visibility dependencies and the residual
report outlier. The separate [exact index-reuse prototype](index-reuse-20260915.md)
remains disabled until a physical performance comparison establishes a benefit.

Private evidence under `engine-restructure-20260914T2300Z`:
`user-ar-gameplay-20260915T223221Z`, `physical-cadence-timing`,
`emulator-profile-batch`, and `physical-profile-batch`. Logs, screenshots and
build artifacts remain outside Git.
