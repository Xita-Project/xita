# Query-boundary startup selection

The existing exact query-boundary candidate can now start enabled after a full
process restart. Repository defaults remain OFF. This wiring changes startup
selection only; it does not change query planning, result publication, scene
order, notification storage, frame retirement or guest scheduling.

Build with the existing package flags plus:

```
XV_QUERY_BOUNDARY=1 XV_QUERY_BOUNDARY_DEFAULT=1
```

Use `XV_QUERY_BOUNDARY_DEFAULT=0` for a fresh-launch OFF control. The default must
be exactly `0` or `1`; a build without `XV_QUERY_BOUNDARY=1` omits the feature and
ignores the startup default. There is no environment or dashboard override for
this startup setting. `g_query_boundary_override` receives its value through a
static initializer before threads start. After dashboard handoff and pipeline
configuration, before the pump starts, one passive log records the effective
mode:

```
[query-boundary] process-start mode 1; exact prefix publication at existing scene ends; final ownership retained
```

The existing selector 39 retains its prior behavior: it snapshots the current
mode, applies its comparison states at drained boundaries, and restores that
snapshot on completion or cancellation. Thus it restores ON in a startup-ON
process. No comparison is necessary for fresh-launch gameplay validation.

`build/query-boundary-startup.config` tracks the effective startup mode. Only
`runtime/main.o` receives the default macro or depends on that stamp. Changing
the default in a compiled-feature build rebuilds main and the final linked
package, without rebuilding guest code or D3D replay. Changing the compile
feature still rebuilds both main and D3D through the existing feature stamp.
Changing the default while the feature is absent does not rebuild either.

Validation uses `tools/test_query_boundary_startup.py --output-dir DIR`: six
ASan/UBSan builds execute the extracted production initializer, getters, startup
log and mode-write branch with the actual comparison controller. They cover
implicit and explicit defaults, feature absence, normal completion,
cancellation, lost-view restoration and the existing early-visibility exclusion.
Ten incremental builds use the actual Makefile and real host compilation of
small fixtures to verify flags, rebuild scope, no-op builds and rejected invalid
defaults. Existing query and pump tests separately cover the unchanged
publication and final-retirement contracts.

The acquisition fixture previously failed to link after the collection selector
was added: its extracted production switch referenced
`xv_benchmark_compare_object_collect`, but the fixture supplied no stub. The
added stub returns false because its tested candidate range excludes collection;
both query-boundary ON and OFF acquisition runs then passed.

Evidence is private under `validation/engine-restructure-20260914T2300Z/`
`direct-cluster-query/query-boundary-startup/`. This change has not been run on
hardware by its implementer. Root must inspect actual ready/attached/fallback
counters, flare waits and frame times after fresh launch; startup ON alone does
not demonstrate boundary admission or a performance improvement.
