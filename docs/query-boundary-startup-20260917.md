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

## Physical follow-up

The four-change package was built and privately pushed from `47250b6`, then
installed through the remote updater. Its confirmed runtime hash is
`4d84eb024c64fa64c01af071765487744793ed0a2ff4e50bd255df0fdd0e8fe1`.
Collision vertices, object collection, segment/sphere and query-boundary startup
logs all report mode 1. The earlier three-helper build is retained in slot 0.
Only the game executable and boot marker differ in the package; launcher and
assets are unchanged. No VPK reinstall or Vita3K run was used.

Ordinary Blood Gulch gameplay confirms actual admission: steady reports show
60/60 existing scene-end attachments and 60/60 query notifications observed before
the final notification, with zero failed attachments or final fallbacks. Maximum
pending packets is now 2. Query latency is about 73 ms, with another 57–61 ms to
final completion, and frame periods near 79 ms. These are overlapping scheduled
observations, not GPU active-time measurements; the final latency includes queue
residence. Roughly 11–12 ms of flare waits remains.

An independent displayed-frame observation gave 12.71 FPS over 60 seconds. The
three-helper-only build's earlier observation was 12.06 FPS, but the new match
spawned on a different side of the base (139 versus 160 draws per frame). Those
numbers do **not** establish a performance improvement. They show that the early
publication path runs and allows overlap while a substantial limit remains.
A charged plasma shot completed (energy 100 to 89), and leaving the match returned
to the menu. This short smoke does not resolve the older vehicle/rocket crashes.

The following campaign checkpoint observation averaged **12.728 FPS** over
60 seconds, versus **11.495 FPS** in the earlier vertex-plus-collection launch.
The view/checkpoint and native settings match, but live AI varies, this process
visited Blood Gulch first, and both segment/sphere and query publication were
added. Treat the roughly 1.2 FPS difference as promising combined evidence that
needs repetition, not an attributed or confirmed gain. Steady campaign windows
admit all boundaries, observe them before final completion, and retain two
pending packets. Recent flare waits are approximately 4.5–6.9 ms/frame. No
`[STOP]` or `[FATAL]` markers appear in the captured session. Vehicle driving,
rocket explosions and longer gameplay stability remain unverified.

Evidence is private under `validation/engine-restructure-20260914T2300Z/`
`direct-cluster-query/query-boundary-startup/`. Broader campaign movement, driving and
repeated comparable-view checks remain required. No stable 20 FPS result is claimed.
