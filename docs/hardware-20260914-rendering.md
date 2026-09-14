# September 14 hardware rendering comparisons

The [later two-view hardware tests](hardware-20260914-candidates.md) measure
early visibility completion, native point transforms and texture binding reuse.

Three matched trials in stationary solo Blood Gulch confirm that the existing
deferred flare queries help this scene. The new model-palette helper did not
improve it. Stable 20 FPS and the newer 30 FPS target remain unachieved.

## Conditions and results

Runtime `7787ebbcccc938dc56bbdb34ed0b6d35099042ec168e17a1d07f50c34f4401c1`
ran at 640 × 360, texture maximum 128, low model detail, triple buffering and
the vertex worker enabled. The cap was raised from 20 to 30 before measurement;
that is measurement headroom, not an optimization. Phase timing was off.
The requested 500 MHz clock fell back to **444 MHz CPU**, with bus/GPU/crossbar
at 222/222/166 MHz. Graphics settings otherwise stayed fixed.

Each test ran three off/on/off trials, with 60 settling and 120 measured frames
per arm. All camera checks passed. Pooled results use elapsed microseconds over
720 off and 360 on frames, not an arithmetic average of rounded FPS.

| Experiment | Off | On | Frame-time change when enabled |
| --- | ---: | ---: | ---: |
| Native model palette | 16.913 FPS / 59.125 ms | 16.826 FPS / 59.430 ms | 0.306 ms slower |
| Deferred flare queries | 13.834 FPS / 72.287 ms | 16.949 FPS / 59.002 ms | 13.285 ms faster |

The three flare trials saved 12.789, 12.992 and 14.074 ms/frame. **Deferred
queries were already enabled before this session**; this verifies an existing
benefit. Model-palette timing varied from 0.018 to 0.663 ms slower, so it remains
disabled. Its sampled work was about 65 matrices/frame, a small part of the
existing native matrix workload. These are one view's results, not campaign
averages, and live simulation continues during each comparison.

## Remaining waits

A nearby diagnostic window reports roughly 94 recorded draws/frame, about
5.2 ms of draw preparation, and 42.7 ms between starting packet submission and
observing final GPU completion. The last number includes CPU submission and
scheduling; it is not a direct GPU execution timer and overlaps guest work.
Deferred flare queries still waited about 11.2 ms/frame in that window.
Do not add these overlapping numbers together.

The next experiment publishes visibility values after their world fragment
scene completes, before the final upscale. It retains every frame slot until
the final fragment fence. It preserves draw order, shader programs and exact
query values, including the existing eager/deferred flare policy. The default
remains off, with a separate `early-visibility` remote comparison.

Host tests exercise the production pump and scene-ending code with delayed
world/final/display completion, stale notification words, ticket wrap, missing
world notifications, native resolution, empty query frames and submission
failure. Query publication must never release frame storage. Acquisition,
benchmark restoration, render-target lifecycle and HTTP tests also pass.
The ARM build completes. In isolated Vita3K, the dashboard and normal solo
Blood Gulch menus render, and a full off/on/off comparison completes with a
passing camera check and restoration. The world-fence logs occur in the enabled
arm, before final retirement. The emulator retained its 20 FPS cap and phase
instrumentation, so its approximately 19.95 FPS in every arm validates the
experiment's operation, not its hardware benefit. Physical performance remains
unmeasured.

The experimental runtime is
`f35ddc77b165174a90f2b435d2784a92e6bd48b731ecc84383144b2632297266`;
its VPK is `da74890427de558a4100340d4bd93b799293cef562c3e1eae55bebec67d98a7d`.
Only the runtime and boot record differ from the confirmed baseline package.

The [VitaSDK API](https://docs.vitasdk.org/group__SceGxmUser.html) exposes separate
vertex/fragment notifications for scene completion. This experiment uses only
fragment completion to read visibility results. The final frame fence and
exceptional error drains are retained.

## Evidence

Private artifacts are under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z`:
`physical-palette`, `physical-flare`, their full logs and exact-time analyses,
and the diagnostic/candidate package receipts. No game assets, device logs,
generated guest source or pairing credentials are committed.

A separate 48-scope diagnostic renders Blood Gulch in Vita3K and produces
complete accounting windows without dropped/invalid scopes. A physical boot
was confirmed, but remote input interception and the following unconfirmed
restart interrupted collection of its gameplay trace. Emulator timings are
not substituted for the missing physical child-cost measurements. The update
sequence is documented in [the updater report](hardware-20260914-updater.md).
