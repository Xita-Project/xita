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
and plasma firing. Physical installation and gameplay validation follow those
checks. No stable 20 or 30 FPS claim follows from this one heavy campaign view.
