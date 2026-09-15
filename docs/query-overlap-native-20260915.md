# Query overlap at native resolution, September 15

A fresh first-person cryobay capture at 960×544 exposed approximately 40 ms/frame
waiting for exact flare visibility results. The earlier 360p campaign comparison
had negligible waits and could not predict this result. The quaternion ownership
experiment shortened one mutex wait but moved contention elsewhere; it did not
materially shorten complete object batches.

The existing [exact query-overlap implementation](flare-query-overlap-20260914.md)
retains the previous query generation while preparing the next query list.
Brightness reads, identity changes and Present still consume the correct results.
This changes scheduling without lowering resolution, using stale visibility or
altering the shader/texture settings.

## Measurements

The three hardware comparisons retain native resolution, standard graphics,
both object workers, lightweight mutexes and private-output math. The quaternion
bypass and hierarchy batch remain off. Each arm settles 60 frames and measures
120, with live simulation and identical status polling. Exact elapsed totals and the follow-up package receipt are retained in private
validation storage.

The initial trial measured 4.737 / 5.795 / 4.703 FPS off/on/off. The first repeat
measured 4.645 / 5.755 / 4.677; the second repeat measured
4.716 / 5.738 / 4.672. Pooling 720 off and 360 on frames gives **4.691 off
versus 5.762 on**, a **22.83% increase** and **39.62 ms/frame saved**. Individual
trial savings were 39.32, 40.80 and 38.75 ms. All camera comparisons passed.
Counter windows across all three comparisons show exact
flare waits falling from 40.53 ms/frame to zero. In the initial trial, complete object-batch time
remained 18.87 versus 18.77 ms/frame, consistent with removing a query dependency
rather than accelerating the object work itself.

## Default and scope

The follow-up enables query overlap by default only in builds that explicitly
include `XV_FLARE_QUERY_OVERLAP=1`. An explicit runtime setting of zero retains
the earlier behavior. Ordinary builds without retained query history are
unchanged. The successful scheduling change is retained alongside existing
multicore work; the ineffective quaternion bypass remains available but off.

The default-selection and restoration tests cover absent, zero and one settings,
including query reuse, late completion, missing history, all dependency barriers
and concurrent history publication under ASan/UBSan. Emulator validation checks
startup, actual retained generations, off/on/off restoration to the new default,
and plasma firing. Physical installation and gameplay validation also completed.
No stable 20 or 30 FPS claim follows from this one heavy campaign view.

The final candidate is runtime
`8f7113bca776aa0b1547cd20ccb41a115d66e08d2b1fac6bd5d50d94c7769843`.
Its package changes only the runtime and boot marker under the existing updater
contract. The emulator enters ordinary solo Blood Gulch, completes the remote
comparison, and resumes retained-generation batches afterward without an explicit
configuration override. A charged plasma shot consumes energy from 100 to 89;
query-history counters remain active and no fatal stop is recorded. This is an
integration check, not evidence of emulator FPS translating to hardware FPS.

The physical updater verified the final runtime in slot B and retained the
known A-slot executable for rollback. No VPK reinstall was needed. Graphics
configuration files were unchanged; omission of the query override now selects
the measured scheduling path. In measured campaign windows, reported average
C2 activity rose from about 66% to 80%, consistent with removing a wait. These
utilization samples are descriptive and do not substitute for frame timing.

On the final installed build, ordinary solo Blood Gulch launched through the
split-screen menus. A charged plasma shot reduced energy from 100 to 89,
both camera-turn directions changed the view, and forward movement changed
the camera position. Retained-generation batches continued during normal play
(58–59 batches per 60-frame sample), while private quaternion checks stayed
at zero. No crash occurred during this short check. Its roughly 10–17 FPS
samples span different views and input activity; they are not a controlled
performance comparison. Rocket explosions and vehicle driving remain outside
this check, so the earlier combat and vehicle crash reports are not closed.

The remaining frame time is still substantial: after overlap this view averages
173.54 ms/frame, while object batches account for about 19 ms. Next profiling
and restructuring should prioritize the remaining rendering preparation and
submission dependencies, alongside moving-camera, combat and vehicle stability.
The quaternion bypass should not be enabled simply to increase core activity.
