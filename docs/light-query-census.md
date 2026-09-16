# Count original per-object light queries

This OFF-by-default observer quantifies the live `8D760` light-group boundary proposed by the private spatial query/replay proof. It does not gather query values early, replace an instruction, change guest memory/context, dispatch queries, create a worker, or change the existing object-worker allowlist/lock policy. There are no hot-path clocks or log calls.

The result is a **structural candidate census**, not a parallel-execution admission decision or FPS result. Full geometry pinning, aliases, preparation dependencies, exact FP/layout replay, bounded worker storage and cancellation still need production integration. A successful counter gate is insufficient permission to publish a private query result.

## Integration and controls

Build explicitly with `XV_LIGHT_QUERY_CENSUS=1 XV_EXPERIMENTAL_OBJECT_JOBS=1`. The Makefile tracks both flag transitions for generated/runtime objects. Ordinary builds compile out the observer. There is no environment enable, graphics setting or default change. Reserved benchmark37 (`light-census`) is an explicit count-only OFF/ON/OFF diagnostic.

`recomp/kernel/xk_light_census.h` exposes:

```c
int xv_light_census_control(xctx *current, int enabled, int reset);
int xv_light_census_take(xctx *current, xv_light_census_stats *out, int reset);
const char *xv_light_census_reason_name(unsigned reason);
```

Both control functions return zero without changing counters/configuration when the backend is uninitialized, the caller is not the registered native owner/current live guest fiber, an object pass/queue/service is outstanding, a currently tracked math scope is open, or a group is open. They never initialize, dispatch or join work themselves. Initialize the existing object backend through its existing guest-owner path before enabling the census; the existing zero-worker diagnostic mode is supported.

The controller must toggle only at an audited **drained frame boundary with no open generated/math scopes**. This is an explicit integration precondition. To keep runtime OFF cheap, pre-enable scopes are not tracked and cannot be reconstructed by `control`. While ON, enclosing logical math scopes, including idle mutex bypasses, reject observation and control. Do not enable from a network/native service thread or from an arbitrary guest helper. Reset/take/report after a successful owner-side boundary check; format or queue the copied host result outside hot query paths. Benchmark37 supplies this boundary through the original Present/Swap HLE context; see the protocol below.

A runtime-OFF hook performs a flag load/branch. Runtime-OFF existing math scopes also retain flag/token bookkeeping and a cleanup branch, but perform no observer native-thread-ID check, counter update or cleanup call. ARM `scope.s` records this cost. Compiling the feature out removes every observer reference. The lightweight mutex and its tokens, private-release policy, recursive worker depth and owner-service protocol are unchanged.

## Exact hook and ownership boundary

Production hooks reside in `games/halo_ce_3925/hooks.py`, guarded by the owned executable identity and local instruction signatures. The owned generation test strips only census `#ifdef` blocks and requires **byte-identical original emitted C** afterward.

| Hook | Point and effect |
| --- | --- |
| `8D760` function entry | Open a serial observation token before any original instruction. EAX is the full object ID; stack+4/+8 are remove/update low-byte flags. |
| independent `8D7A6` function entry | Count a suffix decline. Falling through its instruction in the normal `8D760` body does not invoke this hook. |
| `925AB -> 56670` | After original return-PC push, before callee/guard entry. Count actual query calls and original positive-radius/valid-start traversal conditions. |
| `8D7FA -> 565E0` | After original return-PC push, before callee/guard entry. Count actual removals. |
| all emitted copies of `8D837 ret8` | Close the matching token before the original return instruction, after verifying entry ESP/return-PC. |
| `xv_preempt`, `xk_yield`, thread exit, trap/unimplemented/worker STOP | Cancel before a possible handoff/exit; resumed query calls remain orphaned rather than attaching to a later group. |
| `58CD0`, `58440`, `91D10`, `92230` entry | Cancel on BSP switch, map end, shared-list reset or light deletion. Root-pointer equality alone never pins geometry. |

Before reading even the guest return PC, `xv_object_census_admit` verifies initialized backend; actual native worker versus exact registered owner thread; exact current `&xk_cur->ctx`; unmarked context; exact current native fiber equals `xk_cur->fiber` and guest state is ready; empty queue/not running/no parked-service request or audio context; no tracked enclosing owner math scope. Known retained pool contexts are classified as marked on the owner before the live-context comparison. A foreign thread returns before inspecting `xk_cur`, `c` or owner-local state. Unsupported callers touch only 32-bit atomic admission/decline counters.

Only owner-admitted groups resolve the bounded metadata: object table `2FC6AC` stride12/full-generation ID, attachment-enabled flag, tag table `39CE24` and tag+140 count, at most8 status bytes/light IDs, light table `2FC67C` stride124/full-generation IDs, BSP roots and at most256 visited stamps. Dynamic reads validate page bounds and wrapping spans. The observer keeps only the nearest future epoch-stamp distance; it copies no visited array. Query ordering permits skipped attachments, because valid lights can legitimately skip `56670`; repeated or reversed IDs decline. Radius classification uses IEEE bits and changes no native FP exceptions. Zero/negative/NaN radii and cluster `FFFF` are shortcuts; invalid other clusters decline the candidate.

The observer checks the actual epoch sequence, identity/status changes and observed roots, then excludes wrap/future-stamp collisions using **actual traversals**, not attachment count. These checks remain bounded metadata checks. They deliberately do not walk/pin all geometry, validate every physical alias, copy full query inputs, or run the expensive preparation adapter.

## Counter meaning

`admission[0][reason]` counts original group/suffix entry hooks; `[1]` counts query hooks; `[2]` counts removal hooks. Reason0 is admitted. Rejected worker **entries** and worker **query calls** must be read separately: the latter may also arise outside `8D760`. No guest fields are read to associate rejected worker queries with a group or classify their radius.

`entries/opened/completed/cancelled` describe owner groups. `queries/traversals/removals` include observed work inside open owner groups, including subsequently cancelled/declined groups. Query/removal admission totals also include calls outside any open owner group; those increment `orphan_queries/orphan_removals` without reading query values. Actual query/traversal histograms describe completed groups. `candidate_multi_groups` and `candidate_multi_traversals` retain only completed structural candidates with at least two traversals. Histograms use bins0..8 plus9 for overflow. Mode and bounded return-PC histograms identify entry sources.

`declined[]` combines admission declines with the **first structural failure** of each observed group; it is not a group-count denominator. Use the per-hook admission arrays to interpret worker/context/queue/guard traffic. Cancellation counts distinguish incomplete groups. A previously declined group that later cancels retains its first structural reason.

`guest_reads/bytes`, `entry_reads/bytes`, maximum entry/whole-group bytes and reported fixed-storage sizes quantify instrumentation work without timing it. Runtime allocation/page-copy/journal counts are zero. Full guest-memory snapshots appear only in the private original-oracle fixture, not in this runtime module.

## Validation and scope

Private fixture worktree: `validation/engine-restructure-20260914T2300Z/agent-worker-dependencies/light-census-prototype`, scripts `run_census.sh` and `validate_census_build.py`. It freshly lifts the owned Xbox executable; it does not substitute MCC data/layouts. Generated owned code and build products remain private and ignored.

The final suite contains584 complete original `8D760` comparisons and30 decline/cancellation cases. Every byte of the fixture's8MiB guest/image arena, every byte of `xctx`, ordered query records, removals, scheduler callbacks and native FP exception flags is compared. Cases include0/1/2/4/8 and overflow9 lights; overlapping/warm lists; empty/exhausted/full allocation outputs; all four native rounding modes; skipped attachments, invalid starts, zero/NaN radii;256 clusters; epoch wrap/future-stamp collisions; between-query status/ID/epoch mutations; suffix/nesting, handoff/STOP and all four lifetime hooks. Admission tests prove zero guest reads for wrong native thread, copied/stale/blocked/current-marked contexts, active queue and enclosing math scope.

The actual production object backend is linked in the fixture. Zero, one and two existing workers test native-thread rejection, ordinary and quiescent owner-service rendezvous with a retained marked context, forged owner-context rejection while jobs run, and a held guard during both services. The prototype creates no new query workers. Host ASan/UBSan and TSan runs validate the same suite; ARM validation compiles the observer, pool, scheduler, x86 runtime, native math and hooked owned closure with the feature OFF and ON. No final Vita game link, device/emulator execution or FPS improvement is claimed.

Host fixed storage is192 bytes of group observation plus1,216 bytes of counters, with tiny owner-control bookkeeping. The256-cluster/eight-light test reads at most1,212 bytes at entry and1,526 bytes across the full observed group. These are constructed correctness bounds, not a live workload measurement. The controller boundary precondition, omitted full geometry/alias/FP validation and the256-cluster/eight-light cap remain explicit limits.

## Use with frame-drop evidence

The current physical stationary sample reported by root is about10.2FPS, game95.9ms/wait1.9ms, object batches35.9ms/frame across2.93 passes/frame; lane lock waits14–17ms overlap. Those numbers neither identify how much query work lies inside an eligible `8D760` group nor make lock waits additive frame cost. The earlier pose experiment reduced acquisitions roughly59% without a material FPS gain, so fewer lock operations alone is already a weak target.

Collect these counters in the same owner-side frame windows as existing game/frame-time, object-batch, lock and logger-cadence evidence, with camera, worker configuration and report policy held constant. Compare census OFF/ON/OFF to bound instrumentation cost. Then examine whether long-frame windows coincide with more group queries/traversals, a changed source distribution, more worker/ambient-guard declines, or unchanged query counts. A drop with stable counts needs other evidence (query size, scheduling, streaming, reporting or GPU interaction); this count-only probe cannot name its cause.

High owner candidate multi-traversal counts justify the next pinned-geometry/bounded-arena adapter gate. High worker/guard declines mean work exists under the current worker/transaction boundary; an owner-only gather will miss it and the boundary needs restructuring. High owner orphan-query counts suggest a wider light-update pass is the more relevant boundary. Low eligible counts do not prove low total query work. Only a later controlled frame-time comparison can establish a performance benefit.

## Benchmark37 measurement protocol

`tools/vita_remote.py benchmark --kind light-census` requests reserved selector37. The network publishes only the request word. Compile-OFF builds reject it without linking observer APIs; the alias does not enable the feature. The existing owner consumes the request and arms a Present capability only for this diagnostic. The exact `xctx *` passed to the original Present or Swap HLE is scoped around `xd3d_r_present`; the renderer never invents a context from an ambient pointer. The capability checks registered native owner, currently executing live context/fiber, no marked context, drained object pass/queue/services, no tracked logical math scope and no open light group. It rechecks before the camera probe and before each reset/take/control. Nested Present invalidates the capability. Handoff/STOP invalidates it even during an OFF arm, and stale cleanup cannot clear a newer capability.

The existing audited Present boundary is the no-open-generated/math-scope precondition for first enable; runtime-OFF scopes remain intentionally untracked. GPU draining does not join or otherwise make an inadmissible object context eligible. Rejected initial admission changes no mode. Losing admission after activation retains a busy failed benchmark and retries restoration only at a later admitted owner Present. No foreign caller may mutate the state machine.

Each arm holds the original resolution, camera, graphics settings, workers, math policy and report policy, with60 settling frames then120 measured frames. The controller resets counters after settling and after the phase marker/GPU drain, then refreshes its start timestamp. It takes the end timestamp before drain/copy/format/report. Setup and all18 copied-counter rows per arm are outside the measured elapsed time. It does not force standard graphics; select the normal standard configuration before requesting the trial. No query gathering, replacement or new worker is enabled.

Successful completion, cancellation or view loss restores the original census **enabled state**. Measurement counters are deliberately reset telemetry; pretrial counter contents are not restored. Failed restoration keeps status/request busy, records a boundary failure and retries after250ms at an eligible Present. The controller never claims a successful restoration after a failed control call. The client rejects any fresh boundary failure, missing/duplicate/malformed counters, inconsistent OFF/ON/OFF modes, impossible group accounting or moved-camera result. Historical failures are outside the trial slice.

The schema is `[light-census-count] phase N KEY v0/v1/...`. Required rows are:

| Key | Ordered values |
| --- | --- |
| window |120 measured frames, observer0/1/0 |
| groups | entries, opened, completed, cancelled, candidates |
| work | queries, traversals, removals, multi groups, multi traversals, candidate multi groups, candidate multi traversals |
| orphans | queries, removals |
| reads | total reads, total bytes, entry reads, entry bytes, maximum entry bytes, maximum group bytes |
| storage | group bytes, counter bytes |
| budget | nonpositive entry budget count |
| tags / queries / traversals / candidate-traversals | bins0..8 and overflow9 |
| mode | remove/update low-byte modes0..3 |
| entry-admission / query-admission / removal-admission | OK, uninitialized, actual worker, wrong native owner, wrong live context, marked context, queue/service, ambient logical guard |
| declines | reason enum0..27 in `xk_light_census.h` |
| source-pcs / source-groups |16 bounded guest return-PC slots plus overflow slot |

The separately callable8D7A6 suffix increments OK entry admission and the suffix decline, but is not an8D760 `entries` count. Worker entry/query declines do not include guest reads, input gathering or inferred group membership. OFF rows are zero except window/storage metadata. Reports use one static host counter copy (sizeof `xv_light_census_stats`; observer total accounting remains1,216 bytes), a bounded formatting buffer and no guest snapshots. Even maximum representable counters fit each512-byte production log record.

While compiled in and idle, the additional Present scope and main-controller preflight each cost one flag load/branch (plus a zero-token cleanup branch in the scope); scheduler cancellation now also checks the armed-capability flag. It does not read native thread IDs when both flags are OFF. All three active diagnostic arms additionally check exact Present ownership and invalidate on scheduler handoffs. Those common admission costs are therefore not isolated by the ON-minus-OFF comparison. Existing ON metadata, atomic decline counts and logical math tracking are observer cost, and must be assessed with the actual FPS results. Compile-OFF removes the observer references entirely. Counts describe whole windows, not individual frame latency or traversal size: they cannot prove the cause of recurring frame drops or a parallel-query speedup.

Validation additions: `tools/test_light_census_benchmark.py` compiles the actual benchmark state machine and unmodified `main.c` Present controller with ASan/UBSan, exercises both initial modes, exact120-frame count/timestamp boundaries, camera movement, native/context admission, cancellation, reset/take/mode/restore failures and busy retry. It verifies actual report parsing, suffix accounting, malformed/missing/duplicate/OFF-data rejection and maximum log lengths. `tools/test_remote.py` covers authenticated HTTP selection, fresh versus historical failure rejection and incomplete receipts. `tools/test_frame_acquisition.py` verifies this diagnostic changes none of the other overrides. The private original-oracle suite additionally exercises real Present capabilities with the actual object pool at zero/one/two workers and both owner-service directions, with ASan/UBSan and TSan. ARM ON/OFF builds include Present/Swap, benchmark, main and HTTP objects in addition to the original census closure. No device, emulator, final game link or live result is claimed.


## Clip-region coexistence

The optional clip36 region currently declines when census observation is active,
so original clip calls retain this observer's logical math scopes. Benchmark37
rejects an already-enabled region (or a linked controller without a readable
mode); benchmark36 rejects an already-active census. Neither rejection changes
modes or initializes the other controller. This prevents describing the middle
arm as pure observer overhead when it would also disable another candidate.
See [the clip integration instructions](native-clip-region.md#generation-and-selective-build)
for the four selective guest units and exact phase/HLE/root preservation checks.
