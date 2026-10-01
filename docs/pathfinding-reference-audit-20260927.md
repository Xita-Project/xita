# Pathfinding reference audit — 27 September 2026

Status: investigation, not a deployed optimization. Hardware now runs perf292
(object lifecycle ordering fix; pathfinding behavior unchanged).
The private Halo CE Universal checkout supplied algorithm names; addresses and
layout below were checked against the owned retail executable's generated code.
No reference implementation or generated game source is included here.

## What the hardware evidence establishes

The retained perf288 `guard-detail-288/user-drops.log` has these explicit
parent-to-path-refresh edges. Times are inclusive and cannot be added to their
parents or interpreted as heap time.

| Caller | Frames | Calls | ms/frame | Approximate ms/call |
| --- | ---: | ---: | ---: | ---: |
| `15A890` | 60 | 36 | 13.67 | 22.78 |
| `15A890` | 60 | 31 | 8.50 | 16.45 |
| `14B230` | 60 | 14 | 4.94 | 21.17 |

These windows contain 113, 118 and 120 simulation updates respectively. This
is intermittent costly work, not proof that every actor searches every frame.
Unreported edges are not zero. The newer quiet captures did not reproduce it.
Private extracted evidence: `../path-reference-audit/path-costs.json`.

## Retail mapping and useful candidate

| Routine | Matched responsibility | Implication |
| --- | --- | --- |
| `15A290` | Actor path refresh | Includes destination selection and path construction; too broad to call a heap bottleneck. |
| `13A140` | Reset, begin, then inlined search traversal | Separate initialization, traversal children and uninstrumented loop work. |
| `139E30` | Begin search | Already separately instrumented in the diagnostic branch. |
| `139510` / `139110` | Heap insertion / bubble-up | Existing binary heap; replacing it with a heap is not an optimization. Signed costs and equal-cost ordering matter. |
| `139420` | Build path edges for a collision surface | Candidate for reuse of repeated geometry preparation, pending frequency, cost and validity evidence. |

The reference already checks destination changes and tracks refreshes within a
tick. Do not add a destination-only path cache: actor input, target-object
exclusion, danger attraction, broken surfaces and world changes affect results.

`139420` reads structure+`0x1E8` (pathfinding bytes), structure+`0xB4`
(collision BSP), then collision BSP surface/edge/vertex tables. Its output has
32-byte entries: adjacent surface at +0, flags byte at +4, base point at +8,
and edge vector at +20. Bytes +5..+7 are not written. The retail cap is 64
edges. Inputs are structure in EAX, surface and destination buffer on the
stack; it returns with `ret 8`.

This identifies a narrower reuse opportunity than caching an entire path.
It does not establish that these inputs are immutable for the map's lifetime.
Any implementation must account for BSP/map generation and pathfinding-data
mutations. Dynamic broken-surface checks in traversal must remain active.
It must also preserve output padding, edge order, guest scratch/register/flag
state, x87 rounding and preemption boundaries. An adjacent index of -1 cannot
simply be discarded when the retail code reads its associated byte.

## Supporting Pi census

Private `../path-edge-census-pi` wraps `139420` without changing its body or
other functions. It records calls, inclusive elapsed time and repeated
structure/surface keys within the previous 64/256/1024 calls. State is thread
local. This is a recent-call reuse census, not an LRU cache simulation.

The scan occurs outside the routine timer but still perturbs the harness.
The supporting harness is older than perf291 and has no Vita GPU. Repeated
keys do not prove equal outputs or safe cache validity. Results can justify
a correctness prototype or reject this target; they cannot establish Vita FPS.

The 180-second census completed with timeout status 124 as configured. It
reached ordinary a30 gameplay but emitted no helper-call rows (the first eight
calls would be printed). Thus this route supplies no reuse or cost estimate.
Retain the instrumentation for an active path-search workload; do not promote
the proposed cache. Evidence: private `result-summary.json` and `game.log`.

## Why the first Pi fixture misses the target

An additional read-only census wraps retail `14A162` and `14E1A0`. The former
reads the AI-global pointer at `0x2FA248`; retail checks +1 for map initialization
and +0 for AI activity. Across 7,680 observed update entries the flags were
active/initialized/control = 1/1/1, but the actor-control wrapper was never
called. This rules out those two global gates as the cause for this fixture;
it does not establish that the separately reported AI firing bug is fixed.
Bodies and other functions remain byte-for-byte unchanged apart from symbol
renaming; private source-audit.json records the check.

The map reader and reference scenario layout locate the retail trigger block
at scenario+0x360, with 74 entries of 96 bytes. Decoded names and coordinates
are private artifacts in `../ai-gate-census-pi`. The old route ends near
(35.88,-44.45,58.40), below the pass trigger's lower Z (61.82) and does not
cover the bridge box. Camera positions are navigation evidence, not proof
of player-body trigger execution. No game memory or saves were edited.

A first lateral route moved east to approximately (46,-33,58.38), the opposite
of the intended direction. A corrected westward input sequence is being
checked separately. Do not qualify pathfinding from either quiet route.
The current retail 13A140 and 139E30 already use x87 register lowering; simply
enabling that feature again is not a new optimization opportunity.

Both lateral-route probes finished with no actor/path calls. Intermediate
positions correct the initial interpretation above: left moved X from31.49
to31.37, right to31.59, then both followed the same forward route. Thus the
large positive-X endpoint was terrain-guided forward movement, not evidence
of reversed strafing. The initial lateral movement was almost blocked at the
pod, consistent with its walls. Prepared (not run) `run-exit-bridge.sh` exits
forward briefly before strafing toward the bridge, then advances toward the
ledge. This is a navigation hypothesis to verify, not a successful encounter.

## Exit-first route and live host input

The exit-first route completed its180second bound (status124). Actor control
reached393calls, but139420 still had no calls. Camera minimumZ=-29.97 confirms
the route fell into the canyon; exclude it from combat/performance evidence.
It did reach the bridge box. Forward movement from the middle of that bridge
is not a safe crossing: the crossing runs mostly along X.

Private `ai-gate-census-pi/live-pad/live_pad.c` is a linker wrapper around the
host pad poll, not a Vita change. A local file supplies sequence, hold frames,
axes/buttons/triggers. Automatic release, stale sequence rejection, invalid
range/trailing token rejection, explicit release, disabled default and frame
wrap were checked on PC and ARM Pi. All passed. No network server is added.
This supports route adjustments without restarting the whole harness. Its
file polling/diagnostic overhead excludes the run from FPS qualification.

The live harness is running with a600second bound in the separate
`runs/codex-ai-live-20260927` directory. Scripted input only launches and exits
forward60frames. Wait for gameplay and frame>1260 before local-file commands.
A controlled diagonal strafe can compensate for camera heading when crossing
the bridge; verify coordinates and automatic release before advancing.

The live run also completed its600second bound (124), with seven accepted
and automatically released commands. No139420 calls were captured.
A private collision-surface plot now identifies the actual bridge deck near
Y=-98.3, substantially narrower than the trigger box. PreviousY=-97.6 missed
the deck. Partial raw stick3500 did not provide useful heading compensation.
Manual camera durations were also unreliable. The next fixture needs bounded
coordinate-feedback navigation; no path cache is justified by these runs.

## Focused hardware diagnostic prepared

Private path-cost-293 instruments only15A290(refresh),13A140(search),139420(edge
construction) with entry/exit wrappers. Original bodies are preserved exactly
(source-audit.json). Thread-local counters accept child scopes only for the
active root xctx; unrelated contexts and nested root calls do not replace it.
Integer clocks count inclusive elapsed; reports after64completed roots use the
existing xk_os_log path. No history scans, caching, guest writes, or algorithm
changes are introduced. Child times overlap root time and cannot be added.
Calls outside15A290 are deliberately excluded; a zero edge count does not imply
139420 is globally unused. Fixed clock/report overhead still needs consideration.

Scope/count/context tests pass onPC andPi. Vita build session53013 is in flight,
not deployed. Authoritative ordinary build remains perf292; its timing results
must not be conflated with this diagnostic. Private path_cost.c is compiled via
code027 only; header declarations are used by the other two wrappers. Validate
final changed objects and package before hardware use. Longjmp/nonreturning
scopes are not covered by the unit test; inspect completion/report counts.

Vita build53013 and package audit99861 completed0. Exactly the three target
shards and four version objects changed versus292; package changes limited to
game-a.self/boot-game.txt. Runtime SHA708a092eec594c0c10942f0764c8673083760263f48c7f8a03a62b164412cf32.
Protected test-save backup copied to path-cost-293/save-before-update, all15
files two-read verified. Authorized upload/apply46163 is now pending. Inspect
that handle/update-status before any further restart. Perf292 is the rollback;
perf293 timing is diagnostic, not a claimed FPS improvement. Matching a30
launch-normal.py prepared; use it only after confirmed293 dashboard boot.

Upload/apply46163 completed0:34860134 bytes, expected708a...cf32 SHA verified,
slot1 boot confirmed. Perf292 retained in slot0. Launch sequence46576 is live
with same protected a30 save/settings. New private stream-path.py reads log
incrementally, retains full bytes, reports readiness/path-cost lines, verifies
build/frame continuity and renews keep-awake. Run it for diagnostic capture;
its900-second deadline ends collection only, not the game. No path reports or
hardware performance improvement established yet.

Perf293 launch46576 completed0. Incremental capture39560 is live, reached
loaded1/active1/director1; image named cutscene.png actually shows the lifepod
(post-cutscene), not the canyon. Private scope-call-audit.json verifies direct
15A290->13A140 (two static sites),13A140->139420(one), and no guest callees in
139420. Times are nested. Outdoor route35553 is in flight, with initial5-second
pod exit followed by prior route; verify terminal input release and screenshot.
No path report yet is not evidence of zero calls: report threshold64root calls.

Outdoor fixed route35553 and correction80333 completed, controls released.
Their timing-based turns were not position-repeatable: last telemetry44.50,
-98.53,61.14,fwd0.17,-0.98. No64-root path report yet. New private guided-route.py
(session26319) uses existing streamed camera telemetry, short cardinal stick
commands, post-release fresh reports, waypoints31/-98.3,26/-98.3,3/-98.3, height
floor57.5 and blocked/stale/deadline stops; finally releases input and captures
route. Collector39560 stays live. Verify these handles, not assumed completion.

Additional baseline evidence: object-lifecycle-292/cutscene-interior-summary.json
isolates240frames inside the final loaded1/active1/director0 block after map
loading, with60-frame margins. Mean63.182ms=15.827FPS,p9585.170,p99152.422,
max281.843;204>50ms,6>100ms,1>200ms. Initial main-menu director0 reports are
explicitly excluded by selecting the block after the last unloaded report.
This is approximate report alignment, not full cinematic/canyon acceptance.

Guided route26319 reached31/-98.3, then26.22/-98.55/58.95, where small correction
commands show almost no movement. Await its terminal stop before issuing other
controls. Stream39560 still live with no64-root report. Possible short-pulse
input sampling or terrain obstruction; do not infer pathfinding has no cost.

Route26319 stopped1 via explicit eight-step no-movement guard; finally released
input and saved screenshot/trace. Image shows level ground, not a fall. Replaying
small correction with minimum150ms rather than50ms is now live as63594, starting
from26.24/-98.55 and retaining all height/staleness guards. This tests whether
short input pulses were missed; no jump or guest state edits. Stream39560 remains
the source of camera telemetry; stop controls if it finishes before route.

## First useful hardware path breakdown

After route63594 stopped via no-movement guard (controls released), bounded
sideways correction32929 completed. Camera moved to25.28/-98.14/58.92 and the
path profiler began reporting. Collector39560 still live at this observation.
Partial63reports/4032refreshcalls: refresh7,626,172us; search894calls/1,235,471us;
edges46,800calls/262,775us. Search is16.20% and edges3.45% of inclusive refresh
time, zero nested-root declines. Largest64-call root report818,427us contains
185,138us search and41,842us edges. These are batches of calls, NOT per-frame
costs and NOT additive CPU self times; instrumentation/interrupt waits included.

This materially lowers priority of edge caching as the main solution. Remaining
refresh work dominates: reference actor_moving.c includes destination/prop
lookup, destination tests, path-state initialization and path construction after
search (including smoothing). Need time these calls before choosing a rewrite.
In particular, do not equate path_state_find13A140 with the entire path refresh.
Private path-cost-summary-partial.json retains the numbers. Full collector
summary remains pending. No further input controller is live.

Collector39560 completed0 at900s. Full summary: 86 reports, 5504 refresh calls; search 15.21%, edges 3.2% of refresh inclusive elapsed. See path-cost-summary.json. All controllers/capture processes now terminal; Vita remains running293.

## Refresh-child diagnostic candidate294

Private path-detail-294 adds call-site elapsed scopes inside15A290 for11040,
112E0,1391F0,139220,139260,13A280,13A310,153610,157580,157B50,157BE0,159440.
13A140 already measured by293 is not double-wrapped; stack probe1D130 excluded.
Reversing inserted host scopes reproduces the parent function body exactly.
All original calls, guest state writes, x87 spills, branches and return behavior
remain. Thread-local counter arrays expanded to15kinds; child rows reported with
the same64-root batch, not independent time windows. Includes memory setup and
path reconstruction, without assuming which is costly. Timings remain inclusive.

Unit tests exercise both original three scopes and new child3/14 report values,
context rejection, invalid kind and reset; PC and Pi pass. Build87880 live;
not deployed. Prepared strict package audit expects only code027 and four
version objects changed versus293. Hardware still293 and previous capture ended.
Static call audit:13A310 calls1383D0/138FA0/17BC80 plus112E0;159440 calls14C9C0;
153610 calls363C0/49430/8B910. Names/costs need evidence, not inference alone.

Perf294 build/package audits and fifteen-file two-read save backup completed.
Upload/apply25329 completed0; expected5c1b3472...b33 runtime hash verified,
slot0 boot confirmed. Launch17147 started using protected a30-perf211.
Previous ordinary baseline292 retained as a package; previous device slot293
is diagnostic. Timing scopes add observation overhead, not an FPS gain.
A transient update-status connection refusal occurred during reboot; update
process subsequently confirmed boot, so no extra restart was performed.

## Perf294: path construction dominates refresh

Launch17147 completed0. Capture2821 is live. Guided route72683 completed0,
reaching25.16/-98.29/58.90 without a fall; controls released. The route used
short telemetry-guided movements directly from the pod, avoiding old long turns.
Partial53batches/3392refresh calls: root9,927,911us; construction13A310
8,177,674us (82.37%); search1,614,634us (16.26%); edges349,037us nested insearch.
This is inclusive diagnostic elapsed across calls, not CPU self or frame savings.
Private path-detail-294/path-cost-summary.json retains evolving counts.
Retail13A310 calls1383D0 then17BC80, matching reference path_state_build_path's
smoothing then obstacle-avoidance sequence. Do not remove either behavior.

Private candidate path-build-295 adds scoped direct-child timers within13A310,
1383D0 and17BC80, excluding stack probe1D130;29total timer kinds. Reversing each
inserted scope reproduces all three original function bodies exactly. No guest
state/branch/call changes. Additional child times are nested and not additive.
PCscope/count/context/new-last-kind tests passed. ARMtest uploaded toPi;
Vita build19150 is live, not deployed. Device remains294, capture2821 active.

Perf295 Pi scope tests passed. Build19150 failed because existing declarations
were appended below new call sites; moved path_cost.h to top of025/026/028.
Whole-shard reversal audit confirms original294sources after removing scopes
and normalizing header placement. Build74237 completed0, but artifact audit
correctly rejected unchangedcode027.o: embedded path_cost.c lacked a dependency.
Forcedcode027recompile; build44352 active. No rejected package deployed.
Save backup25636 completed0,15files stable. Capture2821 continues on294.

Capture2821 completed0 at900seconds. Final159batches/10176refreshcalls:
21,254,197us refresh;18,070,135us construction(85.02%);2,799,816us search;
574,427us edges nested insearch. Finalsummary retained privately. Mostly
stationary diagnostic capture, NOT15minutes active gameplay qualification.
No controllers or collectors remain active. Vita294 remains awake/running.

Build44352 and packageaudit72766 completed0. Expectedfourshards+fourversion
objects changed; onlygame-a.self/boot-game.txt assets differ. Runtimehash
dd07b81a19f80840d5d71f89f42d93f957928f0c6db60e2fcc46a9053f5ff5cc.
PrivateMakefile now declares embeddedpath_cost.c/headerdependencies explicitly
to prevent future stale objects. Authorized295upload initiated; inspect handle
before any restart. Next confirmed295dashboard: launch-normal, stream-path,
route-when-ready inpath-build-295. PC/Pitests passed; noFPSclaim.

Pendingupdate handle37379; no launch/capture/controller started for295yet.

Update37379 completed0,295slot1boot confirmed and dd07...ff5ccruntime hash
verified. Launch66176/capture88575/readiness-route52692 live. Route only starts
once loaded/active/director1 appears and collector remainsfresh. Same protected
a30 save and settings. No optimization or acceptance claim from diagnostic.

## Perf295 first breakdown: smoothing clearance tests

Launch66176 and guidedroute52692 completed0, camera25.02/-98.29/58.88.
Capture88575 remainslive. First20batches1280refreshcalls:4,847,206us root;
3,805,224us13A310;2,855,388us1383D0smoothing;2,740,237us137740(14675calls);
935,886us17BC80avoidance, including763,731us17BC10 and132,569us138C30.
Smoothing~75%ofconstruction;137740~96%ofsmoothing. Nestedtimesarenotadditive.
Staticretailcallorder and reference smoothing's structure_test_pill2d identify
137740 as repeated pathclearance checking. It already hasx87registerlowering;
need optimize actual loop/memory/translationwork, not reenable sameflag.
137740 has718generatedlines, onecallee136FD0; its inclusive time can contain
thatcallee and preemption. Existing136FD0timer onlycoversdirectsmoothingcalls,
NOT137740's nestedcall, so do NOT subtract thatrow toclaim137740selftime.
Next: inspectclearance traversal, buildcorrectness fixture/nativecandidate
onPi, preservingbroken-surfaces/collision ordering/FP and outputsemantics.

## Longer perf295 window changes priority

Do not treat first20batches as representative: completedbatches21..152 have
9,933,999usrefresh,9,109,975usconstruction,814,135ussmoothing,8,234,496usavoidance,
7,578,389us17BC10. Initialsmoothingwork is front-loaded; recurringobstaclework
now dominates. Privatepath-window-summary.json excludeslastpotentiallypartialbatch.
17BC10 loops17BAB0 after17BA10initialization;17BAB0 calls1374B0,17B1F0,17B420,
17B5E0,17B7D0. Need preserveobstaclepathsemantics andpreemption; don'tassume
clearance137740 alone fixessteadycombat. Retainboth targets, prioritize measured
recurring avoidance for a native/repeated-work candidate.

Privatepath-clearance-pi built successfully99898. Onlycode025.o differs from
retainedarm-o2-lifecycle (objectaudit). 137740/136FD0bodyaudit matchesoriginalhost
andcurrentVita exactly. Addsroot137740elapsed andnested136FD0elapsed only,
reporting512rootcalls. Navigationlinked withretainednav-camera.o; uploaded and
run45196 active, bounded240seconds, privatefreshsave. Piwasverifiedfreeofother
harnessesbeforestart. Noalgorithmchanges/noVitaFPSinference. Capture88575 on295
stillactive; allVitainputcontrollerscompleted/released.

## Completed295capture and first optimization candidate

Capture88575 completed0 at900seconds:187batches11968refreshcalls,
17,423,602usroot;15,336,516usconstruction;11,360,552usavoidance including
10,349,641us17BC10;3,883,851ussmoothing including3,722,723us137740.
Notactivecombatacceptance. Device295remainsrunning;lease3600renewed.
Pi45196finishedplannedtimeout124at240seconds. first-run.log retained:36reports,
18432clearancecalls549400us,110592nestedcalls494773us. Childcountercouldoverlap
recursive136FD0calls; tightenedwithdepthguard andrecursivecount. Rebuild1690
completed0, correctedarm/harnessNOTnavigationlinked/uploaded/runyet. Initial
Pi binary/run preserved. NoPiharnessremainingactive.

137560(obstacle ray)calls872D0;136FD0(smoothingline)calls877B0 andcanrecurse.
872D0is312lines,noguestcallees,andstillmemoryx87lowered. Existingregenerated
baseline matchescurrentVita body underobserver normalization. Private
path-ray-register-candidate/reference.c andcandidate.c extracted; candidate
isOLDregenwithbareX_PREEMPT, notqualified. Currentcompiler regeneration24331
active (--x87-regs-only000872D0, ownedretailimage/symbols); useitsoutputthen
verifyyieldhandling andbuildPC/Pidifferentialmemory/context tests, including
preemption andboundary/NaNcases. Do notdeployoldcandidate. NoFPSclaim.

Current872D0regen24331 completed0 and replacescandidate.c (oldcandidate retained).
It correctlyspills/reloads atyield. Newprivate differentialfixture covers2400
polygon/raycases,3..32edges,bothorientations,all8FPUtops,degenerate/nonfinite
rays,andmutatesFPUstateatyields. PCpasses2400cases6750yields;negativecontrol
wronginitialboundfails case14. ARMfinitepasses2000cases5494yields, butboundary
case2019failsyieldslot2NaNsign(candidate7ff8a86420000000/referencefff8a86420000000),
finalcontextandmemoryequal. CandidateHELD,notdeployed. SupportingPi timings
reference/candidate ns:3edges559.7/538.6;13edges1799.6/1645.0;23edges3051.5/2774.4.
Small3.8..9.1%leafgain,notwholeframe,requirescorrectNaN/yieldsemantics before
promotion. BothPi tests terminal (34944exit1,67720exit0). Next investigate
strictoperationordering or economical admissibility fallback; do notdiscard
NaN/yieldcomparison justbecause finaloutputmatches. Fullresults privatejson.

## 872D0 candidate corrected and perf296building

ARMdisassembly showedcandidateVNMLS combiningmultiply/subtract;referenceuses
separateVSUB. -ffp-contract=offalone didnotpreventnonfusedVNMLS. Private
candidate-ordered.c insertsARMemptyasm +wbarrier onxr0/xr2 before87367subtraction.
ARMtest74322 passed2400cases6750yield snapshots withfullcontext/memorymatch,
includingpreviousNaNcase. ExpandedThumbtest55727 passed4000cases11124yields,
addingnonfinitepolygonvertices; timingsreference/candidate3edges549.5/533.5ns,
13edges1851.3/1742.6ns,23edges3153.7/2960.3ns. ExpandedPCtestalso passed.
Theseareisolatedsyntheticmeasurements, notVitaFPS. ARMbarrierpreservesregister
allocationwhilepreventingtheobservedNaNsign-changingcombination.

Privatepath-ray-296 basedonordinaryobject-lifecycle-292 (allpreviousgains,
lifecyclefix; no293..295diagnosticcost). Only872D0bodyreplacedafterexactbaseline
bodymatch; orderedcandidatehash in source-audit.json. Build23127 active.
Preparedauditexpects code013+fourversionobjects, packageonlygame-a/boot-game.
Copy-save/launch-normal/stream-path/route-when-ready scripts ready; NOTdeployed.
Device295stillrunning; noPi jobs orcapturecontrollersactive. Nextverifybuild,
packageaudit,savebackup,thenauthorizedupdate andordinarya30test. Do notclaim
20fps orwholeframegain fromthe3..6%Piper-helperresult.

Perf296savebackup49170completed0 (15filesstable). Additionaltest95581completed0:
actualVitaGCCcompiledreference/candidatehelperobjects linkedintoLinuxARMharness,
4000cases11124yields passed. Pi timings620.1/613.1ns(3edges),1909.4/1793.5(13),
3230.5/3004.0(23). Linkerwarnedsmallenums/GNUstack; xctxusesexplicitintegerfields,
noenumsacrosshelperboundary. ThischecksVitaGCCarithmetic,stillnotVitaFPS.
Build23127remainslive;notdeployed. UserreportshavingXboxSDK;askedlocalpath/version
asynchronously. ItmayinformHLEsignatures/layouts; noSDKfilesaccessed/copiedyet.

Perf296build23127 andpackageaudit42982completed0:code013+fourversionobjects
only;game-a/boot-gameassetsonly. Runtimehash65a50bc95fdac42ef76d86da4a4b06873b9c5e898c169e58c69fc8a3ed3eaa3a.
Upload58301completed0,slot0bootconfirmed. Authorizedordinarya30launch/capture/
readinessroute started frompath-ray-296;inspecthandlesbeforemoreinput.
SDKarchivefoundinstalecwd:2001-08 -3911.1(alt),containsXBOXSDK_3911.ISO.
SelectedSDKfilesextractedprivatelyunderprivate-references/xdk3911;noinstaller
executed. XboxD3D8.h/DSound.h/XboxSDK.chmavailableforABIresearch. Originalarchive
untrackedandnowlocallyexcludedvia.git/info/exclude; noSDKcontentaddedtosource.

Live296handles:launch37740,capture48303,readinessroute32270.

## Perf296 ordinary outdoor measurement

Launch37740 and guided route32270 completed successfully; camera ended at
25.21,-98.19,58.91. Collector2292 completed0. Full timings and input brackets
are in path-ray-296/gameplay-summary.json and gameplay-marks.json.
Settled740frames:61.070ms,16.375FPS,p9576.601,p9987.323,max180.395;592>50ms,4>100ms.
ARfire55frames:92.664ms,10.792FPS,p95123.796,max129.005;all55>50ms,12>100ms.
Recovery744frames:60.552ms,16.515FPS,p9574.578,p9985.884,max109.315;596>50ms,5>100ms.
No crash during this short test. Not sustained NPC combat, not paired with292
and not proof of regression or benefit. Target remains unmet; firing adds
about32ms relative to this stationary window. Capture48303 remains active
with keep-awake renewal. Next inspect firing profile and obstacle-avoidance
path, rather than treating leaf872D0 microbenchmark as a whole-frame win.

Private SDK layout audit confirms240-byte pixelshader definition and15
field offsets used in xd3d.c. Signed vertex constant register signature and
texture format/size/pitch bit extraction agree with inspected3911headers.
No new mismatch found in this limited audit; it does not qualify allSDK
versions, method numbers, or complete resource semantics. NoSDKcode copied
into repository. Receipt private-references/xdk3911/layout-audit.json.

## Clearance child attribution follow-up

Corrected Pi95335 verified live at218seconds with frame4102 past pod exit,
no restart. Recent512-root reports show3072 line children, zero recursion,
roughly76..81% child/root elapsed. Final aggregate pending terminal process.
Next diagnostic path-clearance-detail-pi built35090completed0, objectaudit
onlycode025differs. Four direct call sites inside136FD0 instrumented:
three11730(vector helper), one877B0(intersection). Original body reconstructed
exactly by removing wrapper names; root/context admission preserved; no
guest memory write or algorithm change. Audit/link/transfer99772completed0.
Do not run until95335 finishes. Remote run-clearance-detail.sh uses unique
directory and240second timeout. Hardwarecapture48303 completed0 after900sec;
Vita keep-awake3600 renewed. PhysicalVita remains296.

Corrected95335 completed124 at planned240s; full log copied. Aggregate {"calls": 28160, "root_us": 916126, "children": 168960, "child_us": 836729, "recursive": 120, "reports": 55, "child_fraction": 0.9133339737110397, "limits": "Pi headless supporting profile, not Vita frame-time or CPU self; completed batches only"}. Detail run69612 now active; poll before further Pi work.

Detail Pi69612 completed124 at planned240s; full log copied; {"calls": 13824, "root_us": 433849, "children": 82944, "child_us": 392307, "recursive": 14, "projection_calls": 82958, "projection_us": 22931, "intersection_calls": 0, "intersection_us": 0, "reports": 27, "leaf_fraction_of_child": 0.05845167177746}. Timer label vector means11730 coordinate projection, not a generic vector arithmetic kernel. Inclusive observer timings only. No Pi job remains.

Prepared private path-line-inline-candidate: exact ordinary296136FD0 body,86 f32load sites and25 stores renamed to exact same helper bodies with always_inline. Original Pi object has out-of-line f32 helpers; helper call traffic is a candidate, not measured gain. No arithmetic, page mapping or yields changed. source-audit.json records reversible transformation. Not compiled/tested/deployed. Next differential replay/synthetic coverage including split pages and yield-state equality, then Pi cost before hardware promotion.

## Float-inline lead rejected after actual Vita object audit

Private path-line-inline-candidate PC and actualVitaGCC objects onPi passed1600cases/2243yield snapshots, full4MiBmemory/context, split noncontiguous guest pages, nonfinite endpoints and yield-state mutations. Wrong primitive negativecontrol failscase0. Pi timing pairs(ns)1161.2/1154.9,2416.5/2402.3,3575.8/3567.3 are effectively unchanged. Crucially, actual ordinary296code025.o disassembly for136FD0 has ZERO out-of-line float-helper references; only split-page slowcalls remain. Initial out-of-line observation was LinuxPi shard, not productionVita. Do NOT build/deploy this candidate; no Vita overhead removed. Retain fixtures for a substantive traversal replacement. Tests cover synthetic single surfaces, not full multi-surface/breakable geometry. Test38732completed0; noPi job live. Hardware296unchanged.

## Cached float mapping candidate

Private path-line-root-candidate reuses RAM/table roots between guest-call/yield boundaries in136FD0. Refreshes atentry plusall8boundaries(3preemption,5guestcalls). Checkedmemory usesoriginalprimitives; splitpages keeporiginalslowcalls. No arithmetic/ordering intentionally changed. PC and actualVitaGCCobjectsonPi62664pass1600cases2243yield snapshots. PC remappingfixture switches table pointer atyield and invalidatesoldtable, passes; removingrefreshes negativecontrol failscase8. Pi ns reference/candidate1235.3/1233.3,2556.5/2391.6,3728.1/3554.0. Supporting isolated~0..6%gain,notVitaFPS. Isolated VitaTPIDRcompile mrcsites121->34; productionbaseline136FD0has113,so fullshardcandidate auditrequired. Pi tests usedglobaltable variant, noTPIDRexecution; ARMremap testpending. NoPi jobactive,nohardwarechange. Next compilecandidateinsideordinary296shard,verifytokens/rootrefreshes and actualassembly; then qualify before newpackage.

## Perf297 qualified build and deployment started

Actualfullshard compile5227completed0 usingretained296code025command. Full136FD0MRCsites113->9. ARMremap27315completed0:actualVitaGCChelperobjects,1600cases2243yield checks withpointerchanges atyield. Candidate remains exactorder,refreshes atall8boundaries. Perf297private stagebased296retains872D0/lifecycle/allpreviousgains. Build39415completed0;packageaudit83607completed0 onlycode025plusfourversionobjects;onlygame-a/boot-gameassets. Runtime993ce5e950527809fec6ff2f51afce86fc44753292cbdd11ebafb6aa4bf05ecc; packagef73e5e82b79bf265b06b32a5317830d43bbe62b4f3dec576131bc8acb2e12e43. Savebackup83079completed0,15filesstabletwo-read. Authorizedupdate7585active;do not restartuntilterminal. Then verifyboot297,startlaunch-normal,stream-path/readinessroute andordinarycollector inpath-line-297. NoFPSgainclaimed.

Perf297update7585completed0:runtimehashverified,slot1bootconfirmed. Launch57896completed0. Capture26288active900sec withkeepawake. Readinessroute46613active waitinggameplay; no otherVita inputcontroller. Whenrouteexits0runpath-line-297/collect-gameplay.py.

Privatepath-avoid-detail-pi build32004completed0;code028onlychangedversusarm-o2-lifecycle. Sixoriginalbodiesexactmatchordinary297:17BC10root,17BAB0iteration,17B1F0/17B420/17B5E0/17B7D0children. Context-admittedTLSinclusive timers,depthguard,report64rootcalls; nosemanticschange. Transfer71548completed0; noPi harnessobservedbeforelaunch. Runstartedrun-avoid-detail.sh(unique240secondrun); handleinlatesttoolresponse. Sampling is supportingheadlessprofile,notVitaFPS.

Piavoid-detail handle67580active;Vitaordinarya30gameplayreadinessnowseeninlog(cam31.64,-103.41,59.47). Route46613ownscontrols untilterminal.

Route46613completed0 atcamera24.84,-98.31,58.86. Ordinarycollector78499active,settled/fire/recoverysequence. Slightendpointdifferencefrom296(25.21,-98.19) andAIstatevariation precludeexactpairedgainclaim. Pi67580stillactive; capture26288stillactive.

Perf297collector78499completed0. Initial639frames70.689ms14.146FPS,p95122.119,max287.428,93>100ms;fire57frames93.349ms10.712FPS,p95127.3,max137.642,12>100ms;recovery717frames63.036ms15.864FPS,p9574.984,max89.931,all717>50ms. IMPORTANT:initialwindowwasNOTstationary:camera24.84,-98.31->33.48,-103.84,59.85,forward0.21,0.97,-0.14. Movementsourceundetermined;finalscreenshotshowsnearbygrunts. Correctedsummarycaveat. Notpairedgain/regressionproof. RecoveryrenderhelperCPU~54..55ms,sceneWall59..60ms;FA920~41..43msinclusive. Concurrentnestednotadditive. Noobservedcrash. Hardwaretargetunmet.

Piavoid67580completed124planned240seconds;fullruncopied,zero complete64-rootreports. NOTzero-costproof:lowerbatchthresholdorqualifyavoidanceworkloadnext. NoPi jobactive;capture26288stillactiveVita297withkeepawake. Controlsreleased.

## Current render attribution launch

Scene phase mode readonceatstartup;requiresrestart. Ordinarycapture26288 deliberatelySIGINTstopped(exit130),notcrash; rawlogsretained. Newrender-phase-297 savebackup81562completed0,15filesstable. Companionnosleepon,quitonlyXITA00001,relaunchsame297;observer49411completed0 withfreshdashboardtiming_frame0. NoVPKchange.

Preparedrender-phase-297 driver.py sequenceslaunch-diagnostic->readinessroute->collector, fail-stop(noautorestart). Concurrentcapture900secwithlease renewal writesrawlog. XV_SCENE_PHASES=1 andREC_WORKER_TIMING=1(replacesexplicitFRAME_SLOW_MS override); sameothernormal297launchconfiguration. Observeroverheadandroot-pairadmissionconfoundmeansNOFPScomparison. Purposeiscurrentowner/scenechildattribution. Driverhandlelatesttoolresult;checkbeforeotherinput. NoPijobactive.


## Completed perf297 phase attribution and observer limitation

render-phase-297 driver59167 completed0. Capture PID1593569 is now absent;
raw capture is retained. Live status verified perf297, frame11628, CPU444.
The 72 parsed report batches have approximate near-end-frame association,
not exact frame IDs. Initial nine batches: scene5DBC0 43.16ms inclusive,
model5B760 15.50ms, material70110 6.73ms (4.53 uninstrumented remainder).
Recovery eight batches: scene45.38ms, model16.13ms, material7.04ms;
tickFA92077.80ms, collision4C98016.93ms, collection171F1011.62ms.
These are nested elapsed values including waits; do not add them.
At nearby frame7560, collection includes88110 6.67ms and1716F0 2.23ms.
The firing interval has only37frames, so no fully contained60frame phase
batch exists. No precise firing-subtree attribution follows from that window.

Authoritative raw log confirms root-pair accepted0 with roughly19000–21000
declines per60frames during this diagnostic. This is a confirmed observer
confound, not merely a possibility. Native query remains active with zero
declines; zero node counters are because detail counting is disabled, not
because traversal performs no work. Existing native code already has fast
ancestor membership scans. Do not reimplement the older scalar-query prototype
as though this were an untranslated query. Next useful query evidence requires
native internal counters/timings with ordinary root-pair admission retained.
Preparing ordinary-return-297 restart to remove scene observers; same binary,
no new optimization or FPS improvement claimed.

Ordinary restore: two-read save backup49079 completed0 (15 files stable),
companion no-sleep enabled, only XITA00001 quit/relaunched. Fresh297 dashboard
confirmed frame33/timing_frame0. Launch controller2866 executes existing normal
sequence, with scene phases0; poll to completion before any further controls.
Log capture92439 is live for900seconds with3600second leases renewed every60s,
outputs ordinary-return-297/path-stream.log. No binary update. Source confirms
xk_math.c xv_math_root_pair explicitly rejects SCENE_PHASES>0 or xv_phase_enabled;
the phase observer confound is by design. Native query detail counters/timing
can be enabled separately through XV_NATIVE_4B9D0_TIME on a later launch, without
scene profiling; this is the next narrow measurement candidate. Current launch
is ordinary and does not enable those additional counters.


## Ordinary297 confirmation and counter prototype

Launch2866 completed0; guidedroute69413 completed0; collector74762 completed0.
Root-pair restored: roughly17k–20k accepted per60frames, about100declined.
ordinary-return-297: settled733frames61.793ms(16.18FPS),p9586.267,
p99115.511,max181.091,10>100ms; firing59frames92.413ms(10.82FPS),
p95119.798,max165.705,18>100ms; recovery807frames55.984ms(17.86FPS),
p9569.653,p9977.674,max111.494,2>100ms. Recovery619/807 still>50ms.
Final screenshot inspected: correct outdoor game view, AR6 rounds, enemies on
radar; finalcamera25.14,-98.40. This short stationary sequence is not multi-NPC
combat qualification and is not paired proof of297 benefit. Capture92439 remains
live with lease renewal; no input controller remains. No crash observed.

Private query-counter-prototype removes10 per-query diagnostic increments and
4 scan-sum updates, preserving semantic backedge budget. PC73054 and actual Pi
85609 completed0:150cases,5guest timeouts skipped; existing oracle normalizes
14NaNpayloadwords onARM, so do not claim bit-exact qualification. Cross-build
72414/90916 exited1 only at expected host execution of ARM binary; saved artifacts
then executed successfully onPi. Pi benchmark30266 completed0: instructions
5079/5042,5080/5042,5079/5042 baseline/candidate (~0.7%); cycles4042/3943,
4079/3959,4071/3953. First wall-time pair is noisy on both guest/native.
Harness forces detail counting on unlike normal production. Retain as a small
private candidate; not deployed and not a demonstrated frame gain. Pi idle.
Next substantial target remains native collision workload and ordinary firing
cost, with separate query counters rather than all-scene phase mode.
