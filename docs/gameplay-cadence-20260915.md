# Gameplay hitches and periodic reports — September 15

The user reports better overall responsiveness, with drops during camera turns,
assault-rifle fire, and a recurring hitch every few seconds. The retrieved runtime
hash is `c761d36856c9b88b12253ef708a9bd7588a29413bb6f36f28aab1060accdd861`.
The captured log contains no fatal-stop marker. This does not clear prior crashes.

The user tested several resolutions and returned to native 960×544. After
excluding the first report following that final change, 34 native-resolution
60-frame windows range from 7.2 to 15.5 FPS, with a median window value of 12.0.
These rounded windows are not an overall gameplay average or a controlled
before/after comparison. Actual logged clocks are CPU 444 and GPU 222 MHz,
despite a request for CPU 500 MHz.

Twenty-nine of those windows report no new texture decoding. In the 7.2 FPS
window, object batches consume 41.19 ms/frame and draw-HLE time is 16.7 ms/frame;
there is also a nearby exact-visibility wait report of 22.95 ms/frame. These
categories have different scopes and may overlap. Both object-worker lanes
complete thousands of jobs in the window. The log lacks per-shot timestamps,
so it cannot attribute a specific AR shot or individual hitch to these totals.

## Cadence hypothesis

`xd3d_r_present_inner` emits a large set of profiling reports every 60 frames. The log
sink writes each message synchronously to the Vita file. At the observed rates,
that cycle occurs every roughly 4–8 seconds, which is consistent with the user's
reported cadence. Consistency is not proof that logging causes the hitch.

The next build measures wall time around that report block and emits
`[profile-cost] frame ... report-us ...`. It includes counter reporting,
formatting and synchronous log writes, but excludes the timing record itself.
Only two clock reads are added per report. The existing frame accounting places
this reporting time in the following game interval because it occurs after the
last-present timestamp is updated. No logging or crash records are disabled.

If this cost explains the recurring stall, test combining the report's small
file writes while preserving complete records and immediate ordinary logging.
Otherwise, continue with the measured object/visibility dependencies. The
separate [index-reuse prototype](index-reuse-20260915.md) addresses repeated
draw preparation and is not presented as a proven fix for the periodic hitch.

Private evidence: `engine-restructure-20260914T2300Z/user-ar-gameplay-20260915T223221Z`
contains the original log, status/hash, screenshot and parsed window data.

## Physical timing and the batching candidate

Runtime `8785a7e301ad8ececc351b6fd2ba54f06c838dbe6caaf84d3af767a086985d27`
was installed through the updater after preserving all four physical logs. Index
reuse remains disabled. The first menu report took 181,970 us. In a later
stationary native-resolution Blood Gulch snapshot, seven consecutive reports
measured 389,607 / 347,484 / 383,057 / 682,846 / 335,726 / 377,710 / 410,982 us
(median 383.057 ms). An earlier gameplay report reached 1,114,356 us.
These are complete report wall times, not individual disk syscall measurements.
They establish a substantial periodic stall on hardware without proving that
all camera-turn and firing hitches share its cause.

The next candidate groups only this report's calling-thread log writes in a
fixed 32 KiB buffer. End-of-report flushes synchronously before gameplay resumes;
there is no background queue or new worker. Other threads and calls outside the
scope retain immediate logging. Overflow spills complete chunks, large messages
bypass the buffer after its pending bytes, and explicit owner flush writes its
pending report before syncing. The option `XV_PROFILE_BATCH=0` restores separate
writes; batching is enabled by default in this candidate.

Host tests cover report grouping, foreign-thread calls, nested scopes, explicit
flush, overflow, oversized records, short writes and write failure. ASan/UBSan
passes. The actual present path also passes its 120-frame delayed-consumer test
with batching enabled and disabled. Native compilation and package verification
pass; only the runtime and its digest file change. The owned CE emulator boots
runtime `b64e2b984f0d328d5a0ec7f44a94a442f54aba7022e3e43e955b090c30ac22e9`,
enters Blood Gulch and fires a charged plasma shot. The saved emulator log
contains 127 completed batched reports and no fatal-stop marker. This is limited
smoke coverage, not proof of physical FPS or resolution of prior combat crashes.
Physical batched timings remain pending.
