# Collision-query elapsed attribution

`XV_QUERY_REUSE_PROFILE=1` adds optional timing to the existing query-reuse
adapter. It defaults Off and requires `XV_QUERY_REUSE=1`. Only the adapter
object owns this compile option; a content stamp makes toggles rebuild it
without regenerating the game. This is diagnosis, not a performance claim.

Each admitted outer call takes two clocks: after acquiring the adapter's busy
ownership and before releasing it. The resulting `[query-reuse-cost]` rows
report calls, total elapsed microseconds, maximum call time and backward-clock
samples for original execution, capture and replay. Lookup and validation are
included. Original execution includes declined records; capture includes failed
attempts. Calls that decline ownership are not separately timed. A nested busy
fallback remains inside its outer call's interval, avoiding double counting.

These are elapsed times including scheduling and callbacks, not CPU self-time.
The paths handle different inputs, so their mean call times cannot establish a
matched speedup. Sum path totals over the same frame windows to estimate how
much of the frame this selected adapter accounts for. Do not add that total to
its enclosing object/owner scopes. Clock overhead also needs consideration.
Native FPSCR is saved and restored around clock calls, including the final
clock after publishing a replay's floating-point state.

Host and ASan/UBSan checks pass with profiling both Off and On. Tests use a
clock that deliberately changes FP state and cover original/capture/replay
attribution, lost admission, and nested fallback accounting. The compiled ARM
fixture preserves the full context, arena and native FP results against the
original query, including dependency rejection and retention across 192
unrelated queries. The seven allocated text/read-only sections of the Off
adapter match the preceding capacity build exactly. The fixture clock is a
stand-in; these checks do not measure firmware clock cost or real concurrency.
Private qualification evidence is in
`../collision-query-reuse-cost/qualification/`.

## Physical Vita: the selected query is a small part of the remaining frame

Perf.23 (`d7bdea5+`) was hash-verified and boot-confirmed in slot 1. Runtime
SHA256 is `25a97fd6833287becba06cb4a1e35b70d0d2360aa21f9491e2f70d5e3da5e765`.
Only `game-a.self` and `boot-game.txt` changed from perf.22. Previous cumulative
options and graphics settings remain active. A timed five-button launch
sequence reached the same New001 Normal Pillar of Autumn checkpoint, with no
screenshots between button presses. One final screenshot and the loaded/active
log establish the result, rather than treating navigation as proof of gameplay.

The last twelve settled windows contain 720 frames, at camera
`(-28.66, 32.52, 0.62)`, forward `(0.56, 0.82, -0.15)`:

| Adapter path | Calls | Elapsed ms/display frame | Maximum call |
| --- | ---: | ---: | ---: |
| Original, including lookup/declines | 7,067 | 2.741 | 0.947 ms |
| Capture, including bookkeeping | 34 | 0.133 | 8.601 ms |
| Replay, including lookup/validation | 1,509 | 1.018 | 0.698 ms |
| Total | 8,610 | 3.892 | — |

There are no backward-clock samples. Frame time is **78.083 ms / 12.81 FPS**,
with 151.17 draws/frame, essentially unchanged from perf.22's 78.075 ms. No
searched fatal/stop/GPU-fault/data-abort marker appears. This is stationary
ordinary gameplay, not a benchmark run or a heavy-combat acceptance test.

Even eliminating this entire measured adapter interval directly would recover
less than four of the approximately 28 ms/frame needed for 20 FPS here. Tick
catch-up, scheduling and dependency changes can affect that simple ceiling, but
these results do not support further blind cache capacity increases. Capture
can create an occasional long call, yet its average burden is only 0.133 ms.
Replay averages must not be compared against original averages as a matched
speedup: the cache selects different, more expensive queries.

The next priority is the larger object-update/scene-preparation path and a
same-checkpoint native/360p graphics comparison. The prior resolution evidence
was an outdoor view, not this campaign workload. Private captures, build inputs,
package receipt, summary and deployment log are in `../ce-perf23/`.
