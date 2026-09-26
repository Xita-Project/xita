# Firing slowdown attribution — perf259

September 26, 2026. Build and package checks passed; deployment pending.

Perf258's ordinary lifepod AR sample remained about 75.8 ms/frame while firing
versus 56.8 ms idle. Its inverse-transform register lowering did not establish a
frame-rate gain. The earlier owner-only perf257 profile shows two distinct
contributors worth separating before another native replacement:

- Particle impacts: 10E240 → 1746F0 rises from unreported in the settled idle
  window to 2.79 ms/frame in the mixed firing/cooldown window. Static inspection
  shows calls to 112070 and 2B610, constructing effect and sound inputs.
- Object updates: 109760 → 900E0 rises from 49.03 to 56.00 ms/frame between the
  idle and mixed idle/firing reports; 90950 → C0EA0 and C3A00 also appear among
  the larger entries. These are inclusive instrumented wall times, with waits
  and scheduling included, and cannot be added to their parents or treated as
  an exact decomposition of the uninstrumented firing penalty.

The uninstrumented perf258 reports provide a separate check. Window ending
6360 is wholly inside idle; window ending 6480 overlaps firing and its first
cooldown frames. FA920 inclusive elapsed rises from 45.053 to 64.719 ms/frame;
scene-helper CPU from 48.36 to 56.68 ms/frame; scene wall from 49.05 to 59.59.
Draw count rises from 275 to 312, while the reported final wait changes from
1.1 to 0.8 ms. These overlapping paths must not be added. The final wait does
not cover every possible GPU dependency; it does not prove the GPU irrelevant.
The evidence favors examining extra update and scene-preparation work before
attributing this firing penalty to a full-GPU finish stall. Receipt:
`collision-transform-candidate/ar258-frame-paths.json`.

The diagnostic retains perf258 and adds direct-call scopes inside 1746F0,
112070, 2B610, C0EA0 and C3A00. The 158 emitted call sites include duplicate
translated paths; that count is not a per-frame call count. No gameplay,
physics, effects, sound, draw ordering or worker policy is changed.

```sh
python3 tools/patch_scene_phase_timers.py PRIVATE_STAGE/recomp \
  --parents 001746F0,00112070,0002B610,000C0EA0,000C3A00 --any-call
```

All translated shards were compared against perf258. Only the five selected
function bodies differ, and stripping observer pairs makes every shard
identical. The patcher's two host tests pass. The private candidate directory
`impact-phase-candidate/` contains the build, patch audit and scripts; generated
game code stays outside Git.

Use the protected `XV_TEST_SAVE=a30-perf211` launch with owner-only
`XV_SCENE_PHASES=2`, then settled idle/fire/cooldown input brackets. Inspect the
before/after screenshots and complete frame intervals. Reject scope overflow or
abandoned scopes. Timers perturb performance: this run selects the next target;
it is not an FPS comparison against perf258. Return to a cold launch with
`XV_SCENE_PHASES=0` for performance acceptance.

The next decision is whether impact allocation/initialization, sound creation,
or the growing object-update subtree warrants implementation work. Do not
assume that removing particle math can recover the entire firing penalty.
The sustained-20-FPS, NPC combat, cutscene and long-session gates remain open.

## Build receipt

Full developer Vita build succeeded. The compatible package replaces only
`game-a.self` and `boot-game.txt`, preserving the installed asset contract.
Runtime SHA-256: `ff7ff12ee5e425136fceb82d1058e379098cd1d601ab0676c609c598224ea37d`.
Runtime size: 34775314 bytes. Hardware results remain pending.

## Deployment checkpoint

The companion accepted quit/launch, but the in-app HTTP endpoint remained
connection-refused during the initial 60-second readiness check. The first
update command failed before upload; perf259 is not confirmed installed.
FTP and companion remained reachable. The previous gameplay log stopped at
2026-09-26 18:16:02 UTC; no new freeze-lights dump was present. This observation
alone does not establish a crash or justify repeated restarts.

A bounded `impact-phase-candidate/wait-dashboard.py` observer is running (exec
session 63856). It polls the existing launch for up to ten minutes and only
retries the update after receiving perf258 dashboard status with timing frame
zero. Poll that same handle and `wait-dashboard.log` / `deploy-retry.log` before
any further action. It does not relaunch the app. If installation becomes
boot-confirmed, run `launch-a30.py`, inspect readiness with `wait-game.py`, then
`ar259.py` (two four-second bursts with a reload) and `read-phases.py`. The latter
labels only complete 60-frame reports wholly inside an input phase as such;
mixed windows remain explicitly marked. Do not run the firing script before
visually confirming the protected gameplay scene.

Build 56643 and packaging 36333 completed successfully. Initial deployment
session 7208 is terminal failure. Keep-awake refresher PID 134059 is still alive;
its in-app renewal cannot succeed until HTTP returns. Do not use companion
press/nosleep as a substitute. Source changes are locally committed, not pushed.

## Pi attribution (completed)

The 180-second ARM run on cores 0/1 completed its planned timeout (124), with
66 frame reports. It used the existing native-object-query host harness and
four recompiled shards with the same five instrumented function bodies as the
Vita candidate (byte equality checked). The surrounding host runtime is older,
headless, and has different scheduling; these are supporting attribution data,
not Vita timings, a visual test, or an optimization acceptance result.

Script: campaign buttons at frames 150/300/450; fire at 1800 for 120 frames,
reload at 1930, fire at 2040 for 120 frames. The report labels are host frame
markers and asynchronous reporting may straddle an input boundary. No exact
weapon-tick alignment is claimed. The new impact/weapon call reports establish
that the input exercised these paths.

- Near report labels 1800 and 2040, C0EA0 costs 1.62/1.76 ms per host frame,
  including C0C60 at 1.52/1.66. C0EA0's residual is only 0.09 in each.
- At labels 1860 and 2100, 1746F0 costs 0.75/0.75, including sound entry 2B610
  at 0.68/0.69; its child 26B10 accounts for 0.62/0.65. This directs the impact
  investigation toward sound setup. It does not mean effects are absent or
  dispensable: small children can fall below the report's detail threshold.
- C3A00 at label 2040 costs 0.56, including C2D30 0.31 and C26A0 0.15. It is
  a smaller lead here than C0C60.

Private evidence: `impact-phase-pi/{body-audit.json,phase-windows.json,run.log}`
and `d3d-record2-work/pi-runs/codex-impact-phases-20260926.log`. No tick-phase
overflow/abandon notices were found. Parent times overlap with their children.
Next inspect C0C60 and 26B10's children to choose a useful replacement boundary;
rewriting only their small caller wrappers would miss most of this measured cost.

The Vita readiness watcher 63856 subsequently ended without an HTTP response;
no retry upload happened and perf258 remains the last confirmed installed build.
Companion display-on was requested without pad injection or nosleep. A user
screen-status question is pending. Do not silently restart the failed watcher.

A follow-up Pi harness adds 25 direct-call scopes inside C0C60 and 26B10 only,
with all observer-stripped bodies unchanged. It reuses the completed first
harness's other objects; no game behavior is changed. Its ARM build passed
(session 1239). A second planned 180-second run is active as session 89394,
`codex-impact-child-phases-20260926`, on cores 0/1 with the same input schedule.
Poll that handle, then inspect the nested child reports before choosing a
native implementation. Artifacts are in `impact-child-phase-pi/` and the usual
`d3d-record2-work/pi-runs/` directory. The first Pi run 95407 is terminal.

### Follow-up result

The second Pi run 89394 completed its planned 180-second timeout (124), with
68 reports and no tick-phase overflow/abandon notices. No Pi job remains active.
At host report labels 1800/2040, C0C60 is 1.58/1.81 ms inclusive, of which
C02F0 is 1.57/1.80; residual only 0.01. At labels 1860/2100, 26B10 is 0.71/0.75,
of which 26A50 is 0.47/0.49; residual 0.10/0.12. Keep the same asynchronous,
headless, overlapping-time limitations stated above. Full selected reports:
`impact-child-phase-pi/child-phase-windows.json`.

Static inspection confirms 26A50 calls 25590 and 2B460; the latter is the
existing sound-obstruction collision-vector route (native-1721b 0.md). This cost
is not audio sample decoding. The native BSP segment cast is already enabled,
so proposing it again would duplicate existing work. C02F0 is a larger weapon
routine (2,858 translated lines, 15 distinct callees), not a cheap wrapper that
can be replaced without callee attribution. The next offline action is to split
C02F0's actual callees and determine whether creation/initialization or another
shared subtree dominates. Do not claim all of C02F0 is collision work.

The Vita endpoint remains unavailable after the single quit/launch sequence;
FTP still responds and perf259 has not been installed. Await the screen-status
reply while continuing work that does not require hardware access. No new FPS
gain or crash fix has been established by these diagnostic runs.

## Weapon creation and sound-cache census

A further private ARM probe adds direct-call timers to C02F0, 95680, 26A50
and 2B460 (73 emitted sites); 94280 has no direct guest calls to wrap. All
observer-stripped translated shards remain identical to the previous supporting
harness. Full ARM build 88809 passed. No Vita payload was changed.

The preceding Pi run's sound counters rise from approximately 360 native casts
per 60-frame idle window to 1,600 during firing, while total queries grow from
roughly 3,400 to 5,600. The sound cache already exists, is enabled at six frames,
and hashes sound endpoints; do not propose adding this existing cache as new.

The new probe also observes hypothetical two-way and four-way retention using
the same 1,024-entry capacity, frame lifetime and endpoint distance predicates.
It returns the original cache/guest answer on every call; hypothetical hits do
not substitute collision results. It counts original misses as empty, expired,
listener-distance or sound-distance failures, in that order. These counters
select whether further cache work is justified; they do not establish output
correctness or a frame-rate improvement for an alternative policy. Cache
thread ownership and result correctness must be audited before implementation.

Private directory `weapon-create-phase-pi/` contains the patch audit, modified
observer unit and link script. Run 52821 is active for a planned 180 seconds on
Pi cores 0/1, tag `codex-weapon-create-phases-20260926`, with the same two-burst
input schedule. Poll this handle and inspect its completed log. The Vita
endpoint is still connection-refused; no repeated restart or update was made.

### Weapon/census result and candidate policy

Run 52821 completed the planned 180-second timeout (124), with 66 reports.
At host labels 1800/2040, C02F0 costs 1.59/1.75 ms inclusive: BF200 accounts
for 0.82/0.87, BFA70 0.64/0.73, and 95680 only 0.07/0.08. Its own residual
is 0.03. Thus 95680/94280 are not the primary implementation target in this
sample. BF200 calls 2B660 and 113930 (plus 8B910); BFA70 is a larger subtree.

The same-run hypothetical four-way cache reports 4,381/4,749/4,236/4,704 hits
near labels 1800/1860/2040/2100, versus actual reuses 3,690/3,997/3,564/3,915:
approximately 670–790 additional retained queries per 60-frame window. The
original cache reports 873–1,102 recent sound-endpoint mismatches in those
windows, compared with approximately 400 age expirations. Two-way retention
helps less. Neither age nor distance tolerances were loosened. These are
candidate-selection counts, not verified saved casts or FPS.

Caution: this older Pi runtime emits both tick and scene-helper phase reports
even with mode 2. Sound entry 2B460 appears on the helper too. The existing
sound cache and this diagnostic's counters/tables are shared, so thread/domain
ownership and concurrent report boundaries need attention; one window's rays
and completed cast/reuse counters differ by one. Do not treat the approximate
shadow counts as an exact serialized replay. Also 2B460's reported residual
includes the unwrapped `xv_sound_ray` native-hook branch, not just its own math.
Private evidence is `weapon-create-phase-pi/selected-windows.json` and the
complete Pi log. No new Vita test or FPS gain was obtained.

A reusable candidate policy is now in `recomp/kernel/xk_sound_cache.h`, with
1/2/4-way retention at 1,024 entries, memory-view and world-epoch keys, original
endpoint anchors, bounded age, and copied hit results. It is **not integrated
into xv_sound_ray or enabled in any build**. Its caller must serialize access,
release the lock before guest execution, and supply a valid world epoch. The
key fields increase bytes per entry; capacity equality is not byte-size equality.
It retains the original endpoint/age approximation rather than claiming exact
collision equivalence.

`tools/tests/sound_cache_policy.c` passed host ASan/UBSan and Cortex-A9-targeted
ARM execution on Pi core 0. Tests cover same-bucket collisions/oldest eviction,
1/2/4 ways, opposite answers in different views/epochs, maximum hash, expiration,
frame wrap/backward age, unchanged anchors, moved endpoints, nonfinite positions
and invalid ways. Receipts: `weapon-create-phase-pi/sound-cache-policy-tests.log`
and `sound-cache-policy-test-commands.json`. These are policy invariants, not
concurrency or gameplay verification. Next: integrate with short nonblocking
cache synchronization, world-change invalidation and view ownership; compare
candidate hits against actual guest casts before enabling it on hardware.

All Pi jobs in this section are terminal. Perf259 remains built but undeployed;
last confirmed hardware build is perf258, with its HTTP endpoint unavailable.
The pending screen-status question has not been answered. Keep the goal active.

## Weapon creation child probe (2026-09-26)

Prepared private `weapon-child-phase-pi/` from the previous weapon-creation
stage. Timers now wrap 3 direct calls in BF200 and 46 in BFA70. Stripping
observer lines reproduces the prior code_017 exactly (`patch-audit.json`).
Two BFA70 `f_000C003E(c); return;` tail-call exits are not wrapped by the
existing patcher and remain part of unmeasured parent remainder; do not claim
complete attribution. The ARM shard compiled and harness linked (`build.log`).
This harness retains the baseline sound path, not the verification-only cache,
so mandatory extra real casts do not confound its weapon-creation profiling.

Pi run `codex-weapon-child-20260926` is active on cores 0/1, local exec session
13531, 180-second planned duration. It uses the existing two-burst/reload input
sequence and scene phase timers. Poll that session and inspect the whole log.
The sound ray-key run and build session 19477 are terminal. Next use the child
breakdown to select a concrete native/repeated-work target; retain the ordinary
hardware AR baseline as the acceptance reference. Pi timings are supporting
profiles, never Vita frame-rate proof. No hardware deployment occurred.

## Weapon-child result and next target

`codex-weapon-child-20260926` completed its planned 180-second timeout (124),
with 67 host reports. Local session 13531 is terminal. The two reported firing
windows (host reports 1800 and 2040) attribute BF200's 0.87/1.10 ms inclusive
to 113930 at 0.86/1.09 ms; BF200 self is only 0.01 ms. BFA70's 0.82/0.78 ms
is led by 10AAF0 at 0.42/0.39 ms, then 8F120 at 0.16/0.15 and 8FC90 at
0.12/0.14 ms. BFA70 self is 0.06/0.05 ms. Do not rewrite these wrappers on
the assumption that their inclusive time is translation overhead in the body.
Private receipt: `weapon-child-phase-pi/selected-windows.json`, plus full log.
All numbers are instrumented Pi elapsed time; overlapping scopes and host
preemption remain included. The reporter prints only selected parents/children.

BF200 dispatches by tag class: its `effe` branch calls 113930, while its `snd!`
branch calls 2B660. The measured windows predominantly take the former.
113930 is a short wrapper over allocation/setup routines 111430, 110F60,
111F10, and 113370, with a 128-byte initialization between calls. It is not
reasonable to ascribe its entire inclusive cost to that initialization.

Prepared `weapon-effect-phase-pi/` to time direct children of 113930 (9 sites)
and 10AAF0 (40 sites). Inputs preserve earlier impact timers in code_024 and
weapon-child timers in linked code_017. Observer-stripped source equality
passed for both newly changed shards; no guest instruction changes.
Build session 55786 is active; poll it before copying/running its harness.
Build script and log are in that private directory. After successful linking,
copy as `pi:xita-d3d2/harness-codex-weapon-effect` and run pi30.sh with the
same 180-second baseline/two-burst arguments as the preceding weapon-child
run, unique tag `codex-weapon-effect-20260926`. Do not add sound verification
flags. Inspect complete firing windows before selecting the replacement.
No new VPK, deployment or demonstrated Vita speedup results from this probe.

Follow-up: build 55786 and transfer 38934 completed successfully. The planned
weapon-effect run is now active on Pi cores 0/1, local exec session 56721.
Poll this handle; no need to rebuild or restart the job.

## Effect creation result

`codex-weapon-effect-20260926` completed its planned timeout (124), with 69
host reports; session 56721 is terminal. At host reports 1800/2040, 113930
inclusive was 0.85/1.04 ms and its child 113370 was 0.78/0.96 ms. Other
children: 111F10 0.04 ms, 111430 0.01 ms, 110F60 approximately 0.00 at
report precision; wrapper self 0.01 ms. These are Pi instrumented elapsed
values, not Vita savings. Receipt: `weapon-effect-phase-pi/selected-windows.json`.
10AAF0 did not appear as a printed parent in this run's selected rows; that
absence does not mean zero cost. Avoid inferring its child breakdown.

The runtime already has page-aware repeated-byte memset in x_str_stos, which
covers this wrapper's 128-byte all-ones fill. The measured child costs also
argue against prioritizing that fill or rewriting the allocation wrapper.
Focus on 113370's processing instead.

Private `effect-process-phase-pi/` adds timers beneath 113370 (26 sites),
1122A0 (37), and 113080 (14), preserving the prior child probe. 111530 has
no direct calls. Observer-stripped source equality passed. Eight 111240
calls in 113370 use tail-call syntax and remain unwrapped; their cost may
appear in parent remainder. Do not label that remainder pure local math.
Build session 78342 is active, compiling code_024 and linking the harness.
Poll the existing handle. After success, transfer as
`pi:xita-d3d2/harness-codex-effect-process`, then use the previous baseline
180-second two-burst/reload pi30.sh command with tag
`codex-effect-process-20260926`. No sound-cache verification flags.
No new hardware build/deployment or performance gain has been claimed.

Follow-up: build 78342 and transfer 29351 finished successfully. Effect-process
run is active on Pi cores 0/1 with local exec session 81814. Poll this handle.

## Effect processing and a register-lowering lead

`codex-effect-process-20260926` completed the planned timeout (124), with 68
host reports. Session 81814 is terminal; no Pi job is active. The complete
selected parent rows are in `effect-process-phase-pi/selected-windows.json`.
113370 firing rows: inclusive 1.33/1.44 ms, remainder 0.69/0.83 ms, child
1122A0 0.31/0.30 ms and 112C80 0.27/0.27 ms. Recall unwrapped tail calls,
inline descendants, instrumentation and host preemption are not excluded
from the remainder; it must not be called pure arithmetic time.
1122A0 also appears regularly outside the two firing windows at approximately
0.9–1.1 ms inclusive, with 0.5–0.6 ms reported self. Its main reported child
is 10E8B0 (roughly 0.14–0.24 ms in the inspected later windows).

Concrete compiler lead: current 1122A0 has no register-stack locals and many
emulated X_ST accesses. The existing regeneration report
`x87-regs-work/regen-regs-d3-final/x87_regs_report.json` identifies its fallback
as `slot-range`, not merely low density. `recompiler/x87_regs.py` rejects a
plan when maximum minus minimum touched logical slot exceeds seven. Its
locals represent logical slots mapped onto eight physical slots, so simply
removing the rejection would create stale aliases and is unsafe.

Next investigate an opt-in physical-slot mapping for this specific function:
canonicalize operand locals, declaration/fill sets, and dirty spill sets
consistently modulo eight, retain depth joins and call guards, and compare
complete context/slot results against memory lowering under wrap and call
mutation. Preserve default generated output until qualified. Inspect actual
slot ranges/paths first; do not assume this report proves a speedup. Existing
particle/collision register differential tests are useful patterns, but their
cases do not cover this function. No Vita deployment or FPS gain yet.

## Tail-call attribution follow-up

Added opt-in `--any-call --tail-calls` support to the phase patcher. It splits
a bare `f_ADDRESS(c); return;` into call and return, placing the end observer
before the return. Defaults are unchanged. Tail-call elimination is inhibited
in this diagnostic, so timings include observer/call overhead. Unit tests
verify ordering, instruction retention and idempotence.

Private `effect-tail-phase-pi/` patches only parent 113370 on top of the prior
effect-process diagnostic: 34 call sites instead of 26, including eight
111240 tail sites. Removing observers and normalizing return whitespace
reproduces the prior source. ARM build and transfer completed. Run
`codex-effect-tail-20260926` is active as local session 98859 on Pi cores 0/1,
with the same 180-second two-fire-burst/reload inputs as the prior profile.
Poll the existing session; do not restart because of an observation timeout.

Source inspection shows 111240 validates an effect handle, walks linked
entries from an effect-related pool and invokes A92C0 for each. This is not
evidence of its runtime cost; the pending profile will resolve that.
Vita HTTP8080 still refuses, FTP log MDTM remains 20260926181602, and no new
freeze-lights capture exists. Perf260 remains uninstalled.

Follow-up: session 98859 completed the planned 180-second timeout (124),
67 host reports; no job remains active. The two reported 113370 firing rows
(at host report indices 1800 and 2040) are inclusive 1.11/1.18 ms and
self/remainder 0.59/0.63 ms. Largest children: 1122A0 0.26/0.27 ms,
112C80 0.21/0.22 ms, 113080 0.04/0.04 ms. Full rows are recorded in
`effect-tail-phase-pi/selected-windows.json`. 111240 is absent from the
top-eight listing (last printed children round to 0.00); this does not
prove zero calls or zero cost, but does not support a dominant hidden tail
expense. The reporter subtracts all timed children, including unprinted
children, before reporting self. The residual therefore remains a useful
lead, subject to inline work, instrumentation and host scheduling overhead.
No fatal/trap line was found by the log scan. This is supporting Pi evidence,
not hardware performance or rendering correctness. Perf260 still awaits
physical validation; investigate 112C80/local 113370 work next rather than
rewriting effect cleanup based on the old incomplete attribution.

## 112C80 child attribution and staged reporter correction

112C80 has only 19 X_ST references in the maintained body and falls back from
register lowering on density. Its 16 direct call sites reach 13 different
helpers, including object and sound paths. Measure these before assuming
translated floating-point math dominates.

Private `effect-child-phase-pi/` adds its 16 timers atop effect-tail, with
observer-stripped source identity verified. Its staged reporter now prints
112C80 even below 0.5 ms/frame. Inspection found the old baseline Pi object
uses the pre-fix report: one `(parent,callee)` entry for inclusive time but
all matching child entries for subtraction. The current repository already
fixes this, but that fix was not in these reused harness objects. Therefore
previous Pi self/remainder figures, including effect-process, effect-tail,
and physical-slot gameplay summaries, must not be interpreted as validated
aggregate self time where the same routine is entered under multiple parents.
Their raw observations remain preserved; no Vita FPS claim depends on them.

The new private reporter sums inclusive time across parent entries and labels
raw `parent>callee` edges, matching the existing production report correction.
`tools/tests/test_scene_phase_report.py` now accepts `--source` so it can
validate the actual staged reporter, not only the checkout. Both checkout and
staged report pass multi-parent totals, edge-label, and reset assertions under
host ASan/UBSan. This is a reporting-only correction, not a speedup.

Build and transfer completed. Run `codex-effect-child-20260926` is active on
Pi cores 0/1 as local session 40591, with the prior 180-second two-burst/reload
sequence. Poll this session; do not restart on observation timeout. No new
Vita build was made or deployed; perf260 still awaits a working dashboard.

Follow-up: first child run 40591 terminated normally at timeout 124 with 67
host reports. Its firing attribution is QUARANTINED. Adding deeper scopes
exposed another stale implementation: the reused Pi begin/end lacks skipped
scope tracking, so nesting beyond 16 can pop live outer scopes. Unexpected
parent edges cannot be trusted without overflow-safe collection. Previous
deep Pi effect profiles share this limitation; do not use them as precise
self-time or parent-attribution proof. No optimization is accepted on these
measurements alone.

Replaced the private stage's complete phase collector/reporter section with
the current checkout implementation (including owner-only mode 2, lookup hint,
skipped-scope recovery and overflow reporting), retaining the 112C80 reporting
threshold exception. Staged report and depth tests both pass; depth test now
also accepts `--source`. Build and transfer succeeded. New 180-second run
`codex-effect-child-v2-20260926` is launched on cores 0/1; use its live exec
session 65064 to poll. All v1 jobs are terminal. No hardware deployment.

### Overflow confirmed in the corrected collector

V2 session 65064 is terminal (planned timeout 124, 68 reports). Four windows
explicitly report nesting overflow: 360, 96, 360, 96 omitted scopes. Recovery
preserves outer attribution, but omitted children still make self totals
incomplete. The two 113370 firing rows now show inclusive 1.16/1.07 ms and
self 0.06/0.05 ms; largest children 1122A0 0.80/0.72 and 113080 0.26/0.25 ms.
This contradicts treating the older 0.6–0.8 ms remainder as local wrapper
work. Do not optimize that wrapper based on the stale measurements. V2 rows
and warnings are preserved in `effect-child-phase-pi/v2-windows.json`.
No fatal/trap line was found; no hardware performance result is implied.

Private diagnostic capacity increased to 64 scopes (production remains 16).
Depth tests now derive capacity from the compiled arrays and pass for both
production 16 and staged 64, including overflow recovery and per-thread
isolation. Build/transfer finished. V3 run `codex-effect-child-v3-20260926`
is live as exec session 73941 on Pi cores 0/1 with the same 180-second inputs.
Poll that handle and require no omitted-scope reports before attributing
its deepest effect costs. Perf260 remains built but uninstalled.

### V3 completed: no omitted scopes

Session 73941 is terminal: planned timeout 124, 69 host reports, zero omitted
scope messages, no fatal/trap line found. All current Pi jobs are finished.
Complete relevant rows are in `effect-child-phase-pi/v3-windows.json`.
Two firing windows (host report indices 1800/2040), ms per reported frame:

| Routine | Inclusive | Self/remainder | Leading children |
| --- | --- | --- | --- |
| 113370 | 1.04 / 1.09 | 0.06 / 0.06 | 1122A0 0.70 / 0.74; 113080 0.25 / 0.25 |
| 113080 | 0.63 / 0.63 | 0.09 / 0.08 | 112C80 0.52 / 0.52 |
| 112C80 | 0.52 / 0.52 | 0.05 / 0.04 | 2B660 0.26 / 0.25; 963C0 0.17 / 0.18 |
| 1122A0 | 1.68 / 1.69 | 0.74 / 0.74 | 10E8B0 0.43 / 0.44; 111950 0.14 / 0.13 |

Totals aggregate each routine across parents; a child total can exceed its
contribution under one listed parent. Do not add inclusive rows together.
Timers include instrumentation and host scheduling. Zero omitted scopes
removes the identified nesting limit, not every possible measurement error.

Next targets: retain perf260 hardware qualification as first priority; trace
2B660's sound setup (2B140 then conditional 26B10) and 963C0's object work
(8D320, looped 95E50, final 8A300) before choosing native replacement or caching.
Do not rewrite 112C80/113370 local math on the old large self estimates. The
current 1122A0 cost supports continued investigation but does not revalidate
the previous candidate speedup comparison collected with the old reporter.
No new hardware performance result; HTTP8080 still refuses connections.

## Combined sound/object service diagnostic

Private `effect-services-phase-pi/` retains V3's corrected 64-scope owner-only
collector and all prior effect probes, adding 108 direct call sites beneath
26B10 (15), 2B660 (2), 2B140 (5), 963C0 (5), 95E50 (78), and 8D320 (3).
Modified shards 004/005/014 derive from the previous weapon-create diagnostic;
013 derives from the maintained baseline. Observer removal and return
whitespace normalization reproduce their respective inputs. No optimization
or game behavior change is intended.

The staged report/depth tests passed before build. Both build steps and
transfer completed. Run `codex-effect-services-20260926` is active on cores
0/1, same 180-second a30 two-burst/reload sequence. Inspect its terminal result
and omitted-scope warnings before trusting the deeper attribution.

Static inspection: 2B660 calls 2B140, then conditionally 26B10; 26B10 includes
a recursive call. 963C0 calls spatial helper 8D320, iterates results through
95E50, then calls 8A300. This does not by itself prove the costly descendant.
HTTP8080 still refuses; perf260 remains ready but not installed.

Combined service run completed (session 14161 terminal, timeout 124, 68 reports).
No omitted scopes or fatal/trap lines were found. Firing windows 1800/2040:
2B660 inclusive 0.24/0.24 ms, self 0.01/0.01, child 26B10 0.21/0.21. Across
all parents 26B10 totals 0.59/0.56 ms, with 26A50 0.39/0.40, self 0.07/0.06.
The recursive 26B10 child contributes 0.03/0.02; inclusive recursive totals
can count nested work more than once and must not be added as exclusive cost.

963C0 totals 0.18/0.22 ms, self 0.02/0.02;8D320 contributes 0.14/0.16.
8D320's largest child 52240 is 0.08/0.09, versus 8C7E0 0.02/0.02 and local
remainder 0.04/0.04.95E50 is near report precision in this scene; this is not
evidence that processing many nearby objects is universally cheap.
Full rows:`effect-services-phase-pi/selected-windows.json`. All jobs terminal.

Next sound boundary is 26A50: source checks up to four active listeners at
2E3018 (stride 0x44), calls 25590, chooses a distance, takes its square root
and conditionally calls 2B460. Profile those two children before replacing
the scan. Query reuse lead: native 92330 already implements 52240's portal
flood internally, but its public hook covers 56670, so 8D320's direct 52240
call remains translated. Reusing that subtree at a new boundary requires
separate guest-context/flags/x87/memory equivalence tests; existing light
query validation does not establish the new boundary. Perf260 hardware
qualification remains pending; do not infer Vita gains from these Pi times.

## Listener preparation split (September 26)

Private `listener-phase-pi/` derives shards 004/005 from the combined-service
stage and retains its other objects and corrected 64-scope owner collector.
Five direct-call probes were added beneath 26A50 and 2B460. 25590 is a leaf
(no child probes). Crucially, the active `xv_sound_ray()` branch at 2B549
has an explicit wrapper probe `F002B549`; the guest 1721B0 call is only the
fallback branch when the sound hook is compiled out. `F002B549` is a synthetic
diagnostic label, not an executable guest address.

Observer removal plus whitespace normalization reproduces both input shards;
`patch-audit.json` records the checks. Staged multi-parent report/reset and
scope overflow/isolation tests passed. Build and transfer completed, then
`codex-listener-20260926` ran the same 180-second a30 two-burst/reload sequence
on Pi cores 0/1. Session 8140 is terminal: intended timeout 124, 65 reports,
no scope-overflow warnings or fatal/trap lines. No candidate was deployed.

Firing windows 1800 / 2040, milliseconds per reported Pi frame:

| Boundary | Inclusive | Self | Children |
| --- | --- | --- | --- |
| 26A50 | 0.64 / 0.67 | 0.09 / 0.08 | 2B460 0.53 / 0.56; 25590 0.03 / 0.03 |
| 2B460 | 0.93 / 0.97 | 0.12 / 0.13 | sound wrapper F002B549 0.77 / 0.80; 551D0 0.04 / 0.04 |

These aggregate routines across all timed parents; do not add their inclusive
values. Additional instrumentation and scheduling affect these values, so
this is attribution, not a performance regression comparison with the previous
run. Full rows: `listener-phase-pi/selected-windows.json`.

Sound-cache counters in those 60-frame windows: 4,903 rays / 3,563 reused /
1,340 cast; then 4,824 / 3,609 / 1,215. That is approximately 73–75% reuse
already. Reuse is not a newly gained speedup. Native-1721B0's total call count
also includes unrelated callers and cannot isolate audio cost by itself.

Decision: do not prioritize replacing 25590's distance arithmetic or 26A50's
four-listener scan. Most of this measured chain is the existing sound-ray
wrapper and its remaining collision casts. Next isolate actual cast time
inside that wrapper, then investigate why misses occur (movement, expiration,
slot collisions). Keep the previously deferred set-associative candidate in
verification mode until world-state invalidation and side effects are proven;
its earlier hit rate alone does not justify enabling it. Hardware perf260
qualification remains pending because the Xita HTTP endpoint is unavailable.

## Sound cache miss/cast diagnostic

Compile `xk_sound_obstruction.c` with `XV_SOUND_CACHE_PROFILE=1` to time its
actual collision calls under synthetic phase ID `F01721B0` and report rejected
legacy-cache lookups. Miss reasons are a bitmask: 1 empty slot, 2 expired age,
4 listener displacement, 8 endpoint displacement. Multiple bits can apply;
mask 0 is a verify-forced cast of an otherwise reusable ray. Empty entries
are classified separately. Endpoint displacement alone does not distinguish
sound motion from hash-slot collision, because the legacy entry stores no
stable sound identity. Do not interpret every displaced endpoint as a hash
collision or independently sum overlapping reason categories.

Diagnostics add observers/counters only; cache acceptance, replacement and
returned guest results are unchanged. Candidate set-associative mode retains
its separate existing verification counters. Normal builds compile to the
original direct calls with no profiling arrays or observers. Cross-compiled
ARM O1 default `.text` matched the pre-edit source exactly (4,556 bytes).

Private `sound-miss-phase-pi/` preserves compiler receipt, source/header
copies, hashes, default-object comparison and harness. It uses the listener
stage's guest objects and corrected phase collector, with the sound runtime
rebuilt from current source (candidate cache disabled in this run). This is
not a paired whole-harness speed comparison with the prior stage's older
sound object. The 180-second ordinary Pi input sequence uses only cores 0/1.

Completed `codex-sound-miss-20260926`: session 98569 terminal, intended timeout
124, 68 reports, zero scope-overflow or fatal/trap lines. Miss-reason counts
reconcile with cast counts in every reported sound window. Selected records
are in `sound-miss-phase-pi/selected-windows.json`.

Firing windows 1800 / 2040: wrapper inclusive 0.82 / 0.81 ms, self 0.04 / 0.04,
actual collision child 0.77 / 0.77. Rounding explains small nonadditivity.
Thus the actual casts dominate; optimizing cache arithmetic alone is not the
large opportunity. Counts (each window covers 60 frames):

| Reason mask | 1800 | 2040 |
| --- | ---: | ---: |
| 1: empty | 18 | 6 |
| 2: age only | 382 | 377 |
| 8: endpoint only | 935 | 927 |
| 10: age + endpoint | 23 | 25 |
| 14: age + listener + endpoint | 4 | 1 |
| Total casts | 1,362 | 1,336 |

Rays/reused: 4,915/3,553 and 4,860/3,524. Endpoint-only rejection accounts for
about 69–70% of firing-window casts; any age involvement about 29–30%.
Earlier non-firing samples were age dominated, so do not generalize that
live observation to the firing workload. Next distinguish endpoint movement
from different spatial cells mapping to the same slot, or profile the actual
collision descendants. Extending TTL cannot solve most firing misses and
would increase stale-world risk. No cache policy change or Vita gain is
established. All diagnostic jobs are terminal; perf260 remains undeployed.

## Endpoint grid diagnostic

The optional sound-cache profiler additionally classifies endpoint-rejected
casts by the stored and requested half-unit grid cell. Different cells in
the same direct-mapped slot establish a spatial hash collision. Same-cell
rejection can still involve motion or different sounds within that cell;
neither class proves that reusing the previous result would be correct.
Fresh versus expired entries are reported separately, so old collisions
are not mistaken for current avoidable work. No new stable sound identity
or cache policy is introduced.

Private `sound-cell-phase-pi/` rebuilds only the diagnostic sound runtime on
the previous listener harness. Default ARM `.text` remains identical to the
pre-diagnostic object. The repeated 180-second run is needed for the new
miss classification, not to claim a whole-frame improvement.

Completed `codex-sound-cell-20260926`: session 97835 terminal, intended timeout
124, 68 reports, no scope-overflow or fatal/trap lines. Cell counts reconcile
with endpoint-rejected counts in all 61 reported sound windows. Selected rows
are retained in `sound-cell-phase-pi/selected-windows.json`.

| Endpoint rejection class | Firing 1800 | Firing 2040 |
| --- | ---: | ---: |
| Fresh, same grid cell | 965 | 833 |
| Fresh, different cell (slot collision) | 1 | 3 |
| Expired, same cell | 5 | 19 |
| Expired, different cell | 16 | 31 |

Almost all fresh endpoint misses are within the same half-unit cell. A better
hash avalanche cannot distinguish positions already quantized to the same
cell, so changing only the hash is not supported by this result. This does
not prove a single sound is moving: multiple sounds in one cell can also
replace each other. Multi-entry retention may help only if distinct eligible
rays recur, and must be evaluated with exact endpoint/vector distinctions and
world-state validation. Increasing tolerance would hide distinct rays rather
than establish equivalent collision answers. Keep the existing candidate
verification-only. Faster collision processing remains the alternative to
reducing cast count; no new hardware performance claim follows from this run.

## Collision descendants and diagnostic build alignment

`sound-collision-phase-pi/` adds 22 call-site scopes beneath 1721B0, 171AF0
and 1731D0; 172DE0 has no direct children. Observer removal reproduces the
previous shard 028 exactly. `codex-sound-collision-20260926` completed its
180-second two-burst sequence on Pi cores 0/1: session 39325 terminal,
intended timeout 124, 68 reports and no omitted scopes.

Firing 1800 / 2040, Pi ms/reported frame:

| Boundary | Inclusive | Self | Leading children |
| --- | --- | --- | --- |
| Sound cast F01721B0 | 1.67 / 1.67 | 0.33 / 0.34 | 171AF0 1.23 / 1.23; 88E90 0.11 / 0.11 |
| Object walk 171AF0 (all timed parents) | 1.90 / 1.89 | 0.72 / 0.71 | 1731D0 0.74 / 0.75; B0CB0 0.36 / 0.36 |
| Object collision region 1731D0 | 0.74 / 0.75 | 0.22 / 0.22 | 88E90 0.28 / 0.29; B6210 0.11 / 0.10; B5E40 0.07 / 0.07; B5EA0 0.06 / 0.06 |

Deep timers substantially inflate the cast compared with the wrapper-only
profile. Use this to identify branches, not as a whole-frame regression or
Vita-time estimate. Aggregate parents and recursive totals overlap.

Alignment audit found the legacy Pi harness retains the pre-perf258 B6210
transform and pre-perf260 1122A0 effect routine. It must not be presented as
an exact perf258/perf260 hardware surrogate, nor should those updates be
counted as new optimization opportunities. Private
`sound-collision-phase-pi/candidate-alignment.json` records the differences.

`sound-aligned-phase-pi/` replaces only those two function bodies with the
perf260 candidate's versions, retaining other diagnostic objects and restoring
1122A0 child timers. `candidate-body-audit.json` verifies both bodies match
the candidate after removing observers/whitespace. The B6210 shard also needs
the candidate's `xv_x87reg.h` include; an initial link caught its omission
(`x87r_compare` unresolved), which was corrected before use. This aligns the
two known optimized functions, not the entire host runtime to Vita. The next
profile must use this aligned diagnostic before choosing a transform change.
The object-walk branch is the larger remaining lead; B0CB0 and 88E90 already
have native implementations and must not be advertised as new replacements.

Aligned run `codex-sound-aligned-20260926` completed: session 52539 terminal,
intended timeout 124, 68 reports, no omitted scopes or fatal/trap lines.
Firing 1800/2040 still points to the object walk: sound cast 1.56/1.56 ms,
child 171AF0 1.15/1.14 versus world 88E90 0.10/0.10. Across parents, 171AF0
is 1.75/1.77 inclusive and 0.65/0.66 self; 1731D0 0.70/0.70, B0CB0
0.32/0.33. B6210 beneath 1731D0 is 0.09/0.09. These diagnostic observations
support the branch choice, not a measured hardware speedup from alignment.
Rows: `sound-aligned-phase-pi/selected-windows.json`.

A bounded existing-codegen experiment is prepared privately in
`object-walk-registers/`: regenerate with `--profile halo_ce_3925 --x87-regs
--x87-regs-only 171AF0,1731D0`. The report converts 171AF0 with eight x87
instructions, eleven synchronization points, one local slot and no guards.
1731D0 has no local x87 instructions, so this pass cannot accelerate its own
body. The 171AF0 candidate and maintained reference have the same 207 guest
instruction comments in order (`lowering-audit.json`). Candidate C is 17,988
bytes versus 17,051 reference bytes; C size is not native machine-code cost.
This is only a prepared candidate: no runtime hook, package, deployment or
correctness/performance claim. Its low x87 density makes it a limited prospect;
validate complete state/callee boundaries and actual cost before inclusion.

### 171AF0 register candidate validation

Added `tools/test_object_walk_registers.py` and
`tools/tests/object_walk_registers.c`: synthetic differential comparison of
the complete function against the maintained memory lowering. It compares
the entire 4 MiB arena, full xctx, call-boundary context/arguments and
preemption counts. Sibling traversal, one recursive child level, ignore and
object-flag filters, both collision-result paths and all eight synthetic
callees are exercised. Mock callees overwrite x87 scratch slots/status while
preserving their proven stack effects. This tests compiler lowering, not the
real collision callees or real-world object topology.

Host O1 ASan/UBSan and Pi Cortex-A9 Thumb O2 with thread-table/render-view
both passed 1,000 cases / 6,221 candidate callee observations. Every modeled
callee was required to execute at all eight x87 TOP positions; input TOP is
selected independently of the low scenario bits choosing the paths. Recursion
depth reached two. Final run sessions 1098 (host/build), 97769 (Pi) and 15310
(gameplay diagnostic build) are terminal. No timeouts or guard fallbacks.
Private logs/receipts: `object-walk-registers/host`, `host.log`, `arm`,
`arm-run.log`. Inputs are bounded synthetic finite values, not exhaustive.

Uninstrumented Cortex-A9 Thumb O2 function text is 4,156 bytes reference and
4,164 candidate (+8 bytes), with the same staged preamble and FP-contraction
disabled. This is code-size evidence, not instruction counts or speed.
Private `reference-cost.o` / `candidate-cost.o` retain the measurements.

`object-walk-registers/gameplay/` now contains a built diagnostic harness
with only 171AF0 changed over the aligned diagnostic, retaining B6210 and
1122A0 updates and restoring the same child observers. `body-audit.json`
checks the candidate after observer removal. No production recompiler
selection/default or Vita package has changed. Next is ordinary Pi gameplay
to look for regressions and determine whether the low-density lowering has
any useful effect; hardware qualification remains necessary.

171AF0 gameplay diagnostic completed (`codex-object-registers-20260926`,
session 30236 terminal, intended timeout 124, 68 reports). No omitted scopes
or fatal/trap lines. It did not demonstrate improvement: firing self time
0.71/0.69 ms versus aligned reference 0.65/0.66; settled reported self median
0.29 versus 0.26. These are separate diagnostic runs with possible workload
and scheduling differences, not proof of a precise regression. Retain the
validated candidate privately, but do not add it to the hardware stack on
this evidence. Raw comparisons: `object-walk-registers/gameplay/comparison.json`.

Next codegen lead: 171AF0's type-mask shifts at 171B4E and 171B88 still call
flag-producing x_shl32 helpers before subsequent TEST instructions replace
flags. The emitter's existing dead-flag removal strips textual X_FLAGS
stores, not side effects inside shift helpers. A safe experiment must prove
all flag outputs dead along the actual control flow, preserve count masking
(including count zero), and respect shifts' conditional flag writes. It must
not indiscriminately treat a variable shift as killing incoming flags.
Two direct 171AF0 call sites were found (recursion and 1721B0); both immediately
test AL, but that alone does not prove all other state dead or exclude indirect
callers. No relaxed-state native or shift optimization is enabled yet.
