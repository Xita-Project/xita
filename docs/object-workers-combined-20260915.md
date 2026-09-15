# Combined object-worker comparison, September 15

The current experimental build completes six physical Blood Gulch off/on/off
trials across two views. Whole-object workers are approximately **5–6% slower**
than the same runtime with those workers disabled in these views. This closes a
gap in the earlier evidence: the lightweight-mutex comparison kept workers on
in every arm and therefore did not measure the net benefit of parallel updates.

## Device and comparison conditions

The physical Vita boot-confirmed runtime
`10ef0c0d2369eacdef8a16a0be0fcaaaa1e0a8efb6cdd031ac3508e213fcb09c`
in slot B, build stamp September 15, 12:12:07. No new executable was installed
for these tests. Native 960×544 rendering, original material/model/particle/glow
settings, texture maximum 256, triple buffering and an uncapped frame rate were
retained. The device reports CPU 444 MHz and GPU 222 MHz. The normal lightweight
mutex, private-output math and sleep/poll wait policy remain configured.

Each trial uses 60 settling and 120 measured frames per arm. The camera
consistency checks pass and each comparison restores the configured worker mode.
Worker allocations, native-helper infrastructure and diagnostics remain present
in the off arms. This is an in-runtime worker-policy comparison, not a comparison
against an ordinary build without the experiment. The simulation remains live.

The first view faces the base. For the second, only the camera was turned toward
the open field; it stayed stationary throughout each trial. Different views are
reported separately. Neither view represents driving or campaign combat.

| View / trial | Workers off, before | Workers on | Workers off, after |
| --- | ---: | ---: | ---: |
| Base 1 | 9.581 | 9.123 | 9.767 |
| Base 2 | 9.652 | 9.077 | 9.780 |
| Base 3 | 9.615 | 9.167 | 9.661 |
| Field 1 | 9.475 | 9.222 | 9.702 |
| Field 2 | 9.677 | 9.137 | 9.679 |
| Field 3 | 9.817 | 9.210 | 9.635 |

Pooling exact elapsed microseconds, with 720 off and 360 on frames per view:

| View | Workers off FPS | Workers on FPS | On versus off | Added time per frame |
| --- | ---: | ---: | ---: | ---: |
| Base | 9.676 | 9.122 | −5.72% | 6.273 ms |
| Field | 9.663 | 9.190 | −4.90% | 5.331 ms |

All six trial directions agree. This does not refute a gain in another workload,
nor negate the earlier measured improvement from replacing the kernel mutex with
the lightweight mutex. It does reject claiming that the current combined worker
mode has an established net gain in these two views.

## Stability and next implementation

Before the comparisons, one normal and one charged plasma shot completed through
remote input. Screenshots show energy dropping from 89 to 77 and the application
remains responsive. No worker STOP appears in the captured current run or the
comparison logs. This is additional coverage of that firing sequence, not proof
that all combat, rocket/death, attachment or GPU failures are resolved.

Both workers perform real callbacks, but shared math/transaction waits remain.
The [model ownership audit](model-update-ownership-20260915.md) identifies bounds
readers, parent-transform dependencies and collision paths that re-enter model
preparation. Continue with the previously proposed native hierarchy kernel and
explicit input/output ownership, rather than loosening protection around shared
object writes. Compare the serial extraction first, then determine the useful
overlap window for independent jobs. No runtime optimization was added in this
comparison session; the experimental build and its restored defaults remain on
the Vita.

## Evidence

Private capture directory: `physical-user-ready-20260915T183031Z` in the September
14 engine-restructure validation session. It contains the boot/status receipts,
current and three saved run logs, firing screenshots, and both complete comparison
receipts with exact-duration analysis. The two final cumulative comparison logs
have SHA-256:

- Base: `98d901113214847c009702a22b544c9d4d8b3deea35125da0085143956469956`.
- Field: `2ec5e339de06d3685a61a42ca1b34a5125f6643330c4ad068bd4dea24465dc94`.

The initial current-run capture is 5,766,422 bytes, SHA-256
`0e210fb1472ef668b4cbc1fb3e8e8ac5256566911f20ca9cc0bd1e4365ff4ff8`.
Most of this session is stationary automated testing; do not pool it as the
user's free-play average. Original logs, screenshots and binaries stay private.
