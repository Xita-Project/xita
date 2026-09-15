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
