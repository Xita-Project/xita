# Measuring repeated world collision searches

The existing native collision helpers reduce individual search operations, but
recent campaign builds remain near 12.8 FPS. A different potential optimization
is to avoid repeating a complete world sphere query when its meaningful inputs
and world geometry are unchanged. This is not implemented yet.

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
