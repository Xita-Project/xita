# Silent Cartographer hardware baseline

The user replaced a30 with Silent Cartographer (`b30`) as the primary performance
baseline. Do not run further a30 performance comparisons. Preserve its checkpoint.

## Acceptance

Target sustained 20 FPS or better (50 ms/frame or less) during the opening
landing, beach traversal and active combat with multiple NPCs and effects.
Record mean, p50/p95/p99, maximum, counts above 50/100/200 ms and recurring
stalls separately. Verify actors actually move, fire and react; a broken AI
simulation is not a performance pass. Verify weapons, effects, audio and
checkpoint save/resume. Require multiple cold launches and at least 15 minutes
of active gameplay without crashes or new regressions. Success here does not
establish performance on every campaign mission.

## First capture

Private artifacts: `../b30-baseline-299/`. Isolated save:
`ux0:data/xita/test-saves/b30-perf299`, freshly created with empty udata/tdata.
No a30 checkpoint or normal player saves copied or changed. The device has
`haloce/maps/b30.map` (162877440 bytes). `XV_LEVEL=b30` redirects the first
campaign entry through the existing guest level tables; the menu label can
still say Pillar of Autumn. Confirm the loaded scenario in logs and screenshots.

The prepared environment keeps the previous runtime settings, including 360p,
and adds `XV_RENDER_PROFILE=1`. Record actual clock and configuration from the
run; do not describe this as native-resolution/default-settings evidence.
Perf299 changes attribution only: fragment-uniform time now includes the entire
binding helper. This is part of submit time, not an additive GPU execution cost.

Launch and observer scripts are guarded against wrong versions and benchmark
mode. A fresh profile may need menu setup; inspect the initial menu once before
recording a reusable input sequence. Never replay the a30 movement controller
on b30. No measured b30 results yet.

## Initial hardware launch

Perf299 verified update and boot in slot1; runtime SHA recorded in the repository
review journal. Initial launcher86649 completed0. Menu screenshot confirmed
Campaign selected. Two cross presses created a fresh profile; subsequent screenshot
confirmed Normal difficulty selected. Cross requested level launch, with status
receipt in start-level.json. No a30 route controller was run. Collector76652
remains live, renews lease each60s, bounded900s, writes b30-stream.log/status.
Next verify actual b30 scenario load, identify cinematic and gameplay frame
boundaries, inspect AI activity and analyze the completed intervals. Profile
creation and launching are not proof of successful level rendering.

## First measured opening interval (preliminary)

Collector76652 remains live. Map load logs explicitly name b30.map and isolated
cache path; screenshot landing-check.png at remote frame7502 shows Pelican/ocean
cinematic. Remote frames have a375 offset from timing_frame in observed status
receipts. Preliminary Present interval7141..7260 (120frames) averages119.000ms
(8.40FPS),p95135.815,p99161.189,max167.940; all120 exceed50ms and115 exceed100ms.
This is a short opening interval, not the complete cutscene or combat result.

Adjacent mesh7140..7259 reports: submit approximately5.95ms/frame; whole
fragment-uniform helper0.549ms, vertex uniforms1.441ms, draw0.816ms. These
subspans overlap submit and are not GPU timings. Frame/mesh correlation must
retain their separate indices. Scene helper cpu26.88–28.89ms, scene wall
29.80–32.21ms, done-to-noticed85.65–88.46ms. Owner FA920 reports subsequently
108.82–114.06ms inclusive/frame; includes nested work/waits, not CPU self.
No full-GPU Finish calls in those render-time reports. This evidence deprioritizes
fragment conversion as the main cause and prioritizes owner-update attribution.
It does not prove the entire owner span removable or GPU cost negligible.

Private analyze.py selects only complete Present windows and requires separate
explicit mesh bounds for submission summaries. opening-preliminary-summary.json
retains results. No new optimization was applied during this capture, no
AI-combat validation yet, and no FPS success claim.

## Beach capture and independent tick profiler

beach-check.png and beach-later.png show player on beach, marines/enemies,
shield and weapon effects. Enemy firing/perception correctness not proven;
screenshots are not a controlled AI test. Interval8401..9600:1200frames,
123.746ms mean/8.08FPS,p95152.929,p99175.864,max272.506;1200over50ms,
1168over100ms,5over200ms. Adjacent20mesh reports: vertex uniforms3.654ms,
fragment uniforms1.548ms, draw1.879ms; whole submit roughly12.818ms.
Input history for this interval is not fully controlled; no improvement claim.

Added XV_TICK_PHASES positive selector to xk_scene_thread.c: when legacy
XV_SCENE_PHASES<=0, use existing owner-only timer mode2 internally. Does not
change the environment read by root-pair configuration, does not set XV_PHASE,
and excludes scene helper and object worker threads. Native fused callees may
remain inside enclosing spans; residuals are not CPU self time. Seven selector
combinations pass ASan/UBSan nested-clock tests; concurrent worker isolation
test passes and detects its unprotected mutant. Root-pair actual acceptance
must still be checked on device.

Private tick-profile-300 stage built30351 exit0. Initial baseline-equality
check stopped preparation because staged comments differed; resolved by
replacing only phase_mode and verifying comment/whitespace-stripped source
identity. Initial no-op build retained299; final build/version and audit are300.
Audit passes: only xk_scene_thread plus four version-bearing objects changed;
only game-a.self/boot-game.txt differ, updater contract preserved. Runtime
SHA:e2881de707caa9deb3b65b7363d61abe85374cd966ae6268c662a1a4b214e76f.
Package:xita-perf300c.vpk, SHA
ba6161a9ec8e3bfa6fd6b8bf81e4b029da933fbff159e950116022eb0142c102.
NOT deployed. Current299 capture76652 is still active under original900s
bound. Next finish capture, protect b30 save, announce interruption, update300
and launch b30 with XV_TICK_PHASES=1,XV_SCENE_PHASES=0,XV_ROOT_PAIR=1.
No a30 test. Goal remains unmet.

## Perf300 deployment in progress

Collector76652 completed0 after900s; not a15-minute active gameplay pass because
that bound includes menu/loading/cinematic. b30 checkpoint backup93096 completed0:
six files, matching file set and two-read hashes; private tick-profile-300/
save-before-update/receipt.json. No a30 save changed.

Updater29742 live, install.log advancing; do not start another updater. Prepared
b30-tick-300 scripts retain isolated b30-perf299 save and all rendering policies.
Remote launch owns at most32 keys: replaced redundant XV_REC_DEFER_SPIN_US=0
with XV_TICK_PHASES=1. Retrieved xita.cfg contains no spin override, env.txt
absent, and production xv_rec_defer.h defaults spin to0; this preserves policy.
XV_SCENE_PHASES=0 and XV_ROOT_PAIR=1 remain explicit. New stream/launch are
NOT started until verified update/boot. Initial launch script only enters Halo
and captures menu; next select profile/resume using verified current screen.

AI qualification remains open. Prior campaign-npc-observation notes likewise
do not prove NPC shooting. Render-view actor snapshot labelled FROZEN belongs
to scene snapshot isolation, not evidence that owner AI simulation is frozen.
Do not change simulation tick rate or remove actors to meet the frame target.

Perf300 updater29742 still live after upload; an independent update-status
request returned ConnectionRefused during restart. Not a terminal failure;
keep polling the original updater. Private timed-call-map.json audits the actual
staged functions: FA920 and109760 direct translated callees are instrumented;
900E0 includes8ECA0 without a direct timer, so remaining parent time includes
unmeasured work. Native replacement paths also retain enclosing attribution.
Do not label residuals as exclusive CPU time or infer an unlisted callee free.

Updater29742 completed0:34871754bytes, matching runtime SHA e2881de7...,
verifiedtrue,restartrequestedtrue,slot0,bootconfirmedtrue. Launched separate
collector50866 and launch93548. Resume controller87193 waits for new menu
screenshot then runs existing campaign/profile/resume three-cross sequence
with5s delays and releases controls. Poll these exact handles; no duplicate
launch/controls. Inspect resume-request.png and verify b30 loaded, root-pair
admissions still nonzero and tick-phase reports before interpreting timings.
All new artifacts in ../b30-tick-300. Save remains b30-perf299, backed up.

## Menu correction and capture continuation

Initial resume87193 completed0 but screenshot showed Choose Difficulty, not
resumed gameplay. Back navigation revealed Load Level then Select Profile.
The attempted existing-profile launch23907 completed its inputs but subsequent
screenshot showed locked Load Level entries; do not label this checkpoint
resume success. Created New002 within isolated b30 save via triangle from
Select Profile, accepted default name, then Normal; controller68800 completed0.
fresh-loading.png shows transition; actual loaded scenario still needs logs.
Original test checkpoint backup remains untouched.

Collector50866 returned terminal143 (no traceback/reason supplied), last status
elapsed257.5s. Do not infer game crash; remote screenshot/control still worked.
Preserved original b30-stream.log and started collector4402 using stream2.py,
new b30-stream2.log/status and reports2, reading remote log from0. No game
restart or duplicate input controller. Must analyze new file, not stale stream1.

Seven phase-mode configurations also passed on Pi with static Cortex-A9 ARM
fixture phase-mode-300-arm. Supporting observer correctness only, not FPS.
Next confirm b30 load, root-pair acceptance and tick reports in stream2.

## First perf300 loaded tick evidence

stream2 shows b30 load I/O and tag completion. Root-pair remains active:
37417admissions in transition, then~75240–75269per60frames. Tick scopes work
without the legacy scene selector. Latest windows show120 calls to109760
per60frames (two simulation ticks per rendered frame). Owner FA920135.81–
136.86ms inclusive;109760133.38–133.82; object900E0108.24–110.36; AI14A162
22.12–23.92. Nested:9095048–49ms,4C980~25ms; do not sum these.
Earlier detailed window: 4B9D0→49600→172BF0→171F10→88110 is the dominant
collision chain, while pose8DDF0 and92330 also contribute.

Observer overhead warning: many tens of thousands of timed object calls per
60frames; broad tick profiling can add substantial work and affect catch-up
tick counts. These diagnostic times are not a perf299/300 regression comparison
or exact uninstrumented self costs. Prefer a sparse set of outer tick scopes
for the next observer, keeping all game updates intact, before promoting a
large cost claim. No dropping simulation ticks to inflate FPS.

Started bounded Pi b30 CPU sample7721 after confirming Pi idle and b30 map
exists162877440bytes. PID23789 and timeout23788 verified live at41s. Uses
retained297 CPU sampler, corrected native/recording knobs, new isolated save,
XV_LEVEL=b30, no scripted firing,300sbound. Remote runs/
codex-b30-sample297-20260928. This is supporting CPU attribution, not perf300
parity or graphics/combat validation. Poll existing7721 to terminal and fetch
samples/log before another Pi job. Vita collector4402 remains live.

## Sparse owner timer qualified locally (perf301)

XV_TICK_PHASES>=2 selects sparse owner mode: only FA920,109760,900E0,14A162,
108FD0,BCB30. Rejected addresses return before thread queries and clocks in
both begin/end; selected nesting remains intact. Legacy positive scene modes
retain both-thread behavior except original2=owner-only. No root-pair, update
count, tick rate or worker-policy changes. Twelve host ASan/UBSan selector
cases pass, including10000ignored callbacks with zero added clock reads,
helper exclusion and exact nested times. Concurrent worker isolation passes.
Five sparse/precedence cases pass on Pi core2, separate from CPU sample0/1.

Private tick-sparse-301 build98958 completed0; audit passed unchanged updater
contract/assets except game-a.self and boot marker. Changed objects only
xk_scene_thread and four version-bearing files. Runtime SHA
8e804a0f4325b0805cc20600cd0fe7919987064e818cfd44eb4277d53c0ecb9a, package
fe055ab4883ed0d3ee7feddd082c1cfd0e1d7cf548c83ef232bcb1e210e9c19b.
Not deployed. Vita300 collector4402 live at423s, timing15859, awake3569s.
Pi sample7721 remains live under original300s bound. Next collect its terminal
result; deploy sparse observer after preserving current capture/save.

Pi sample7721 subsequently completed planned timeout124; fetch60208 started
for game.log/samples.txt/exit-code into b30-pi-sample-297. No restart required.
Use matching cpu-sample-297/harness for symbolization; qualify loaded scene
from log before choosing sample range.

## Sparse profiler deployment in progress

Previous conversational goal turn was no progress; resumed with live Vita
status perf300, collector4402 confirmed running. Saved nine b30 profile/save
files to tick-sparse-301/save-before-update; two reads hash-identical and
file sets match. Collector4402 intentionally stopped130 after preserving
b30-stream2.log. Updater21763 started for qualified301; poll existing handle.
No a30 files touched. Prepared b30-tick-301 launch/collector with
XV_TICK_PHASES=2, SCENE_PHASES=0, ROOT_PAIR=1. Not launched yet.

Pi sample fetched and symbolized using cpu-sample-297/arm/harness.
Profile at b30-pi-sample-297/profile.txt: owner25001 samples/69windows;
guest-read9.1%, memcpy3.8%, collision86F502.1%, pose8DDF01.9%,
collision87EA01.6%. Linux syscall9.2% is not Vita GPU time. Headless log
confirms b30.map and loaded/active gameplay, but this does not verify
enemy firing or a rendered combat encounter. Keep AI behavior as an
explicit acceptance gate; do not equate quiet enemies with an optimization.

Updater21763 completed0:34871774bytes, runtime SHA8e804a0f...,verifiedtrue,
restarttrue,slot1,bootconfirmedtrue. Independent status confirms perf301,
timing_frame0,CPU444MHz. Collector98838 and launch47689 started for301;
poll exact handles. Collector bound900s, renews lease60s. Launch captures menu
before further inputs. Initial environment-script preparation failed because
JSON is a list of KEY=VALUE strings; corrected before any remote mutation.

Perf300 late diagnostic1200frames mean144.676ms,p95154.711,p99172.635,
max477.279; all>50ms. Stored late-diagnostic-summary.json. Broad observer
and catch-up ticks make this unsuitable as an ordinary performance regression.

Additional unqualified lead: generated A43D0 atA43DD expects180ADA to preserve
x87 depth; actual _CIfmod pops one. Guard safely falls back to memory lowering,
repeatedly reported by Pi. Existing HLE delta table knows crt_fmod=-1, but
this call is a guest CRT dispatch wrapper and inferred summary is guarded0.
Do not simply remove guard or apply HLE semantics to all guest error paths.
A call-effect hint retaining runtime guard may be worth an isolated correctness
and timing experiment, with all stack slots/NaNs preserved. Not implemented.

Launch47689 completed0 and halo-menu.png verified Campaign selected.
Controller47381 completed0: Cross(Campaign),Triangle(new profile),Cross
(default name),Cross(Normal), delays5/5/8/8s. fresh-start.png shows black
loading transition, not proof of loaded scene. Collector98838 still active;
latest status elapsed126s readyfalse. Next confirm b30 map/tag completion and
loaded/active director before selecting timing windows. Do not count menu or
load frames as gameplay or restart merely because loading is black.

## Perf301 evidence and inline profile attribution

Previous goal turn made progress: deployed qualified sparse observer. On next
continuation collector98838 was terminal143 (cause not supplied). Vita remains
responsive/perf301; no game restart inferred. Preserved original stream and
started collector52392 with stream2.py/new filenames,900s bound,lease renewal.
Loading-check.png showed loading; logs continued b30 reads131074KB and later
completed load. Landing.png confirms dropship view; beach.png confirms landed
first-person view with NPCs at distance. Neither verifies enemy firing.

Sparse reports retain root-pair~75240accepted/60frames. Owner FA920~110–122ms,
object900E0~89–103ms,AI14A162~16–20ms,120simulationcalls/60renderedframes.
At beach: helperCPU57.43ms,scenewall59.53ms,ownerwait0.01ms,
done->noticed62.77ms. Thus owner update is a major current limit; these are
nested/overlapping spans, not summed frame costs. No tick-count reduction.
Landing-window660Presentframes7441..8100 mean124.479ms(8.03FPS),p95142.824,
p99181.489,max187.498; all660>50ms,651>100ms. Sparse profiler still enabled,
not matched whole-combat comparison. Goal plainly unmet.

Enhanced tools/host_profile.py with opt-in --inline-owners: uses DWARF inline
ancestry at the sampled PC, attributes to outermost compiled routine, not an
inclusive call-stack estimate. Default leaf profile byte-for-byte matches the
previous b30 profile. Added help handling, checked symbolizer failures, stdin
addresses to avoid argument-size limits, and validates symbol output shape.
Private independent inline-profile.py produces matching owner counts/ranks.
Sampled LR report mostly invalid/clobbered: do not infer caller rankings from it.

Corrected attribution for Pi297 b30 owner25001samples: n4_86f501110(4.44%),
n4_87ea0800(3.20%),objectcollect637(2.55%),8DDF0569(2.28%),8BD50426(1.70%),
44AD0414(1.66%),49600408(1.63%). The earlier leaf9.1%guest-read is distributed
across containing functions, not a standalone function bottleneck. Collision
leaf/source samples cluster in record loads/span mapping/plane comparisons.
Native code already caches contiguous record pointers and center pointers;
do not duplicate that optimization. Consider alignment-qualified record-load
specialization only after assembly shows a benefit and differential tests keep
unaligned/page-crossing/alias paths correct. No collision change made yet.

## Private collision alignment experiment

Previous turn produced hardware evidence and improved sampled attribution.
Collector52392 confirmed live again; perf301 retained. Private experiment in
../collision-alignment-audit checks actual host alignment before marking a
contiguous collision-record pointer 4-byte aligned. Unaligned/page-crossing
records retain per-field accessors. No production collision source edited.
Vita O2 assembly:86F50 instructions4834->4839,vldr65->81,vmov80->63;
87EA03800->3849,vldr227->245,vmov58->37. Static counts do not prove speed.

Initial host test selected zero variants due to wrong CLI spelling; NOT a pass.
Fixed test_native_4b9d0.py to reject invalid variant names (checked exit2),
added explicit --build-only for cross differential binaries, requires --keep,
rejects replay/bench/mutants combinations. Correct host1000case seed1 test11369
is still live and must be polled. ARMbuild48748 completed0. Pi test9599 completed0:
100seed7cases,4guesttimeouts skipped,0mismatches,0verifyfailures;96casesqualified,
not all100. Other limits include existing NaN-payload allowances in fixture.
BaselineARMbuild12945 completed0. Started three alternating isolated Pi query
timing rounds(12fixtures x50repetitions,seed7,core2),original commands bounded
60sperprocess. Capture pi-timing.log; not VitaFPS evidence. No candidate deployed.

Pi timing3487 completed0:3rounds12fixtures x50reps. Those fixtures produced
zero surfaces, so insufficient for86F50; broadened sample9043 to128fixtures
x100reps, completed0. Baseline1644.9ns/query,3570cycles,4392instructions;
candidate1558.6ns,3443cycles,4453instructions. Same counters:63000nodes,
1400surfaces,8500edges/vertices. Single broader run suggests~5.2%elapsed
and~3.6%cycles reduction in this isolated query, not gameFPS or Vita proof.
Keep private candidate pending host1000case test11369 and additional coverage.
Timing baseline and candidate commands are terminal; no Pi workload left.
Collector52392 live on last poll; original900s bound approaching completion.
Do not restart a collector without checking handle; retain perf301/save state.

## Perf302 prepared, not deployed

Host11369 completed0:1000cases,26original-guest timeouts skipped,974checked,
0mismatches,0verifyfailures,0NaNpayload allowances used. Collector52392 also
completed0 at original900s bound. Not a15-minute active combat pass: includes
loading and stationary gameplay. Lease explicitly renewed3600s.
Last1200Presentframes11701..12900:128.306ms(7.794FPS),p95142.248,p99176.101,
max296.408,3>200ms; still sparse instrumentation. Full result settled-summary.

Started larger Pi1000seed1verify test37363, timeout600s,core2. Poll existing
handle before another Pi task. Prepared private collision-alignment-302 using
reflink copy of301,version302,only native4B9D0 alignment source change.
Build46663 completed0; audit94664 completed0: only native4B9D0+fourversion
objects changed; onlygame-a.self/bootmarkerchanged,updatercontractpreserved.
RuntimeSHA b85eca3bd6081923a9bf97ab6803a8a6fcc03b2133bee6b16b64b2ea567484ae;
packageSHA9770fae15086d7548ee3a9770206122f27097acd313eb455f68e1683ba9c04d6.
Not installed. Savebackup2553 completed0:12b30files,twohashreadsidentical,
file sets match. a30untouched. Need larger ARM result before deployment.
Production source native4B9D0 still untouched; candidate private. Keep known
301rollback and do not present isolated5%Piquery gain as whole-frameFPSgain.

## NaN qualification follow-up and AI observation

Pi candidate1000test37363 completed0:974checked,26guesttimeouts,0ordinary
mismatches,0verifyfailures BUT40NaNpayloadwords allowed by existing fixture
(and2012 verifier nan-words). Do not call this bit-exact. Baseline1000seed1
Pi test59380 started withsame600sbound/core2 to determine existing allowances.
Also preparing strict native-baseline vs native-candidate pair, using same
randomized fixture but replacing reference query execution with unchanged
native; disabling both context and arena NaN exemptions. Build65476 in progress.
Private build-exact-pair.py documents precise changes. Not deployed302.

Vita301 controller45694 completed0:12sforward,15sobserve. ai-approach.png and
ai-observe.png show nearby grunts idle/animating, no visible outgoing fire or
shield loss across those endpoints. Controller69520 completed0:2sARfire,
15sobserve. ai-after-fire.png shows ammo43(previous60), enemies no longer in
same visible spots and radarcontact. This verifies ammunition consumption and
visual change, not enemy firing or complete AI correctness. Controlsreleased.
Savesbackedbefore these inputs; no a30writes or progressclaim. Lease3600renewed.

Strict pair first build65476 failed duplicate xv_native_object_query_force;
renamed that baseline export too, rebuilt51946 completed0. Pi exact-pair test
started core0 under600s bound, separate from baseline reference test59380core2.
Native-vs-native run retains original guest fallback for declined cases but
removes arena and x87/SSE context NaN allowances. Query path only; no --verify
flag, which would compare against translated guest instead of baseline native.
Record terminal results before deploying. No production collision edit yet.

ExactpairPi handle82770 live task; baselinePi handle59380 live task. Both
bounded600s, core0/core2 respectively. Strict pair changes documented private
build-exact-pair.py; no reference runtime code merged. Need inspect terminal
and logs before installing302. Keep-awake still leased; currentVita301.

Read-only AI cross-check: retail staged14A162 reads pointer2FA248, byte+1
mapinitialized,byte0active; whenactive calls144B00,13EA40,14E2C0,thenwrites
byte+2hascontrol=1. Else calls14BFC0freezeifpriorcontrol. Structure resembles
reference ai_update without proving all version identities. Priorperf300
reports contained nonzero14E2C0→14E1A0,so AI was not simply globallydisabled
in those intervals. Observed failure to shoot requires deeper actor/target/
weapon investigation, not blindly flipping a guessed global byte.

BaselinePi59380 completed0 and matches candidate summary:974checked,
26guesttimeouts,40NaNpayloadwords,2012verifiernanwords,0ordinarymismatches.
This supports existing allowances but aggregate counts alone do not prove
same payload locations. Strictpair82770 confirmed live viaPID26357 at161s,
under original600s bound; keep polling originalhandle. No Pi restart needed.
Prepared b30-collision-302 launch/start/stream/environment,32samekeys as301
(including sparseTICK2), no remote launch yet. Lease3600renewed.

## Access restored

Network and authoritative-write permissions restored. Vita status perf301,
timing76345, priorlease0; renewed3600s successfully. Strict ARM oldhandle
missing, oldlocal logfileempty, remote process absent (ps only grep command).
Restarted only missing strict native-pair job as1996, N4_ALARM=1,1000seed1,
core0,180s bound. Remote collision-exact-pair-resumed.log/.rc now preserve
result independently of SSH. Do not claim original run passed. Savebackup
48267 running into save-after-access-restored, separate from previousreceipt.
Host strict974completed/26timeouts/zeroNaN-exemption mismatch result from
/tmp/xita-collision-followup/handoff.md retained as supporting evidence.

## User constraint: Vita-only testing

User now needs to take Pi and explicitly requires testing strictly on PS Vita.
Stop all new Pi and host performance/correctness test execution; use code review
and builds locally, validate behavior/performance on Vita. Sent SIGTERM only
matching our collision-exact-pair1000seed1 process; remote process listing shows
no remaining collision test. Do not launch Pi jobs. Preserve prior evidence;
unfinished strict ARM run is not a pass. Savebackup48267 completed0,12files
hash-stable across two reads. Hardware verification is next; no emulator tests.

Correction after polling originalhandle1996: test had already finished before
stop command. Terminal0, saved strict native-pair1000cases/26reference-timeouts/
974completed/0mismatches/0NaN exemptions. This is completed prior evidence;
no further Pi tests authorized. Stop found no remaining matching process.
Announced interruption; started302remote updater, savebackup complete. Poll
updater existinghandle; no duplicate update. All subsequent testing on Vita.

Integrated qualified alignment helper in production source with explanatory
comment; token comparison ignoring comments/whitespace matches staged302
candidate. No other collision logic change, no new tests outside Vita.
Updater5760 remains the current install handle; package34867646bytes.

Updater5760 completed0:34867646bytes, runtimeSHA b85eca3b...,verifiedtrue,
slot0,bootconfirmedtrue. Independentstatusperf302timing0/CPU444 confirms.
Lease3600renewedafterboot. Collector4594 and launch9716 nowactive; inspect
launch menu beforefreshprofile sequence. Preparedstart-fresh.py isnotyetcalled.
All subsequenttestingVita-only. Goalunmet,no302FPSresultyet.

## Perf302 physical-Vita follow-up

Vita-only constraint remains in force; no Pi or emulator tests. Previous turn
was a constraint acknowledgement, revalidated by polling collector4594 live.
Fresh launch reached b30 gameplay; screenshot b30-collision-302/gameplay.png
shows first-person beach and distant NPCs. Collector continues its original
900-second bound and renews the keep-awake lease.

Latest stationary 1200-frame sample (6301..7500): mean123.443ms,8.101FPS,
p95 138.435ms,p99 178.187ms,max443.230ms;1200/1200 over50ms,2over200ms.
Sparse profiling remains enabled. Prior301 separate stationary sample was
128.306ms; different intervals/scene state prevent attributing this difference
to alignment. No clear qualified total-frame gain. Tick report: object900E0
93.17ms,AI14A16217.74ms,outerFA920113.78ms per rendered frame;120 simulation
calls per60frames. Nested values overlap and must not be added as CPU time.
Physical gameplay approach script8121 started (12secforward,15secobserve),
with finally-release; inspect completion/screenshots before further controls.

Read-only next-target review:8BD50 still uses memory x87 lowering due join-depth
at8BDC0. Calls CRT180ADA at8BF20/8C083. x87_regs.py guess() only supplies0/+1
for unknown effects; wrapper180ADA has indirect CRT dispatch and an unproven
summary, while inner180AE4 normally consumes two x87 values and leaves one.
A guarded -1 hint might unlock conversion but must not be called proven or
remove fallback guards; exceptional paths need coverage. No such code change
has been applied or tested. Existing8BD50 owner samples were1.70%, so this is
an incremental target, not evidence of a route to20FPS alone.

Approach8121 completed0; inputs released. Hardware frames8056→8324advanced;
ai-observe.png shows nearby grunts, full-looking shields, no proof of outgoing
NPC shots. Movement works; AI combat remains unqualified. Collector4594 still
live at557s, frame8050timing, awakelease3540s. No further update deployed.

## Guarded x87 object-update candidate303

Implemented optional per-call-site expected deltas in recompiler/x87_regs.py
and CLI, preserving proven summaries and original mismatch continuation.
Only private target8BD50 selected. Corrected generator46468 completed0:
109x87 ops,9syncs,7guards,slots[0,1],converted1. Hints8BF20:-1,8C083:-1.
Initial generation20163 accidentally included8BEF1:-1 (different callee) and
converted0; discarded, never spliced/deployed. Corrected run uses only the
reviewed two remainder calls. Regeneration is compilation, not gameplay or
correctness execution. Basegenerator40751completed0. Splice matched original
stage body exactly and changed only8BD50 in private303stage. Build started;
no303deployment or FPSclaim.302 remains running with collector4594.

Collector4594 completed0 at900s. This includes loading and stationary time,
not15minutesactivecombat. Lease3600renewed after terminal. NearNPC latest
1200frames: {"frames": [10261, 11460], "count": 1200, "mean_ms": 100.94704666666667, "fps": 9.906183816373165, "p95_ms": 112.502, "p99_ms": 136.007, "max_ms": 447.305, "over50": 1200, "over200": 2, "limits": "After approach, stationary near NPCs; sparse profiling; enemy firing unverified; no matched comparison"}
Private303 build28710 stilllive; packageaudit prepared but notrun. No303deploy.

303 predeployment: build28710 remains live (cc1 compiling onlycode_013 at
fullcore, not hung). Static source-review.json verifies exactly8BD50changed,
seven stack guards, no extra function modifications. Backup21312completed0:
15b30save/profilefiles read twice withstablehashes andsamefileset. Existing
a30savepath untouched. Prepared b30-x87-303 launch/input/capture scripts,
sameenvironment as302; not launched before version303bootconfirmation.

303build28710completed0; packageaudit4845completed0. Changed objects exactly
code_013 plus four version-bearingobjects. Package preserves update contract
775a1863..., replacing onlygame-a.self andboot-game.txt. RuntimeSHA
e240087ee0754b8716b0caed9a9e5c6c1055f077ae1ec4050725161ca7c23549.
User interruption announced; remoteupdate48318 started, receipt install.log.
Do not duplicate update; poll48318. Need independent303bootconfirmation,
lease renewal and b30-x87-303 launch/capture. No303gameplay result yet.

303update48318completed0,34877494bytes,verifiedtrue,slot1,bootconfirmedtrue.
Independentstatus303timing0/CPU444;lease3600renewed. Collector83285live,
launch58359live (45second wait thenmenuscreenshot). Poll these exacthandles.
Do not invoke freshprofile macro untilmainmenu confirmed. OnlyVitatesting.
Candidate8BD50 machinecode size0x2A32→0x434C due guardedfallback body; this
is a tradeoff, notproof of speed. No303performance/correctnessqualificationyet.

Launch58359completed0. halo-menu.png visually confirmsCampaign selected;
started fresh-profile macro (cross/triangle/cross/cross, delays5/5/8/8).
Collector83285 continues same900sec bound. Need inspectmacroterminal and
map-loading progress, then settled b30 ordinarygameplay. No303FPSresultyet.

## Perf303 hardware first gameplay

Freshmacro27488completed0; collector83285 observed readiness and remainslive.
Landing.png shows scripted landing with rifle/world rendered. Before movement,
600frames8041..8640 mean121.680ms (8.218FPS),p95134.348,p99173.270,max204.602,
all600>50ms,one>200ms. Landing/beach interval, sparseprofiling, not matched
comparison: cannot attribute small differences to303. Still farfromtarget.
Approach9554completed0; nearbygruntsvisible, movementworks. Fire29856completed0,
ARammo60→46, actorsmove from view, inputsreleased. No observedcrash. Outgoing
enemyfire/audio/checkpointresume notverified. No claimfullcorrectness or15min
activecombat. No newtests onPi/host/emulator.

Next larger targets from existing broad300hardwaredata:8FB70→8DDF0 about24.8ms,
collision171F10→88110about16.6ms,92330selfabout6.2ms. These nestedinstrumented
figures are perturbed and not additive to current sparseframecost.8DDF0 already
has x87locals; native92330 implements its child lightquery, not fullwrapper.
Review these remainingcosts rather than re-enabling alreadyactive options.
Static collisionassembly inprivate303area retains53journalcall/relocation
references in87EA0 and25inquery: possible specialization target, not measured
runtime overhead or an implemented change. Query onlyclearsfourlistcounts,
not entire4KBresultarray; bulk-clear optimization would address nonexistentwork.

## Pose-call candidate304 (not deployed)

Current303capture83285completed0 at900sec; includes loading/stationary, not
15minactivecombat. Latest1200frames: {"frames": [11281, 12480], "samples": 1200, "mean_ms": 101.80244583333334, "fps": 9.822946706380295, "p95_ms": 110.495, "p99_ms": 125.512, "max_ms": 156.402, "over50": 1200, "over200": 0, "limits": "Post-movement/firing stationary interval; sparse profiling; different actor state vs302, not matched comparison."}

Reviewed8DDF0 current nativehierarchy/rootpair hooks and ownershipreports:
hierarchy has no snapshot/assist batches (idle ownership). Do not remove
pose copies before proving source lifetime across existing suspend/assist.
Concrete nextlead instead:8DDF0callsA43D0 at8E00D. A43D0's firstremainder call
A43DD incorrectly expects unchangedx87stack (fsp+6) onnormalpath, sending
actual fsp+7 tosafe memoryfallback. Priorretainedlogs reported thissite often.
303logger absence inremote capture doesnot prove0misses.

304generation32041completed0 with onlyA43D0 selected, A43DD:-1 guardedhint.
ExistingstageA43D0 exactlymatched retainednohintregeneration before replacement.
OnlyA43D0 changed from303stage;54x87ops,12syncs,5guards,logicalslots[0,2].
Originalmemoryfallback retained; noarithmetic/tick/policychanges. Builds on303,
keeps8BD50 optimization. Privatepose-call-hint-304/source-review.json receipts.
Build58642live; audit-package.py prepared butnotrun. No304deploymentorhardware
qualification. Lease3600 renewed thisturn. Need poll58642, audit, savebackup,
announceupdate and verifyboot before nextVitatest. NoPi/emulator/hosttests.

304build58642completed0; audit47762completed0. Onlycode_015 plusfourversion
objects changed. Backup9047completed0 (18stableb30files, twohashmatchingreads).
Updater95282completed0:34877366bytes,runtimeSHA4153195ca7afb8bc6c8d0597690660aa1b0f488a88a7562b212121e6a93cd21b,
verifiedtrue,slot0,bootconfirmedtrue. Independentstatus304timing0/CPU444.
Lease3600renewed. b30-pose-304 collectorandlaunchstarted, sameenv/inputs as303.
No304performanceorcorrectnessresultyet. NoPi/host/emulator execution.

304launch67807completed0; menuscreenshotconfirmsCampaign selected. Started
freshprofilemacro. Collector54858 live, same900sec bound/60sec lease renewals.
Need inspectmacroterminal, loadingprogress, andgameplaybeforemeasurements.
No304FPSclaim. Earlier objectjobsreadiness messages are just eligibility;
hierarchyownership idle counters do notprove whole-systemworkers areunused.

304freshmacro6142completed0. Collector54858observedready; loadingI/Oadvanced
through219620KBcachewrites beforegameplay. No restart. Stillneedsettledsample.

Read-only compiler expansion: inventory42function/sitepairs,34distinctdirect
180ADAcallinstructions,26overlappingfunctionentries. Census54526completed0,
onlytheseentriesselected,density3,allcall-siteexpectations-1withguards.
21converted,13newlyconvertible versus retainedoriginalregeneration.
Includes173F20 waveformhelper calledby8BD50; no broadcandidate spliced/built/
deployed. Private remainder-census-summary.json recordsremainingdensitydeclines.
Differentdiscoveryentries mayoverlap,so countsarenot distinctgameplayroutines.
SupportingretainedPiprofiletoplist didnotranktheseadditionalhelpers; no new
Piwork and no aggregateperformanceclaim. Nextdeploymentmust followreview,
code-sizecheck,andcurrent304Vitaresults,notautomaticbulkpromotion.

304gameplay.png confirms first-person beach, HUD/rifle/world visible. Initial
600frames6661..7260 mean124.637ms (8.023FPS),p95134.784,p99293.158,max624.685;
all600over50ms. Window includes landing; do not attribute its stalls or tiny
FPSdifference to304. Sparseobjectupdate94.36ms/AI18.19ms,120ticks/60frames.
No clear totalframegain. Approach43246started,12secforward/15secobserve,
finallyreleasesinputs. Collector54858 remainsoriginalactivecapture.
Approach43246completed0; ai-observe screenshot shows nearby grunts and updated
camera position; no visible healthloss/enemyshots proven. No observedcrash.
Inputsreleased.304ARfire scriptprepared, notexecutedthisturn. Currentcollector
54858stilllive, no restart. Broaderremaindercensus not deployed.

## Pose diagnostic305 preparation

304ARfire83821completed0; ammo60→45, inputsreleased, noobservedcrash.
Enemyfire/audio/checkpointresume remainunverified. Collector54858 stilllive.

Privatepose-profile-305 retains304runtimebehavior and instruments8DDF0 direct
calls via existingpatch_scene_phase_timers --parents0008DDF0 --any-call.
40static callsites acrossregister/fallback bodies; source comparison after
removingobserverlines exactlymatches304. Existing sparseowner selector gains
only8DDF0 and sixanimationchildren A4CE0,A1E40,A43D0,A4720,90770,A38E0.
Matrixleaf clocks remainexcluded: their native/fusedwork is insidepose remainder.
This is diagnostic, notoptimization; elapsedremainder isnotCPUself and observer
cost canchangeframe scheduling. Rootpair/math/nativepolicies retained.
No productionselectorchange; modificationsonlyinprivate305stage.

Build75389live; audit-package.pyprepared. Backupstartedwithseparatefreshreceipt.
Need completebuild/audit/backup then announceupdate.305notdeployed.

304collector54858completed0. Final1200frame summary:{"frames": [9841, 11040], "stats": {"windows": 20, "samples": 1200, "unavailable": 0, "mean_ms": 100.69401083333334, "fps": 9.931077248031956, "p50_ms": 98.655, "p95_ms": 110.281, "p99_ms": 156.547, "max_ms": 440.961, "over_50ms": 1195, "over_100ms": 457, "over_200ms": 8}, "incomplete_windows": 0, "limits": "Post-approach/firing stationary scene; sparse profiling; not matched combat or 15-minute active stability acceptance."}
305backup21922completed0;savefilesstabletwohashmatchingreads. Build75389
remainslive;no305deploy. Futurestatisticsuseexistingtools/frame_times.py,
whichvalidatescompletechunksandunavailableintervals.

305build75389completed0; audit88403completed0. Changed objects exactlycode_013,
xk_scene_thread,andfourversionobjects. Saved21b30files stableacrosstworeads.
RuntimeSHAafd1a525ff0edd21a6d2e7a7106bec9eb0157f6b3ff261753dfdfa0792f66e0a;
updatecontractunchanged,onlygame-a.self/boot-game.txtreplaced. Announcedrestart;
updater44988live,install.logpreserved. b30-pose-profile-305scriptsready,same
runtimeenv. Needbootconfirmandleasebeforelaunch. DiagnosticnotcleanFPSbaseline.

305update44988completed0,34882606bytes,verifiedtrue,slot1,bootconfirmedtrue.
Independent305statusCPU444,timing0;lease3600renewed. Collectorandlaunchstarted
inb30-pose-profile-305. No performanceconclusion; needmenuthenfreshmacro.
305launch51107completed0,Campaignmenuvisuallyconfirmed. Collector11934live.
Startedfreshprofilemacro; pollhandlebeforefurthercontrols. Needloadingcompletion
andsettledposeparent>childreports. Do notcountmenuorloadingFPS.

## Perf305 pose attribution and perf306 verification candidate

The fresh-profile macro (70848) completed successfully. Collector 11934 reached
ordinary b30 gameplay and remains live. The early pose report was 36.42 ms,
with 25.87 ms outside the selected child scopes. Later reports settled nearer
27.75 ms: 90770 6.11 ms, A4CE0 2.74 ms, A1E40 1.23 ms, A38E0 0.11 ms,
A43D0 0.09 ms, remainder 17.46 ms. These are nested, instrumented elapsed
times, not exclusive CPU costs. They show A43D0 is a small contributor here;
do not promote the broader remainder-call census as a likely large gain.

The serial hierarchy path currently builds all local matrices, then composes
all outputs. A private perf306 candidate computes one local matrix and composes
it immediately, keeping the validated parent-before-child order and identical
arithmetic expressions. Assisted execution retains its existing path.

Perf306 is verification-only, based on perf304 (without the extra pose timers).
It always publishes the existing two-pass result. The first 32 serial batches
and every 64th thereafter per thread also run the alternative on private data.
Successful batches compare every produced matrix byte and the complete final
FPSCR. Failed batches compare the failed work-item index; the caller discards
all output and restores entry FPSCR on failure. No NaN tolerance is used.
The original result and original final FP state remain authoritative.

Private files: hierarchy-serial-306/build-x87/recomp/kernel/xk_hierarchy.c and
qualification-scope.json. No production hierarchy edit, no off-Vita execution,
no equivalence or speed claim yet. Build 86180 is live; package audit prepared
but not run. Perf305 remains installed. Inspect its settled data before update.
Perf306 build 86180 completed successfully. Package audit 73884 completed
successfully: only hierarchy object and four version objects changed from304;
package preserves contract and changes only game-a.self / boot-game.txt.
Fresh save backup 54018 is running (save-copy.log). No306 update yet.
Collector11934 remains the active305 capture. Next poll backup, capture status,
and assess settled pose rows before installing the verification-only build.

Perf305 collector11934 completed its original900-second duration, exit0.
Latest settled pose reports remain approximately28.6ms inclusive/17.9ms
remainder, not proven CPU self time or all hierarchy work. Complete frame
reports ending8700..9900:1260 intervals, mean128.084ms,7.807FPS,
p95136.167ms,p99289.154ms,max590.101ms;22 over200ms. This is a stationary
instrumented scene, not a matched optimization comparison.
Perf306 save backup54018 completed:24 files stable across two reads,
same file set. Package SHA matches the audited receipt. Deployment initiated
only after305 capture completion. Prepared b30-hierarchy-306 launch/capture
scripts retain the32 settings and collect hierarchy-serial-verify rows.
No Pi or emulator execution. User's physical-Vita-only testing rule applies.
Perf306 updater20699 completed exit0:34881934 executable bytes, SHA
650f67288095320f7861984098f0a5f362f1939706e8569af6093555d7a83b6b,
verifiedtrue, slot0, boot_confirmedtrue. Independent status confirms306,
timing_frame0; renewed3600-second lease. Launch73150 started;900-second
hardware collector started separately. Candidate results remain unpublished;
no performance/equivalence conclusion until captured checks are inspected.
Perf306 launch73150 exit0; captured Halo Campaign menu visually inspected.
Fresh-start59919 is live, using the existing four-button sequence and isolated
b30 save namespace. Collector77013 is live, original900-second job; renews
lease every60 seconds. Early menu verifier rows checked0 are not evidence of
arithmetic equivalence. Next inspect fresh-start result and await b30 gameplay
before aggregating nonzero verification checks or issuing movement/fire.
Perf306 fresh-start59919 completed exit0. Loading progressed into b30 gameplay
without restarting. Collector77013 remains live; at elapsed261.7sec the first
940 sampled hierarchy batches matched all output bits and full FPSCR, zero
failed-output samples. This is early coverage, not universal equivalence or
speed proof. landing.png visually shows the scripted Pelican arrival, so no
movement/fire yet. summarize-verification.py aggregates all retained report
counters; no host game execution. Next await landing and exercise gameplay.
Perf306 movement6777 and fire56682 both completed exit0. Pre-movement image
shows landed beach; approach image shows nearby Grunts; post-fire ammo60->45.
No crash observed during this short sequence, but enemy firing/audio not
verified and this is not a15-minute active stability qualification.
At111 reports7689 sampled hierarchy batches match exactly; zero mismatches
and zero failed-output checks. Original900-second collector77013 remains live.
Private hierarchy-serial-306/xk_hierarchy.fused.c and serial-fused.patch now
prepare the fast-path-only successor from304: identical sampled fused function,
serial branches only; assisted execution unchanged; no verifier counters or
extra FPSCR operations. Not built/deployed or integrated into source yet.
Next finish306 capture and assess coverage, then build/time this successor.
Prepared private hierarchy-fused-307 from306 stage, restoring304 hierarchy
plus the serial-fused-only diff. Version307, same build flags, unchanged
assisted path, no verification counters/FPSCR overhead. Build95947 is live;
package audit script retains304 asset contract and exact changed-object gate.
At130 reports306 sampled11066 batches with zero mismatches/failed samples.
Fresh b30 save backup started under307; its I/O may disturb306 timings, which
are verification-only and must not be advertised as optimized performance.
No source integration, off-Vita tests, commits or pushes.
Perf307 build95947 and audit65597 completed exit0. Exact changed objects:
hierarchy plus four version-bearing objects; package changes only game-a.self
and boot-game.txt, unchanged launcher contract. RuntimeSHA
3e34a631aa5121885fffd508c7ec331c4ee826db15867e237c5fe1d0846cc2b2;
packageSHA afe395281fcaa9219585c4a9675fc90314306c04464ad09f9aa21bec03c5e449.
Not deployed. Backup43923 remains live.306 collector77013 confirmed live,
elapsed657sec of900;12845 sampled batches, zero mismatches, zero failure
samples. Preserve this original capture until terminal; do not restart it.
Perf307 save backup43923 completed exit0:27 files, stable two reads/same set.
Prepared b30-fused-307 launch/input/capture scripts, same32 environment values.
Perf306 motion-check65926 exit0:two short opposing camera turns and strafes,
controls released, final screenshot intact. At165 reports17309 sampled
batches matched, zero mismatches/failed-output samples; collector still live.
Static follow-up:90770 is a93-line object-type callback dispatcher, indirect
call at907B2 using type table1FCB78 and callback+44. Its~6ms inclusive cost
cannot be assigned to dispatcher overhead; profile its target children before
attempting replacement. Existing aim-blend native remains enabled.
Perf306 collector77013 completed original900seconds, exit0:179 reports,
19827 sampled batches, zero mismatches, zero failed-output checks. Includes
landing, approach, AR firing and short camera/strafe sequence; mostly stationary
capture, not15-minute active qualification. All sampled success outputs and
full FPSCR matched. No proof of exceptional/failure equivalence from runtime
because none sampled; static failure path discards outputs/restoresFP retained.
Announced interruption and initiated307 upload after capture finished. Backup
and package audits passed. Await updater handle before launch; no timing gain
claimed.306 remains rollback candidate with original-result publication.
Perf307 updater65887 completed exit0, verifiedtrue, slot1, boot_confirmedtrue;
independent status307 timing_frame0. Receipt updated, lease renewed by launch.
Launch72370 completed exit0; Campaign-menu screenshot visually checked.
900-second collector97224 is live; fresh-start sequence initiated, handle below.
No performance result yet; use settled scene-qualified frames, not loading or
menu samples. Existing rollback/perf304 and verifier306 artifacts retained.
Perf307 fresh-start31908 completed exit0. Collector97224 confirmed live;
elapsed221sec, frame5132/timing4293, loading still active (cache writes131076KB,
b30 reads98306KB), lease3570sec. No gameplay timing conclusion from loading.
Retain original collector job. Next await readiness/landing, mark a stationary
window, then approach/fire using307 scripts and analyze complete frame reports.
Perf307 landing-check.png during loaded1/director0 transition showed corrupted
colors; arrival-check.png after director readiness shows intact landed beach.
Do not classify transient as307-caused without prior-build matched evidence.
Preselected stationary report window6540..7080 completed600intervals:
mean123.039ms (8.128FPS),p50118.557,p95134.374,p99281.249,max606.947;
all600>50ms,588>100ms,11>200ms. Prior304 beach124.637ms had different
landing stalls; this modest difference does not establish meaningful gain.
Live tick reportFA920113.66ms includes900E094.73ms and14A16215.95ms;
nested/waits included, notCPUself. No20FPS success. Approach script started;
collector97224 remains live, elapsed502sec. Next fire, inspect and finish capture.
Perf307 approach13162 and fire86958 terminal0. Nearby Grunts visible before
firing, ammo60->46 afterward. No observed crash in short sequence; enemy return
fire/audio still unverified. Post-fire1200-frame window selected in
b30-fused-307/post-fire-window.json; originalcollector97224 still live.
Collision detail node counters read0 because n4_detail is disabled unless
verification/timing enabled; calls alone do not establish traversal size or
cost. Native object querymode2 confirmed. Avoid interpreting omitted counters
as absent work. Next finish post-fire window and ordinary capture before
changing profiling/optimization. Current native source retains journal checks
inside noinline recursive functions, a potential specialization target whose
cost still needs hardware attribution. Do not weaken write ordering/aliasing.
Perf307 post-fire preselected8760..9900 completed1200frames:99.282ms mean,
10.072FPS,p5097.578,p95108.982,p99134.709,max259.094;1199>50ms,
313>100ms,4>200ms. Prior304 post-fire100.694ms/9.931FPS, not matched
actor state and not a clear meaningful improvement. Keep qualification open.
Reference review corrects earlier name:90770 is orientation PREprocessing,
not matrix postprocessing. Its callback offset44 precedes matrix generation;
2342 object_types.h puts datum_preprocess_node_orientations at that offset,
postprocess next. Prior source comment in xk_native_aim_blend also misnames it.
Reference unit_preprocess_node_orientations includes replacement/overlay,
emotion/mouth/aiming/looking animation; biped callback chiefly validates vectors.
Current native-aim-blend reports6774/6774 and6728/6728 native calls, zero
compressed/other declines. Thus extending compressed handling is not the
current scene's target, and native aim blend is already present. Larger
refactor must remove retained guest-state bookkeeping across a proven call
boundary, not duplicate this existing native or skip animation updates.
No upstream implementation copied. Collector97224 still live; poll to finish.
User now explicitly authorizes using the upstream source-based engine for Vita.
Created private detached worktree native-source-vita-001 at upstream3012128;
port/vita/README.md and prototype.json record bring-up/acceptance gates.
No runtime replacement/deployment yet. ARM32 ABI and GXM bridge need actual
adaptation;2342 engine data requirement differs from inspected2276 maps.
Do not promise FPS from source availability alone. All runtime testingVita-only.
Perf307 collector97224 completed900seconds exit0; retained build on hardware.
Renewed3600second awake lease this turn. Source-based prototype is the new
implementation direction toward the same sustained20FPS/correctness goal.
Native-source prototype now has reproducible port/vita/compile_engine.py using
clang arm-vita-eabi, VitaSDK sysroot, Cortex-A9 hardfloat/NEON Thumb, signedchar,
16-bit wchar, strict FP contraction off. Actual upstream real_math.c and
integer_math.c compile exit0 to ARM32 ELF objects. Vita prefix resolves C-library
enum collisions/POSIX name conflicts; minimal source change lets fast_ftol use
existing builtin-rint path for HALO_VITA, no x86 architecture disguise.
port/vita/include/float.h contains declarations only; FPSCR implementation is
still required. No executable linked, runtime correctness/FPS not established.
No host/Pi/emulator game execution. Build logs/receipt under prototype/build/vita.
Native-source expanded compile57453 completed0:all9 math C units plus
model_animations.c and objects.c compile into ARM32 objects. Vita compatibility
prefix handles engine/system-name collisions, MSVC prototype-scope forward
types,16-bit wchar/signedchar and atomic external-name collisions. Matrix C
implementation enabled for HALO_VITA instead of x86 inline assembly. No no-op
atomics or FP control implementations supplied. Engine still unlinked; ABI
layout and runtime correctness unproven. Prototype diff-check passes.
Next expand engine compilation then implement/link the required Vita services.
Native-source full-engine compilation census: first pass82458 terminal1,
383/467C entries compiled. Second94960 terminal1,435/467 compiled after
upstream MSVC tag/weak-inline declaration generator, xbox define (release,
DEBUG intentionally omitted), and Vita libc allocator declarations forlibtiff.
32remaining files include x86asm, script aggregate-return callback ABI,
ARM va_list assumptions, pointer signatures, rasterizer prototype disagreement,
and absent linker_common.c. All errors retained in per-unit logs and timestamped
receipts; engine-build-2-summary.json is authoritative. No linking/runtime
claim from435compile successes; sharedplatform/GXM integration stillrequired.
No off-Vita execution. Lease renewed3600sec at turnstart. Next tackle compiler
ABI issues with actual adapters, not warning suppression or no-op substitutes.

## 2026-09-28 — Native-port experiment cancelled by user

The user explicitly cancelled the separate native-source Vita port after the long
sequence of standalone iterations without a running game. Stop development,
builds and deployment of native-source-vita-001. Preserve its files as reference;
this is not authorization to delete work. v77 only compiled/linked/packaged; it
has not rendered Halo on hardware or demonstrated a performance gain. Earlier
Next steps in native device-integration notes are superseded.

Resume performance work in the existing source checkout and Xita runtime. The
20 FPS physical-hardware goal remains unmet, with Silent Cartographer (b30) the
user-selected baseline. Last verified installed gameplay build is perf307; do
not claim current device state without reconnecting. Preserve saves and rollback.
Prioritize measured NPC/world/effects preparation and submission costs in the
running implementation. No new native replacement port is authorized by this
continuation. No device package was changed by this cancellation.

## Return to existing runtime: fresh evidence, 2026-09-28

User reauthorized Raspberry Pi support and using reference repositories for
existing-runtime improvements; separate native port remains cancelled. Pi SSH
confirmed raspberry, uptime 10h24, idle load ~0.1. Vita status freshly confirms
perf307, reported CPU444, frame331481. Awake lease renewed3600seconds.
Fresh bounded 200000-byte log tail retained in return-to-xita-20260928/perf307-tail.log.
Full log is123570687bytes; stopped redundant full pull after obtaining bounded
tail, partial /tmp/xita-return-perf307.log is not a complete capture.
Tick FA920107–111ms, object phase900E085–88ms,14A16216–19ms, all inclusive
elapsed and not additive to concurrent renderer. Render helper latest70.64ms
CPU,76.50ms wall; owner join3.37ms, done-to-noticed42.54ms. This selects object
update subtree attribution ahead of further graphics bridge work. It does not
prove collision is the dominant child. Alternating object-job readiness/mask80
reports require reading admission logic; not proof workers are permanently off.
No deployment, configuration change, save operation or FPS gain this turn.
Next inspect sampled object child/lock attribution and admission policy, then
qualify a targeted replacement with the Pi oracle and real Vita gameplay.

### Object-worker gate and Pi test recovery

Inspected both authoritative source and perf307 build-stage xd3d.c: strict
viewport lag==1 resets stable_frames to zero on the second simulation tick
(lag0). Repeated alternation can prevent the90-frame admission requirement
from ever completing. This is an existing deliberate default, justified by an
older perf144 cinematic regression, not a newly introduced bug. Tolerant mode
accepts lag0/1 but does not bypass map/camera/stability gates. Fresh xita.cfg has
no tolerance override; remote process environment is not fully established.
No worker-pass/lock reports found in captured prefix or recent bounded tail.
Do not attribute the current85–88ms to worker mutex waits without new evidence.

Pi source-predicate/readiness tests passed including stabilization, alternating
lag, all reset gates, duplicate ticks and wrap. Current worker fixture was stale:
missing host owner-alias and optional HLE-timing symbols; parser expected old
two-slot contention report. Repaired fixture with real pthread_self identity,
disabled diagnostic timing with fail-fast unexpected callback, and current
three-slot report parsing asserting unused slot zero.
ARM32 Thumb/ARMv7-A hardfloat NEON static worker suite passed on physical Pi:
0/1/2 workers, profiling off/on, bounded waits off/on; ownership/600 callbacks,
native hierarchy transforms, quiescent resource/audio service, overflow joins,
reset accounting and forbidden callback/yield rejection. This is synthetic
worker infrastructure coverage, not Halo shared-state or FPS qualification.
Private Pi directory ~/xita-object-audit-20260928; logs retained beside fresh
hardware tail. No processes remain from these tests.
Next: assess a controlled tolerant-admission gameplay trial on current build
with save protection and actual pass counts, retaining rollback if slower or
incorrect. Do not remove the gate unconditionally or promise a multicore gain.

## Tolerant object gate physical trial started

object-gate-308-trial is an experiment directory, NOT a perf308 build. Still
perf307. All30 isolated b30 save/profile files backed up with matching two reads
and directory sets in save-before/receipt.json. Companion no-sleep enabled.
First quit/launch produced no remote; old log stopped and subsequent targeted
quit confirmed title absent. Fresh launch restored perf307 dashboard frame13
timing0. No freeze attributed: freeze.txt dated20260926 is stale.
Reapplied b30-fused-307/environment.json plus OBJECT_JOBS_VP_TOLERANT=1 and
EXPERIMENTAL_OBJECT_JOBS=1. Both acknowledged in new log. Gameplay graphics
and other optimizations unchanged. Lease3600renewed. Halo main menu screenshot
verified after launch5186 completed0. Standard fresh b30 controller62904 running;
collector10131 running240seconds with lease renewal, logs in trial directory.
Poll these handles before any further control. No worker-admission/FPS result yet.
Controller62904 completed0; screenshot at frame1930 is black with active overlay during loading, not established gameplay. Collector10131 still live on last poll. Do not use loading FPS as result; await director/gameplay readiness.

### Object gate trial rejected; original settings restored

First collector10131 completed240sec exit0 during load. Continuation58446
started from exact prior byte count into gameplay-stream.log, observed gameplay
readiness and worker initialization (whole callbacks core0/1). First worker batch
stalled: owner-wait4..32sec, math_holder200 (other thread), service words0/0;
no completed display frame (screen HTTP504). Light-mutex owner query returns
FFFFFFFF/count-1/rcFFFFFFFF, so it DOES NOT identify the actual holding thread.
No completed worker performance window, no FPS gain. Do not enable tolerant
gate by default. Saved partial log; explicitly interrupted collector exit130.

Targeted quit then5-second delay/relaunch restored dashboard, freshly confirmed
perf307 timing_frame0. Original b30 environment restored plus explicit
XV_OBJECT_JOBS_VP_TOLERANT=0; lease3600. restore-status.json retained. No package
change, no unverified save restore, no collector/input job left running.

Next synchronization target: establish actual non-worker lock holder/callsite
and pending scene proxy/recorder state at batch stall. Existing owner_blocked
service already calls scene proxy and must not be duplicated blindly. Do not
remove mutual exclusion or simply yield guest fibers (known snapshot hazards).
Pi synthetic fixture lacks concurrent scene-helper integration; passing it did
not cover this hardware failure. Add a matching dependency test before a fix.

### Concurrent scene-holder fixture on Pi

Extended tools/tests/object_jobs.c with opt-in OBJECT_SCENE_WAIT_TEST: a real
separate pthread waits until a worker batch starts, acquires the production
math guard, and holds it until the blocked owner services a synthetic proxy
request. Worker callbacks wait for acquisition before continuing. Atomic
release/acquire handshakes, exactly-one owner-service assertion and helper
join verify completion. Test runner exercises two workers with timed waits0/1
and a30-second process bound. Existing infrastructure/rejection matrix also ran.
ARM32 Thumb ARMv7-A hardfloat NEON static suite passed on physical Pi, including
both new scene-holder cases; log pi-scene-worker-test.log retained.

This confirms the existing owner-blocked service mechanism works for this
synthetic request. It does NOT reproduce the Vita stall or prove the actual
scene proxy/streaming/resource dependency safe. No production synchronization
change made. Next capture real lock-holder thread/callsite plus scene pending
HLE/yield state; avoid duplicating existing proxy service as a purported fix.
Vita keep-awake lease renewed; strict-gate dashboard retained, no game restart.

### Opt-in real-holder diagnostics prepared, not deployed

XV_OBJECT_GUARD_TRACE=1 captures actual native thread identity and outer return
address for non-worker acquisitions of the production guard. Recursive depth
is maintained under that guard; atomically published thread/callsite is read
only by existing4-second stall report. Worker slow-path release is excluded
from foreign-depth tracking. Last caller is stale when published thread is0;
fields independently sampled, not a coherent ownership proof during changes.
Tracing defaults off. No scheduling, ordering, unlock or recovery change.
Scene snapshot hook reports enabled/in-flight/proxy-fiber/pending/function and
helper-done without following mutable guest pointers or invoking watchdog.

Physical Pi ARM32 synthetic worker suite including both concurrent-scene wait
cases passed with tracing enabled (pi-guard-trace-test.log). Modified object
jobs and scene-thread modules cross-compiled with VitaSDK, __vita__, scene
and object workers enabled, exit0 with no compiler output; object files and
logs retained privately. This is module compilation, not full package or
hardware qualification. Full perf307-stage build/package and deployment still
required to capture the actual holder. Native-port experiment remains stopped.
Vita remains restored perf307 dashboard; lease renewed. No running test jobs.

## perf308 foreign-owner diagnostic package

Private guard-owner-308/build-x87 cloned retained307 stage. Only object-jobs
and scene-thread source diagnostics plus version308 applied. Full build62013
completed0. audit-package8584 completed0: exactly these two modules plus four
version objects differ; only game-a.self and boot-game.txt change in retained
307 VPK. Contract unchanged. RuntimeSHA02f41a67352ce8805fc5fc5cf652d3e59ae093f9115cd8350239cc19d63cf00d;
packageSHA44cb15f65cdb402d6c218eb09a0a6bcaca7a26b533c2558159a1a154dc0a3399.
Remote status confirmed307 dashboard timing0 before deployment. Announced
update; updater55653 uploading/applying. Poll before further action; not yet
installed proof. Diagnostic launch/scripts prepared, not run. No performance
change claim. Previous30-file test-save backup retained; dashboard only since
last recovery, no guest save writes requested. Native-source port still stopped.

Perf308 updater55653 completed0: 34882398-byte runtime verifiedSHA
02f41a67352ce8805fc5fc5cf652d3e59ae093f9115cd8350239cc19d63cf00d,
slot0 boot_confirmed. Fresh status308 dashboard timing0. Package receipt updated.
Diagnostic launch65353 completed0, Halo menu screenshot verified. b30 input
controller71224 running; collector3784 live600seconds with lease renewal.
Both under guard-owner-308. Poll these handles before further inputs.
Runtime settings are prior b30 plus tolerant1, experimentaljobs1, guardtrace1.
Diagnostic reproduction, not FPS qualification. symbolize-holder.py prepared
against exact308 ELF/printed runtime anchor, subtracts Thumb bit and2bytes to
resolve return caller. No holder record yet at launch time.

### perf308 confirms scene helper owns 325C0 guard

Controller71224 completed0; collector3784 observed gameplay then matching stall.
Repeated trace: native holder4007017F equals scene-helper ID; outer caller
814744A7, exact308 ELF adjustment resolves0x813FB4A4 inside f_000325C0.
holder-symbol.txt retains result. No proxy fiber, pending0, fnNULL. Snapshot
reports helper-done1: do not infer active proxy deadlock or lock leak solely
from this timestamp-based predicate; reconcile it with lock scope next.
No ABANDON/guest-trap line observed. First batch never completed.

Retained generated325C0 has whole-function math guard; cache-ready reads at
3262A/32642 loop via32685->12AA3 (yield), with optional282B0 after elapsed
time. This matches earlier perf134 cache-wait hazard but present helper-done
contradiction requires checking dispatch/retirement and actual helper stack.
No blind unlock, scheduler yield, or disabling guard implemented.

Collector3784 deliberatelySIGINT stopped130 after capture; targeted quit,
5sec wait then launch15153 completed0. Fresh308 dashboard timing0 confirmed.
Restored original b30 environment, strict viewport gate0 and trace0; lease3600.
restore-status.json retained. Perf308 remains installed (diagnostics dormant),
perf307 rollback slot retained. No performance gain; no live input/capture jobs.
Next investigate why325C0 can retain foreign guard despite helper-done and
introduce a dependency-safe boundary rather than re-enable gate permanently.

### 325C0 return and helper-done audit
Exact308 objdump shows325C0 cleanup call to xv_object_math_unlock at813FBB26
followed by census cleanup and return. No logged abandon supports claiming a
longjmp lock leak. helper_done reads two64-bit timestamps (LDRD loads onARM32),
not the done semaphore. Timestamp comparison alone is insufficient to resolve
current holder state; do not assert a torn read caused the hang.
Source now extends opt-in stall snapshot with raw dispatch/end microseconds,
serial/depth/guestESP and Vita kernel helper status/waittype/waitID. No guest
stack dereference, recovery, scheduling or synchronization changed. Vita module
compile prepared /tmp/xk-scene-stall-clock.o; full package not yet rebuilt.
Next use raw state to distinguish live325C0 cache wait from retained lock after
completion, rather than assuming oldperf134 deadlock or removing guard.

## perf309 raw helper-state diagnostic

Private guard-state-309 stage cloned308, updates only scene-thread diagnostic
and version. Full build11376 completed0; audit67184 passed: scene-thread plus
four version objects changed, package only game-a.self/boot-game.txt, same
asset contract. RuntimeSHA85be1651bfc0e37daf753cf17fef10f9a3eaf72ec9ecd69b4c6ca8c6d4fb29df;
packageSHAd9275c5d78734764fcd000ca664a84dfa109a7e5c2d804ae64747ac4c625df3c.
Confirmed308 dashboard timing0 before applying. Announced updater71676 now
live; poll it before launch. Prior save backup preserved, no guest gameplay
since last restoration. Scripts prepared for309 with same guarded trial settings.
Goal remains unmet; this is failure diagnosis, not performance improvement.

Perf309 updater71676 completed0, verified34882682bytes runtimeSHA above,
slot1 boot_confirmed. Receipt updated. Launch53449 completed0, version309
confirmed and main menu screenshot inspected. Standard b30 controller26446
running; collector23609 running600sec with keep-awake renewal. Private outputs
guard-state-309. Poll these before further actions. Tolerant1 and guardtrace1
trial only; restore strict0/trace0 after failure evidence. No FPS result yet.

### perf309 resolves completed-helper/held-guard state

Controller26446 completed0. Collector23609 reached gameplay and captured repeated
holder4007019D (helper) caller8140E51F -> exactELF813FB51C f_000325C0.
Raw dispatch342662228us/end345170996us: completion2508768us later. Helper
kernel status8(waiting),waittype32(semaphore),waitID40070199; no pending proxy,
no proxy fiber, helper-done1. Thus completed-helper indication is consistent
with kernel state, not just speculative torn timestamps. Guard remains held
after completion. Duration strongly implicates2500ms timeout longjmp, whose
existing catch explicitly skips C scope cleanup. No ABANDON line retained,
so describe timeout attribution as strongly supported inference, not direct log.

Next fix needs scoped non-worker guard unwinding on abandonment AND prevention
of cache-read starvation when object workers overlap rendering. Consider draining
previous scene before worker snapshot/admission (owner can yield safely there),
retaining parallel object workers within the batch; measure total frame tradeoff.
Never release another thread's mutex or merely skip bitmap readiness.
Collector stopped deliberately; no success/FPS qualification. Dashboard restoration
80334 running after targeted quit/5sec delayedlaunch. Poll handle and confirm
restore-status.json before further input. No new fix implemented yet.


### Worker admission scene-retirement boundary (not deployed)

Previous continuation only rechecked Pi availability; no performance progress
claimed. Implemented xv_object_jobs_begin boundary in authoritative source:
retire previous scene with xv_scene_thread_join_owner before initializing/
publishing batch ownership; an admitting flag rejects reentrant admission while
that join yields. Recheck configured override, owner, phase and gameplay readiness
on return. Active-batch waits remain unchanged; no arbitrary guest scheduling
inside workers and no foreign mutex unlock added.

Fixture now tests nested admission rejection and readiness, phase, and override
invalidation during the join. Full production worker harness passed locally
(/tmp/xita-admission-tests.log) and physical Pi ARM32 Thumb ARMv7-A NEON static
with guard tracing (/tmp/xita-admission-pi.log, process68027 exited0).
Vita object-jobs module compiled /tmp/xk-object-admission.o. These tests establish
admission ordering/revalidation and existing synthetic worker behavior, not full
Halo streaming correctness or FPS. No package/deployment yet; hardware still309.
Vita keep-awake lease renewed successfully. Previous restore80334 completed0
(as confirmed before this continuation); safe strict gate and trace off restored.

Next: audit scene-abandon scope unwinding (longjmp skips math/census cleanups),
then build a candidate and retest the actual streaming/worker overlap on Vita.
Preventing the triggering overlap alone does not repair an already retained
mutex or establish timeout cleanup correctness. Goal remains unmet.


### Same-thread scene guard abandonment cleanup (source candidate only)

Previous turn was progress: admission boundary implemented/tested. This turn
found all three scene longjmp sites (guest trap and Vita/host timeout) and added
weak xv_object_math_abandon_current calls immediately before jumping. Production
non-worker recursive guard depth/actual thread identity is now tracked even with
guard trace off. Cleanup returns without touching a foreign holder, releases
only current native thread's recorded recursive acquisitions, and iterates over
a captured count (never reads shared depth after the final mutex release).
Normal scope cleanup is unchanged. Worker tokens/private scopes are not unwound
by this helper. Callers must immediately discard the old stack/tokens.

Census owner identity now uses actual sceKernelGetThreadId/pthread_self, matching
its native-owner requirement: the render helper's guest-owner alias must not
increment the owner's census scope count. Worker-specific pose/solver scopes
already require a real worker lane and do not admit a scene helper.

Synthetic scene test now forces nested real guards through longjmp, verifies
another thread cannot abandon the holder's locks, and completes subsequent
worker callbacks. Full suite passed host /tmp/xita-abandon-tests.log and physical
Pi ARM32 /tmp/xita-abandon-pi.log (45089 exited0), both trace0/trace1 cases.
Vita object-jobs and scene-thread module compiles passed. This does not prove
all game/render state is recoverable after arbitrary guest traps; existing scene
abandonment can leave partially written guest state. No Vita package/deploy yet,
no FPS result. Keep-awake renewed. Next build candidate from309 with admission,
guard cleanup and census identity changes, audit artifact changes, then test
actual b30 overlap. Preserve strict-gate rollback and normal saves.


### perf310 build and update in progress

Previous turn progressed cleanup implementation/tests. Private worker-retire-310
cloned retained309 stage (independent files); only object_jobs.c/.h and
scene_thread.c plus version changed. Full build15786 completed0; package audit
53118 passed: exactly object-jobs, scene-thread and four version objects changed;
package only game-a.self/boot-game.txt, asset contract unchanged.
RuntimeSHA852e221776c680b2e50367a63c21a56048bbc0826e2a0fd330e2a12ff577c9cf;
packageSHAe903c91c176d1abb0431f8d6423c27b52e40aa4cebfffc214a4b2316cdbcdce2.
Vita confirmed309 dashboard timing0 before update. Announced deployment;
updater19300 live, uploading/applying; poll before any launch. Scripts use310,
same isolated b30 save and prior trial environment. No hardware qualification yet.

Perf310 updater19300 completed0; runtime34883130bytes SHA matched, slot0
boot_confirmed, fresh status310 dashboard timing0. Receipt marked deployed.
Launch48354 completed0 and menu screenshot verified. Controller38156 completed0;
fresh-start screenshot black loading with live overlay. Collector19887 remains
live (latest poll30sec running), captures worker-retire-310/b30-stream.log and
status, renews lease every60sec. No active gameplay yet at latest check; loaded0
active0, no worker pass report yet. Do not restart based on an observation timeout;
poll collector and inspect loading progress. No FPS claim or success yet.


### perf310 hardware: batch stall cleared, whole-object workers too costly

Previous turn progressed build/deploy. Collector19887 reached loaded1 active1
and director1 after long loading/cutscene; no restart during loading. Seven
worker reports: first partial20 passes then six120-pass windows; all completed,
no STUCK/ABANDON retained. Full windows roughly39600–69880jobs/60frames,
batch elapsed126–151ms/frame, substantial shared-lock waiting. Gameplay totals
214–237ms/frame (~4.2–4.7FPS), not20FPS. Scene owner joins37–66ms/frame in
sampled windows. Screenshot /tmp/perf310-gameplay.png shows beach gameplay and
active workers. worker-result.json retains exact reports. No matched scene
comparison; do not assign precise regression delta or claim timeout recovery
hardware-tested (no timeout occurred). Immediate previous stall no longer occurs.

Capture19887 intentionallySIGINT130 after~300sec, evidence preserved. Restoring
fresh dashboard with original b30 environment + tolerant0/trace0 using targeted
quit,5sec delay,launch: process61479 live; poll terminal/status before more input.
Installed310 remains candidate with safety fixes, whole-object experiment gate
restored once receipt succeeds. Next bottleneck: high shared transaction cost;
XV_OBJECT_HOLDS=1 can attribute sampled hold owners in ordinary gameplay, no
benchmark toggle loop. Do not retain slower whole-object worker configuration
as a performance win. Need narrower independent jobs/native work to offset the
lost render overlap, not just higher worker utilization. Goal unmet.

Restore61479 completed0; restore-status.json confirms310 dashboard timing0. Strict gate and guard trace off, lease3600 renewed. No live collector/controller remains.


### perf310 hold attribution ordinary-gameplay run started

Previous turn progressed: observed completed batches but poor hardware FPS and
restored dashboard. New private worker-holds-310 reuses installed310 without
rebuild, same trial environment plus XV_OBJECT_HOLDS=1 and guardtrace0.
Launch46391 completed0; menu screenshot verified. Controller51901 completed0.
Collector81808 confirmed live, still loading at last status (~58sec, loaded0).
This collector renews lease every60sec and has600sec bound. Poll same handle.

Important interpretation: hold sampler disables query unlock and other private
lock-release paths. This run ranks held routines only, not a speed comparison.
Prior310 had query-unlock enabled and actually releasing~2902 calls/60frames;
do not rediscover/enabling that as a new optimization. query-world-run counter0
is not proof missing native math: its admission requires retained math_depth,
whereas the existing world query runs unlocked. Earlier source/measurement
notes already identify4C980->4B9D0->49600->172BF0->171F10->88110. Use current
sample to confirm/change ranking, then reference collision source to seek new
work. Existing summarize_object_holds.py and symbolize_vita_hold_sites.py can
analyze with worker-retire-310/build-x87/build/xita.elf; samples1/64, inclusive.
Goal unmet, diagnostic run not qualified performance settings.


### perf310 sampled hold evidence shifts priority to56670

Previous turn started valid diagnostic capture; this turn obtained actual hold
samples. worker-holds-310/hold-summary.json and hold-sites.json retain three
windows, ELF310 relocation0x36000, all14sites resolved/return-call verified.
56670 sampled124858us vs4C98069032us. Movement child4B9D061735us;
8811030415us nested, almost all world route30051us vs object364us.
Do not sum nested scopes or treat sample durations as CPU self/FPS savings.
Hold mode disables private releases; this ranks retained transactions only.

Read production310 generated56670/code_010.c. Actual worker query symbol comes
from xk_cluster_runtime.c (XV_TYPED_CLUSTER_QUERY), NOT old journal adapter
xk_worker_query.c. Original310 non-hold log confirms typed query thousands of
applies, private-calculations and source validations (e.g11835checks/26447176
comparedbytes per60frames), so native collection/unlock already active. Cannot
claim an absent native rewrite as solution.56670 also allocates/publishes shared
cluster linked-list records after the typed prefix returns; next isolate capture,
calculation, validation/replay and remaining list-publication costs before editing.
Reference structures.c describes cluster sphere flood; collision_bsp.c describes
separate88110 sphere feature collection. Do not confuse56670 with texture cache
wait325C0 due earlier notes' ambiguous cache-query description.

Collector81808 deliberatelySIGINT130, evidence preserved. Restore script
worker-holds-310/restore.py targeted quit then5sec launch, original environment
plus tolerant0/guardtrace0/holds0; process90485 running, poll to completion.
No new package, no FPS gain. Goal unmet; native-source separate port stays parked.

Restore90485 completed0; fresh310 dashboard timing0 confirmed, strict gate/holds/trace off, lease renewed. No live capture/controller. Sampled total shares56670 48.5%,4C980 26.8%,hierarchy5.2%; diagnostic ranking only.


### Typed-query stage cost sampler implemented, not deployed

Previous turn progress: sampled lock ranking, restored dashboard. Added opt-in
XV_TYPED_QUERY_COST initialized at owner-only cluster_runtime_begin. Every64th
admitted lane query measures capture, owned calculation, lock reacquisition,
validation and publication. Per-lane cleanup records declined/bypassed/applied
outcome even on early return, existing joined report drains totals. No original
admission/locking/publish checks removed, no geometry copying added; hold mode
must stay OFF for next capture. Timings include scheduling, not CPU self.
Capture excludes pre-lane admission; this does not time56670's shared linked-list
tail, only typed prefix. Need surrounding attribution for that remainder.

Files source/recomp/kernel/xk_cluster_runtime.c, tools/test_cluster_runtime.py,
tools/tests/worker_query.c (missing real-thread fixture alias supplied). Fixture
checks stage outcome sums and zero-sample zero-time; disabled mode never starts
runtime and correctly has no cost rows. Initial systempython lacksiced-x86;
used retained privatevenv. First build failed missing fixture alias, fixed.
HostASan/UBSan then passed10modes normal,disabled,alias,mutation,parking,
concurrent,source,inflight,budget(expected stop),overlap. Logs/private generated
reference in typed-cost-test-311; compilation command retainedcommand.json.
Final mode sweep reran builtbinary after parserdisabled correction, allpassed.
Normal336exactcomparisons/62private-readyvisits. Vita module compile passed
/tmp/xk-cluster-cost.o. No full311build/package, no hardware measurement yet.
Pi object-audit tree lackscluster runtime source; need sync dependency files or
use existing fullcluster harness before ARM validation. Vita remains310dashboard,
previous safe environment, lease renewed. No live test processes. Goal unmet.


### perf311 ARM qualification, build and deployment

Previous turn implemented/tested sampler. Synced isolated Pi~/xita-typed-cost-311
with exact reference/production dependencies (not existing object-only tree).
ARM32ThumbARMv7-AhardfloatNEONstatic binary passed all10fixture modes, including
expectedbudget stop and outcome bookkeeping;69753terminal0. Local
/tmp/xita-typed-cost-pi.log and typed-cost-test-311/pi-normal.log retained.
Remote pi-run.py and per-mode logs retained; no Pi jobs remain. Not VitaFPS.

Private typed-cost-311 stage cloned310, only cluster_runtime.c/version changed.
Build51326completed0; audit59750passed exact cluster-runtime+fourversionobjects;
onlygame-a.self/boot-game.txt assetdelta, samecontract.
RuntimeSHA49b0af11e90a4e88ee522812961834312f06265f53a2e5d0f91f56b852ede7e5;
packageSHAee3844b7e96e79cae2273a0b42e376273eadba0a036498ba97d6472d64b89ea9.
FreshVita310dashboardtiming0confirmed, lease renewed. Announced update;
updater67032live uploading/applying; pollsamehandle before launch. Launch/stream/
start-fresh scripts use311 and sameb30isolatedsave. Environment: workertrial on,
guardtrace0,holds0,typedquerycost1. No newFPSclaim or gameplayqualification.

Updater67032completed0,34883758bytesSHAverified,slot1bootconfirmed. Launch awaiting menu; need poll launch handle before controller.

Launch75762completed0, menu visuallyconfirmed311. Collector97123live600sec and renewslease60sec; controller40263livecross/triangle/cross/cross. Poll both before further controls. Paths typed-cost-311. No hardwaretimingresultyet.


### perf311 hardware isolates typed-query validation cost

Previous turn progressed ARM validation/deploy. Controller40263completed0;
collector97123 observed readiness and stage samples, no restart while loading.
Added source tools/summarize_typed_query_cost.py: complete nonempty lane0/lane1
pairs, verifies outcome/sample consistency; no extrapolated FPS. Checked it on
Pi fixture log then actual hardware. typed-cost-311/stage-summary.json covers
three windows569samples(535applied34declined): capture8.58us/sample4.15%,
compute17.93us8.67%,reacquire20.80us10.05%,validate154.67us74.77%,publish4.86us
2.35%. These are overlapping sampled elapsed times, not CPU self/frame savings.
Sampler leaves query-unlock/private-release behavior intact (holds0).

Source_current iterates EVERY linked SourceRead and evaluates dependency bits
before validating relevant bytes; current geometry has~824records. Candidate:
precompute a bounded transposed cluster->read-ID bitset plus global row and
ordered SourceRead* array at snapshot construction. For each result combine
startcluster+changedclusters+global read IDs, then visit set bits in ORIGINAL
list order with identical pointer and byte checks. Never reuse validity across
mutations, never skip global/unknown dependencies; full batch validation stays
full. Preserve scalar fallback for allocation/index unavailable; release index
with snapshot. Need tests for shared dependencies, changed relevant/global
bytes, start cluster inclusion, page remaps, duplicate IDs, zero/last IDs and
allocation fallback. No candidate code implemented yet. This specifically
reduces searching the validation set, not the safety checks themselves.

Collector97123 deliberateSIGINT130; evidence preserved. Restoring311dashboard
originalenvironment+tolerant0/holds0/guardtrace0/typedcost0 via restore.py.
Process51384live, poll/status to completion. No performancegain claimed, goal
unmet. Need implementation/correctness validation then hardware result.

Restore51384completed0;311dashboardtiming0confirmed, strict gate and all three tracing options off, lease renewed. No live collector/controller.


### Candidate312: exact indexed validation implemented and qualified

Previous turn hardware evidence identified validation74.77% sampled adapter time.
Source now adds xk_cluster_read_index.h, bounded8192read IDs x257rows (256
clusters+unconditional), allocated once per snapshot. Unknown/explicit-global
reads in globalrow; shared dependencies OR sameID. Query selects global+start+
changedclusters, enumeratesIDs in original linked-listorder. All pointer/equality
checks unchanged; full batch reuse checks remain scalar/allrecords. Index/order
freed withsnapshot. Allocation failure usesoriginalscalarloop; env
XV_TYPED_SOURCE_INDEX=0 disables, default1. Newtyped-read-index counters show
uptake/examinedrecords/retainedbytes without pretending reducedFPS.

Standalone tools/tests/cluster_read_index.c differential selection vs scalar
predicate passed30size/cluster combinations (1..8192records,1..256clusters),40
querieseach,shared/repeated IDs,unknown/global,start andboundarybits,allocation
failure andinvalidcapacity. Passed hostASan/UBSan andphysicalPiARM32.
Full worker runtime fixture passed20hostASan/UBSan runs(index0/1 x10modes),
includingmutation,alias,source,inflight,parking,concurrency andexpectedbudgetstop.
read-index-test-312 contains command/build/per-mode logs. Test uses retained
private typed-cost-test-311/worker-reference.c. Piisolated~/xita-read-index-312
passed10index-enabledARM32ThumbARMv7-AhardfloatNEONstatic modes, log
/tmp/xita-read-index-pi.log. Tests verify no game behavior divergence, not FPS.
Vitamodulecompile/tmp/xk-read-index.o passed. No312package/build/deploymentyet.
No liveprocesses; hardware311dashboard,safe settings,lease renewed.

Next clone typed-cost-311stage,copyonlycluster_runtime.c andnewreadindexheader,
version312,fullbuild+audit(runtimecluster+fourversionobjects),deploy,testsameb30
withtypedcost1holds0workertrial1. Compare actual validationstage andtotalframe
cost, not syntheticexamined-recordcounts. Preserve310/311evidence/rollback.


### perf312 build/audit, updater active

Previous turn implemented/tested indexedvalidation. Private read-index-312 cloned
311stage, copiesonlycluster_runtime.c+newcluster_read_index.h/version312. Full
build11682completed0; audit62640passedexactcluster-runtime+fourversionobjects;
packageonlygame-a.self/boot-game.txt, sameassetcontract.
RuntimeSHAd8a4beaa9ee791662f56ffc7f706d69d3f1b11f2300706f3b86cfd821b3a7aaf;
packageSHAfbb422c18ded7928bb6583c8706554ece8fec2aa3950affb029cdd6e0f1974ab.
Vita311dashboardtiming0confirmed,lease renewed. Announceddeployment;updater34515
live uploading/applying, pollsamehandle beforelaunch. Allscriptsuse312, restore
scriptretainsstrictgate/tracingoff. Trialenvironment311+typedsourceindex1,holds0,
typedcost1. No hardwareperformanceclaim yet; target20FPS stillunmet.

Updater34515completed0,34884302bytesSHAverified,slot0bootconfirmed. Launch62260live45sec menuwait withlease renewed; poll beforecontroller.

Launch62260completed0, menuvisuallyconfirmed. Collector52507live600sec+lease60sec, controller83238live standardfreshb30macro. Pollboth beforemoreinput. Pathsread-index-312. No312hardwaretimingresultyet.


### perf312 hardware indexed-validation result

Previous turn progressed build/deploy. Controller83238completed0, collector52507
reachedready. typed-read-index confirms10176–12720indexedqueries/report,30024
retainedindexbytes; noSTUCK/ABANDONobserved. stage-summary.json:3windows589samples
(544applied45declined),capture8.22us,compute19.11,reacquire15.61,validate28.44,
publish4.23. Earlier3113window569samplevalidation154.67us; large~82%reduction
in sampledvalidation; workloads/schedulingnotidentical, no exactFPSprediction.
Recenttotalframe190–207ms(~4.8–5.3FPS), objectbatch~107–123ms/frame. Remains
slowerthanearlierstrict-gate gameplay; do NOT promote whole-object-workertrial
as20FPSsolution. Keepindexedlookupcode/tests; safetyselectionpreserved. Index
memory30KB, notnewperqueryallocation. worker-result.json retainsexactreports.

Collector52507 intentionallySIGINT130; originalcapturepreserved. Restore17003
live via read-index-312/restore.py targetedquit/delayedlaunch, safeoriginalb30
settings+tolerant0,alltracingoff. Poll completion/status before furtherinputs.
Nextneedreduce remainingupdatework/lostrenderoverlap; avoidingthesevalidation
scans is useful but cannotfixwholecallbackdependency architecturealone. Current
nativequerycalculation~19us while fullobjectbatch>100ms. Read reference cluster
link/listpublication56670tail and ownerhotpaths before another optimization;
no blindremovalofchecks or arbitraryguest-scheduler yields. Goalunmet.

Restore17003completed0,312dashboardtiming0confirmed, lease renewed. No livecollector/controller; safeoriginalenvironment restored.


### User direction: reference reuse and Pi availability

The standalone native-source port remains cancelled. Preserve its code as a
reference for targeted improvements to the existing Xita runtime. The user has
made the Raspberry Pi available again for ARM correctness and concurrency tests.
Physical Vita measurements remain the authority for performance; Pi test results
do not establish Vita FPS gains. Continue with cluster-list publication work,
with equivalence checks before hardware deployment.


### Test-only native 566DE publication continuation

Goal continuation: previous turn made progress by verifying Pi reachability and
recording updated user direction. This turn confirmed typed-worker successes
jump past the native92330 hook to 566DE and run the translated publication tail.
Refactored native query into n9_query_at(s, tail_only), keeping original wrapper
and production hook behavior. XV_NATIVE_566DE_TEST exposes continuation with
entry ESP reconstructed as current ESP+0x88. No production tail hook or deploy.
Added --tail to tools/test_native_92330.py: extracted private guest prefix runs
in both modes; only label566DE dispatches native for mode2. Existing differential
fixture compares results and guest memory; verify/flood/mutants disallowed here.
Private native-tail-313-test holds generated sources/build receipts.
Test command running as exec session3388, log /tmp/xita-tail-test.log: 3000cases
per configuration. First plain-O2 passed 0mismatches,2262flooded,2764linked,
122stack-alias scenes;5originalguesttimeouts skipped. Remaining configurations
still running at last poll. Poll same handle; no restart on observation timeout.
Next inspect complete results, then ARM/Pi continuation equivalence and existing
whole-function regression before considering production hook. Need actual
worker integration/locking and whole-frame gain before promoting. git diff
--check passed. No Vita update; keep-awake lease3600 command completed0.


### Native-tail host validation complete; Pi ARM validation running

Previous continuation made progress (test-only candidate and differential mode).
Read objective again. Session3388 completed0: all three host configurations
plainO2/thread-table+render-viewO2/plainO0 passed3000cases each,0mismatches,
5originalguesttimeouts skipped per configuration. Existing whole-query path
regression session40992 completed0:600cases per same3configs,verify enabled,
0mismatches,0verifyfailures,0timeouts;7journalcapacity fallbacks per config
(their native result is covered by differential mode, not journal verification).
Logs /tmp/xita-tail-test.log and /tmp/xita-tail-regression.log.
Pi test deployed to isolated ~/xita-native-tail-313 using ARM32ThumbARMv7-A
hardfloatNEON static executable. Local private native-tail-313-pi contains
source/header copies and exact command.json/run.py. Session55965 stilllive
at final poll, logfile /tmp/xita-tail-pi.log not yet populated (captured
output). Poll same handle before any rerun. No other test/build live.
Lock audit: xk_cluster_runtime resumes object query guard before validation
and publish;56670 outer cleanup guard spans list tail; each native datum
allocation retains recursive lock/unlock. Future continuation hook must run
after XV_QUERY_WORK_END and preserve outer guard. Do not release shared-list
transaction lock. No production hook or deployment yet;perf312 unchanged.


### Native publication integration prepared, hardware unchanged

Previous turn progressed host regressions and startedPi. Read objective. Pi
session55965 completed0:3000ARM32cases,0mismatches,5guesttimeouts skipped.
Integrated xv_native_566de in xk_native_92330.c, mode2 only, no scenehelper;
restores E from ESP+0x88, native tail, budget, counts, separate tails counter.
Existing test-only wrapper now invokes this production entry. Generator
games/halo_ce_3925/hooks.py calls it only after successful typed-worker prefix
and XV_QUERY_WORK_END, preserving outer transactionguard/cleanup; mode0/1
fallback goes to original566DE. Stage patcher now upgrades old worker-success
branch even when wholequeryhook exists; checked real312body/idempotence.
No stage updated or deployed yet.
Added --native-tail workerfixture option (native92330 source+default2). Session
41548 completed0:realpool ASan/UBSan normal/disabled/alias/mutation/parking/
concurrent/source/inflight/budget/overlap passed. Native-tail-313-worker private
reference+commands/logs, /tmp/xita-tail-worker.log. Added native report to fixture
and assertion of nonzero tails in normal/concurrent/overlap to rule out vacuity.
Coverage rerun session58958 LIVE, /tmp/xita-tail-worker-coverage.log. Pollsame.
Next run this integrated workerfixture onPi (previousPi test was isolatedtail),
check census compatibility, compileVita, stage313build+audit+deploy onlywhen
qualified. NoFPSclaim. 312stillinstalled. gitdiffcheck/pycompile passed before
lastcoverageassertion edits.

Coverage session58958 completed0; normal/concurrent/overlap all confirmed nonzero native-tail calls. See native-tail-313-worker/*-coverage.log for counts.


### perf313 integrated validation passed; full Vita build running

Previous goal turn progressed integration+host tests. Read objective. Pi
worker runtime session28821 completed0:all10modes passed including native
uptake assertion normal/concurrent/overlap. Private Pi~/xita-tail-worker-313
cloned priorread-index workspace,updated native/fixture/generatedreference;
local native-tail-313-worker/pi-worker-run.py records exactARM32command.
/tmp/xita-tail-pi-worker.log and remote typed-cost-test-311/tail-*.log.
Hostcensus session90229 completed0:all10ASan/UBSanmodes passed, exact prefix
counts/backedges maintained. /tmp/xita-tail-census.log andprivate
native-tail-313-census/*.log.
Staged native-tail-313/build-x87 is reflinkclone312,only copied native92330.c
and patched worker-success branch within generatedcode010,version313.
Fullbuild exec86146 LIVE,build-x87.log. Pollsame before audit.
Prepared audit-package.py expects ONLY native92330/code010+fourversion
objects changed, replacesgame-a.self/boot-game.txt in312cpackage,unchanged
assetcontract,outputs xita-perf313c.vpk. Not yet run; no package verified.
Launch/startfresh/stream/restore scripts copied withversion313 andsame312
environment. Not launched. Hardwareprestatus confirms312dashboardtiming0;
lease3600renewed. No deployment yet. pycompile+diffcheck passed.
Next pollbuild86146, auditpackage, announce/deploy,verifyversion/bootconfirm,
then normalb30trial with tailcounts andwholeframetiming; restorestrictgate
afterexperimentalworkertrial if stillslower. Target remainsunmet.


### perf313 audited; remote update in progress

Read objective; previous turn progressed integratedARM/census validation and
fullbuild. Build86146 completed0. Audit1171 completed0 with exactly code010,
native92330,andfourversionobjects changed. Onlygame-a.self/boot-game.txt
updated;assetcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.
RuntimeSHA c64c0b1b246336e0832812c3963a680365569bf1a856f0a01ba8c55c924a8164;
packageSHA e7bc3b86eadad27e5edf7fa3e51423fff87c196d3d736638fadb9e0be0236118.
Verified code010 compilerflagsenable native92330/typedquery/objectguard.
Announceddeployment. Updater exec42758 LIVE;native-tail-313/update-result.json
containsuploadprogress(currently23MB of34.9MB). Pollsamehandle toterminal
andverify bootconfirmation/version beforelaunch. Nothingelsecurrentlylive.
Hardwaretrial scripts prepared sameenvironment312; isolateb30testsaves.
Do not inferFPSgainfromaudit. Goalunmet.


### perf313 installed; b30 gameplay capture started

Readobjective;previousturnprogressedbuildaudit/deployment. Updater42758
completed0:34884974bytes runtimeSHAverified,slot1bootconfirmed. Package
receiptmarkeddeployedonlyaftermatchingupdaterSHA. Launch66481completed0,
Halo mainmenuvisuallyconfirmed. Perf313version assertion passed atlaunch.
Collector10394 LIVE (600seconds,keepawakeeach60sec), controller3779 LIVE
standardfreshb30macro. Pollboth;do notsendextra inputuntilcontrollerdone.
Private native-tail-313/{b30-stream.log,b30-stream-status.json,start-result.json}.
Loadingmaytake150seconds;use logadvancement+readiness,notblackscreenalone.
No FPSresultyet. Afterreadycollect settledframes,native92330 tailscounts,
objectbatchcost;preservefullcapture. Restorestrictgatewithrestore.py after
trial ifwholeworkerpathremainsworse. Allnormal a30savesprotected viaisolated
testsaveprefix. Nootherprocesseslive.


### perf313 loading verified in progress

Readobjective. Previousturnprogressed installedboot+capture. Controller3779
completed0 and releasedpad. Collector10394 re-polled live; elapsed158.7sec,
offset1469904,readyfalse. LoadI/O advanced from32MB mapread to163844KB cache
write;this is verifiedloadingprogress,not aterminal stall. No extra controls
or restarts sent. Continue samecollector;readyframe hasnotyetbeenobserved.
Native tail counters0sofar areloadingframes and prove nothingaboutgameplay.
Source audit:batch geometry snapshot isalreadyreused withfullvalidation,not
rebuilteverybatch. 312lastwindows1–13builds/60frames (2.3–31msTOTAL),notmain
framecost. Workerwaits2.8s/2.2s per60frames overlap batchwall;4C980movement
callback nextlargestknownhold(26.8%) after56670. Do notsumoverlappingtimers.
No sourceedits or otherliveprocesses thisturn; hardware313loading b30 test.


### perf313 hardware result: native tail active, whole-worker path still slow

Readobjective;previous turnverifiedloadingwait. Collector10394observedready,
controlleralreadycompleted. Screenshot b30-gameplay.png confirms loaded
openingpelican view. Native92330 reports12118/12134tails per60frames;
pathactuallyactive. Earlyworkerwindows189.3/190.6ms, later198.6/201.8/208.5ms
(~4.8–5.3FPS). Objectbatch120passes/60frames,121–125ms/frame early;
lockwaits~2.68–2.75s and2.49s per60frames(overlap). NoSTUCK/ABANDON.
worker-result.json preservesrecentrawreports. Notmatchedcameracomparison,
no meaningfulwholeframegain demonstrated. This doesnotjustifyenabling
whole-object-workersbydefault.
Collector10394 intentionallySIGINT130,fullcapturepreserved. Announcedrestore,
restore23896 startedtargetedquit/delayedlaunch;poll beforeanynewinput.
Retainnativecode/tests as qualifiedcontinuation but stop treatingwholeworker
path as a demonstratedFPSoptimization. Returnto strictrender-overlapbaseline
andtargetactual remainingserialupdate/collision cost. Knownchain
4C980->4B9D0->49600->172BF0->171F10->88110. Don't repeat alreadyenabled
native4B9D0/BSPsphere work or broadphase timers thatdisturbcatchupticks.
Goalunmet.

Restore23896completed0,313dashboardtiming0confirmed,safestrictgate/tracingoff,lease renewed. No livecollector/controller/build.


### Return to strict baseline: focused native-collision evidence

Readobjective;previous goal turnprogressed hardware rejection ofwholeworker
experiment andsaferecovery. Reference physics/collision_bsp.c confirms
accumulatedvertex/edge/surface/ancestorplane first-match searches. Existing
native4B9D0already covers88110and864C0; don'tclaim anewnative rewrite.
Potential scalarsearchcost requirescounts. Privatecollision-detail-313
scan-audit.c/.s extracts exact n4_find withsamewordload/page translation,
VitaSDK O2ThumbCortexA9NEON shows oneLDRperword (not4byte loads), so
alignment-only scanoptimization is ruledout. No productionchange.
Preparedcollision-detail-313 withsafe307environment+strictgate,holdtraceoff,
typedcostoff,XV_NATIVE_4B9D0_TIME=1. This enablesexistingnativeelapsed/counters
withoutbroadnewsubcalltimers; overheadnotassumedzero. Need settledb30sample
scans/scanned/nodes/surfaces andus/callto rank SIMDsearchvsothercollisionwork.
Launch75858completed0,313mainmenuvisuallyconfirmed. Collector60421 LIVE
600sec boundedwithleaseevery60sec;controller75242 LIVEstandardfreshmacro.
Pollbothbeforenewinput;loadingtypically~180sec. Existingnative-tail-313
evidencepreserved. Currentrunnotnewbuildandnotautomatedon/offbenchmark.
Usecollision-detail-313/restore.pyaftercaptureto disable native timing too: it
currentlyrestores307env butdoesnotexplicitlyclearXV_NATIVE_4B9D0_TIME, so
add that overridebeforeexecuting restore. Nootherliveprocesses.

Correctedcollision-detail-313/restore.py nowexplicitlyclearsXV_NATIVE_4B9D0_TIME=0.


### Native collision summary ready; focused capture still loading

Readobjective;previousgoalturnprogressed source/assemblyaudit andnewcapture.
Controller75242completed0,padreleased. Collector60421re-polledlive;180seconds
offset1656548readyfalse. CacheI/Oprogress219620KBwritten,then15353KBread,
notastall. Keep samecollector.
Added tools/summarize_native_collision.py: selects complete timednative-only
reports followingloaded/active/directoron, last3windows perquery/features,
calls/frame,weighted us/call,aggregate elapsedms/frame,counts/frame,
elements/search. Rejectverificationmode;ignoreincompletelines/menureports.
Arithmetic/readiness/incomplete/verified-rowchecks passedinline. Timers
exclude budget/counteroverhead,includeinterruptions;differentquery/feature
windows not automaticallyadditive. No hardwaretimingresultyet.
Next waitready+settledwindows,runsummaryoncollision-detail-313/b30-stream.log,
inspectactualcostandcounts beforeSIMDcandidate. Preservecapture andrestore
scriptclearsnativeTIME. No otherliveprocess or pendingbuild.

Collector60421thenobservedgameplayreadiness. Early3window180frame summary
collision-summary-early.json:query55.64calls/frame,8.502aggregatems/frame;
features64.1calls/frame,0.665ms/frame;1228searches/4721elementsperframe,
mean3.84elements/search. ThisarguesAGAINST prioritizingSIMDsearch: short
searches andlimitedtotalquerybudget. Let laterwindowssettle;next inspect
remainingowner-phasecost ratherthan spendingbuildon speculativevectorization.
Capture60421stilllive.


### Settled strict-baseline critical path confirmed

Readobjective. Prior goalturn progressedsummary+earlymeasurement;status-only
userreply made no newtechnicalclaim ofsuccess. Collector60421completed0
at600sec beforeattemptedCtrlC;normalcompletion,not130. Preserved
collision-detail-313/critical-path-summary.json andcollision-summary.json.
Final3windowsquery17.5225ms/frame128.106calls,features1.012ms/frame95.122calls,
searchmean4.208elements. Earlier settledwindows18.78msquery;initial8.5ms
waslighteropeningview,notwholelevelcost. Latestframe121–128ms(~7.8–8.2FPS),
tickFA920~115–117ms,object900E0~96–98ms,AI14A162~16ms (120ticks/60frames).
HelperCPU~72ms withscenewall~76–81ms;overlapsowner,notadditive. Ownerjoin
~1.6–6.4ms,recorddrain~1.5–1.9ms,nofullqueuewaits. C2~93–97%steady.
Thereforeownerupdateiscurrentcriticalpath,notrecordqueuewaitorsearchalignment.
Nativecollision20msceilingalonecannotreach50msframe. Broadworkersworsenit.
Source audit:900E0 perobject8FB70 callsaretimedbutfilteredbysparsemode;
finaltail8ECA0 completelyunmeasuredseparately(tailcall). 8ECA0 has915generated
lines,iteration/indirectcallbacks;90950is105lineindirectdispatchwrapper.
Nextpartition900E0 intooncepertick lifecycle/8ECA0 tailvs perobject update
withouttimingthousandsofsubcalls. Existing sparsemode3 selects6outerlabels
only (XV_TICK_PHASES=2). Consider bounded sampled8FB70andexplicit8ECA0
entry/exit attribution, preservetailABI/ordering;don'treuse broadtimers that
changedcatchupticks. Referenceobjects/objects.c objects_update at4176 and
object_update3607 helps mapresponsibilities. No speculativeSIMDchange.
Restore53599LIVE lastpoll:quit/delayedlaunchcompleted,awaitstatusconfirmation.
It clearsnative4B9D0_TIME andrestoresstrictgate/tracingoff,lease. Nootherlivejob.
Goalunmet.

Restore53599 completed0; focused collision timing disabled and strictgate restored. User reaffirmed engine restructuring as priority. Added docs/engine-update-restructuring.md with evidence, reference decomposition, and next limited attribution task. No active processes.


## Perf314: final object collection ruled out

Implemented one additional sparse owner scope, 900E0>8ECA0. The production
selector admits this once-per-tick scope; the private generated shard wraps its
final tail call using the existing patcher --parents 000900E0 --any-call
--tail-calls. Stripping observers gives the exact original guest instruction
sequence (apart from separating the call and return lines). Patch is idempotent.
Twelve deterministic host timing configurations pass with ASan/UBSan, including
20,000 ignored callbacks and helper exclusion; five patcher tests also pass.

Private engine-lifecycle-314 build45022 completed0; package audit75086 completed0.
Only code_014, xk_scene_thread and four version-bearing objects changed. Asset
contract unchanged; only game-a.self/boot-game.txt changed. Runtime SHA256
23796e8faf78be11bd1d03543c62619b8a0b157afe220aa92cc2783c0fd030a3.
Updater1209 completed0: verified true, slot0, boot confirmed; independent version
and boot SHA checks agree. No new optimization policy; strict worker gate and
all retained qualified paths preserved. The isolated b30-perf299 save was used;
normal a30 saves untouched.

Launch91773 and fresh-profile input sequence46487 completed0. Collector47630
completed its original600-second bound (includes loading/stationary gameplay).
Forty complete post-readiness lifecycle windows: final three object900E0
93.8867ms/frame; final collection8ECA0 0.01ms/frame. Both120calls/60frames.
Remaining object cost includes activation, regular callbacks and creation/
deletion; it is not callback-only CPU self time.

Last600 Present intervals (report ends7080..7620): mean124.2788ms/8.046FPS,
p95133.569,p99284.32,max591.425; ten>200ms,all600>50ms. Not a speedup,
matched actor-state comparison, enemy-combat verification or15-minute gameplay
qualification. Arrival screenshot shows landed beach, weapon and NPCs. No
STUCK/ABANDON/budget-abort/positive-mismatch/lifetime-invalid strings found;
this is limited log evidence only. See engine-lifecycle-314/lifecycle-summary.json,
settled-frame-summary.json, collision-summary.json and error-scan.json.

Reference instruction checks support8ECA0 as garbage collection: force flag+2,
free-memory thresholds0xCCCC/0x19999,2048objects/102free threshold,50activegarbage.
The new timer rules this final pass out as a major target. Next engine work
should audit the remaining8DDF0 animation/model-preparation boundary, already
partially native, for repeated setup and guest bookkeeping; do not rewrite
90770 merely because its child callbacks have inclusive cost. See the readable
plan in docs/engine-update-restructuring.md. Restore34959 was started after
collector completion to return to the strict-gate dashboard with collision
timing off and lease renewed; confirm terminal before new controls.

Restore34959 completed0. restore-status.json confirms314 dashboard,timing0,
strict gate/timing-off environment applied and3600-second awake lease renewed.
No collector,controller,build orPi test remains running from this turn.


## Root-matrix engine boundary candidate315

Source xk_math.c now implements opt-inXV_ROOT_CHAIN, joining8E419..8E58B's
three matrix operations for unattached objects. Arithmetic/order, intermediate
basis publication, final register/flags/x87/SSE and dead-stack state preserved;
malformed/attached/alias cases fall back before guest mutation. Root input
snapshot checks physical page contiguity, not only same-page addresses.
Exact-image source hooks pin both emitted call sites; private315stage adds
two register-body and two memory-fallback hooks, preserving current x87
spills/reloads and every original guest instruction. No other stage runtime
behavior changed; earlier private hierarchy fusion retained.

Host ASan/UBSan and Pi ARM32:1024fullstate/arena/FP cases,15malformed declines,
fourdisabled/diagnostic modes x1024 allpass. ARM comparescompleteFPSCR across
rounding/FZ/DN includingNaNs/denormals/zeros. Full liftedhierarchy integration:
host254cases/mode,Pi254enabled+254math-disabled;32newrootcases accepted when
enabled,0withnative math disabled. Initial full-loop test caught zeroadmission
on a valid page-crossing stack, corrected with a physical-contiguity proof.

PrivatePi~/xita-root-chain-315 jobscompleted; lastbenchmark baselineexisting
rootpair0.470us/call,newchain0.372us/call (three100k trials/path); localoverhead
only,notpredictedVitaFPSorlargewhole-framewin. Full findingsandcontracts in
docs/native-root-chain.md. All private evidence in../root-chain-315.

Vita315build78296 currentlylive; nativecompiler code_013 at fullCPU confirmed.
Pollsamehandle; no315deployment yet. Vita314safe dashboard retained and awake
lease renewed. Needfinishbuild/packageauditthenhardwareuptakeandordinaryb30
framecapture. Goalremainsunmet; do not equate this boundary with complete
object-updaterestructuring or a20FPSresult.

Build78296completed0; packageaudit21551completed0. Changedobjects exactlymath,
code_013,andfourversionobjects; contractunchanged,onlyruntimeandbootmarker
replaced. RuntimeSHA256689e3949cf34aa64adc8aa0e1ed6bec8acb5f1fad05ae7dc9a24f54399112b37.
315builtandqualifiedlocally,notdeployed. No build/Pi/controller/collector
remains running. Next deploy315 withXV_ROOT_CHAIN=1,strictgate,sameisolatedb30
save/settings;captureuptakeandframe/stalldistribution.
