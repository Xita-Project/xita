# Measuring repeated world collision searches

The existing native collision helpers reduce individual search operations, but
recent campaign builds remain near 12.8 FPS. A different potential optimization
is to avoid repeating a complete world sphere query when its meaningful inputs
and world geometry are unchanged. This is not implemented yet.

The follow-up [reuse contract and memory recorder](query-reuse-contract-20260919.md)
documents the implementation and ARM checks completed after this census. It
remains separate from the live query path; no hardware query is skipped yet.

The selected `172C95 -> 171F94 -> 88110` boundary returns AL and four ordered
lists of IDs. Static packet construction and dynamic-object collection follow
that query and must still run. The query consumes collision geometry, a sphere
center/radius and a mutable breakable-surface filter. Its ECX low word is the
signed filter limit, not an output capacity; the four result caps are fixed256.
Thus matching just position and radius does not qualify a cached answer.

`XV_QUERY_REPEAT_CENSUS=1` adds a diagnostic at that actual world wrapper.
It defaults Off, requires the existing world-run admission, and never substitutes
results, changes a guard, changes the query budget or adds per-node clocks.
A single caller-owned ring retains the last64 valid observations, including
repeated keys. Hashes only select candidates; equality checks actual key fields.

The input key includes native mapping roots, geometry and filter pointers,
center pointer and raw coordinates, expanded radius, filter limit, guest FCW,
native FP controls, the zero/axis-selector constants and0x60bytes of geometry
metadata. The stricter tier also compares32owned filter bytes. Reads accept
unaligned/page-crossing data, reject wrapping/out-of-arena/trash spans and never
invoke guest fault handling. Missing filter bytes leave only the input tier;
invalid base inputs have a separate count. Diagnostic watch/trace modes decline.

Observation requires the exact active native worker/context with its actor guard
held. The existing actor guard serializes writers. `xv_object_jobs_finish`
advances the epoch only after joining and releasing ownership; the epoch is an
**object pass**, not a rendered frame. A current-pass hit takes precedence over
a prior-pass-only hit independently for each tier. Reports run after workers
join and reset counters without discarding history. Mapping-root changes clear
history. No guest pointer is retained for dereference.

Matches are only opportunities for further investigation. They do not prove
unchanged pointed geometry, page contents, aliases, replay FP effects or callback
and scheduling-budget behavior. The64-observation bound can miss older repeats;
reported evictions are populated ring-slot overwrites, not distinct-key cache
pressure. This probe cannot predict FPS savings.

Host module tests cover exact hash collisions, variants of a changed filter,
ring eviction, missing filters, epoch changes/wrap, invalidation and independent
report reset. Actual-adapter tests cover owner declines, unaligned/cross-page
copies, trash and overflow rejection, context/guest-memory preservation and
host FP-state preservation. Normal and ASan/UBSan runs pass. Generation tests
verify retained default source identity, exactly one additional world-wrapper
call, no-op generation and exact OFF restoration. Native build/hardware results
will be recorded after completion. No result reuse is enabled in this change.


## Hardware observation: repeated work is present

`0.2.0-probe.2 / ba6bac2+` was built, hash-verified, installed in slot0 and
boot-confirmed. Runtime SHA256:
`a907fbef0a47ed1652b92b723102eca14cfc695ff559a7d777b40851e882c34f`.
The package retains1744entries; only `game-a.self` and `boot-game.txt` differ
from perf.19. The owning object/query OFF ARM text matches perf.19 exactly;
ON compilation/linking, no-op rebuild and malformed-option checks pass. The
adapter has no dependency on the optional checked-address trash symbol: the
allocation's trailing trash page is derived from the arena-size contract.

The same Normal Pillar of Autumn save loaded on physical Vita at native544-line
rendering, unchanged settings and camera (-28.66,32.52,0.62; forward
0.56,0.82,-0.15). This was ordinary stationary gameplay with active NPCs, not
a benchmark or emulator. The initial sample found52.66%matching inputs; the
settled last twelve60-frame windows give:

| Counter | Settled total |
| --- | ---: |
| World queries / valid inputs | 8,622 / 8,622 |
| Valid32-byte filters / unavailable | 8,622 / 0 |
| Same-pass repeats | 0 |
| Prior-pass-only repeats, both tiers | 4,699 (54.50%) |
| Misses, both tiers | 3,923 |
| Completed object passes | 1,437 |
| Populated history-slot overwrites | 8,622 |
| Root invalidations during settled sample | 0 |

The stricter filter tier equals the input tier in this sample. Approximately
6.53queries/frame match previously observed inputs, but that is not a measured
saving or an approved replay count. All matches cross a completed object-pass
boundary; clearing a cache after each pass would forfeit this observed coverage.
The64-observation history is finite and is not a prediction for a differently
sized or keyed cache.

Median FPS remained12.8 with151draws/frame (77.2ms game,0.9ms wait, independent
component medians). The settled1,774,123-byte log contains no searched STOP,
FATAL, GPU-crash or trap markers. This short input-only observation does not
qualify a collision replacement or extended combat stability. Raw logs,
screenshots and reconciled counter summaries are in `../ce-probe2/gameplay/`.

This evidence promotes complete world-query result reuse to a concrete research
candidate. Before enabling it, prove geometry contents and mappings remain
valid, exclude output/scratch aliases, preserve the four ordered lists and live
caller state, preserve callback/backedge budget behavior, and qualify FP exit
effects. Original packet generation and dynamic-object collection must rerun.
Copying an old full CPU context is not a valid substitute for those boundaries.
The project option remains Off and the diagnostic is removed from active play
by restoring perf.19 after this capture.
