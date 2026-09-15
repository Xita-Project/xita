# Bounded campaign timing on hardware — September 14

The new remote capture identifies the scene-rendering path as the largest
selected elapsed-time region in this cryo-room view. This includes translated
preparation, native calls and any blocking inside them; it is not a GPU timer
or a measurement of pure CPU arithmetic.

Runtime `2ecbca0853495a5634c8789b97097b81392361125a3354ed253ffb3cb69a1a4a`
is boot-confirmed in physical update slot B. The [combined-candidate trials](optimization-bundle-20260914.md)
finish and restore their defaults before the separate `guest-phases` capture.
Resolution remains 640 × 360, with the established indexed-vertex, upload-worker,
native object-basis and flare settings. Requested CPU 500 MHz reads back as
444 MHz; GPU/bus are 222 MHz.

## Validation and overhead

Vita3K and the physical Vita each record exactly three complete 60-frame
windows in the middle arm, with zero dropped or invalid scopes. The camera
check passes and the final off arm emits no timing windows. Runtime settings
are restored. The earlier emulator configuration had continuous profiling
enabled; the remote capture correctly rejected that mode before it was changed
to off for validation.

The physical off arms pool to **6.692 FPS**, compared with **6.479 FPS** during
the timed arm: approximately **4.93 ms/frame of added diagnostic time** in this
comparison. Reporting alone accounts for about 0.258 ms/frame. These are
profiling costs, not optimization gains. The trace is disabled during ordinary
play and the combined-candidate performance comparisons.

## Selected regions

These averages cover 180 instrumented frames. Inclusive regions contain their
children; do not add the rows. Selected self time excludes only other selected
children, so it still includes unselected callees and native waits.

| Region | Inclusive active ms/frame | Selected self ms/frame |
| --- | ---: | ---: |
| Scene `5D410` | 109.51 | 48.90 |
| Tick driver `FA920` | 34.61 | 12.31 |
| Scene callbacks `54010` | 36.98 | 7.35 |
| Render `5B710` | 13.77 | 10.00 |
| Render `5B760` | 11.20 | 10.88 |
| Callback `628F0` | 10.44 | 10.44 |
| Callback `62870` | 9.29 | 9.29 |
| Pose `8DDF0` | 6.15 | 4.13 |
| Matrix helper `B5B40` | 2.10 | 2.10 |
| Quaternion helper `B5F60` | 0.90 | 0.90 |

The hot callback labels include their unselected bodies: `628F0` invokes
`77FE0`, while `62870` tail-dispatches into `77480`. Their wrappers alone
are not the measured expense. The scene's 48.90 ms selected-self remainder
likewise contains substantial unselected work.

Long-lived parents already on the stack when capture starts are absent. These
regions therefore do not form a complete frame accounting. Nearby draw reports
show roughly 430 draws/frame and about 34–35 ms/frame in draw HLE; that work
overlaps the scene timings. This view differs from the earlier bridge capture
and cannot quantify the causal cost of extra visible NPCs.

## Finer scene capture

The complementary 48-scope build, runtime
`5a024a145c87ed5f3e59f5c1bbc61ada694960c0b8e8e487c4784c5b831d7e00`,
is now boot-confirmed in physical slot B. The earlier working runtime remains in
slot A. Its ordinary translated instruction bodies match the preceding build
when timing prologues are removed.

Vita3K and physical campaign captures each complete three valid 60-frame
windows, with no dropped/invalid scopes. The physical comparison reports
6.768/6.712/6.660 FPS for off/on/off; this is a diagnostic, not an optimization.
Selected inclusive times include scene callbacks `54010` at 35.90 ms/frame,
object updates `8FB70` at 17.26, flare-query preparation `60560` at 10.29,
scene setup `539C0` at 9.86, light work `92890` at 7.27 and `5E270` at 6.82.
Rows overlap and contain native blocking or unselected descendants.

The owner subsequently requested a more aggressive multicore approach, accepting
broken gameplay/rendering during development. The next implementation is the
[concurrent-object experiment](parallel-object-experiment-20260914.md), with whole
object callbacks, private stacks and joined batches. Shared game-state ordering
is intentionally unproven; no hardware speedup is claimed for the prototype.

Private logs, camera captures, parsed windows and exact elapsed-time receipts
are under `engine-restructure-20260914T2300Z/physical-guest-phases`, alongside
the corresponding emulator validation and combined-candidate trials.
