# Xita Roadmap

Xita runs Halo: Combat Evolved's original Xbox executable on the PlayStation Vita. The
offline recompiler (`python -m recompiler`) lifts the x86 code to C; the runtime
(`xita`, `xita.vpk`) supplies the Xbox kernel, Direct3D and DirectSound surface on top of
the Vita's GXM, sceCtrl, sceAudio and sceIo. This document is the planning reference for
the remaining work. Gameplay performance and stability gate later work; the dashboard
is independent of the multiplayer transport.

Status legend: `[x]` done, `[~]` in progress or unverified on hardware, `[ ]` not started.

**Active objective (September 15):** restructure costly Halo CE engine and rendering
work into efficient native routines and independent worker jobs until sustained
30 FPS is verified on the physical Vita in representative campaign, combat and
vehicle gameplay. Keep rendering and gameplay correct, document fixed comparison
settings and retain private checkpoints. Stable 20 FPS is an intermediate
milestone; neither target is complete. The separate Halo 2 agent is pursuing the
original main menu in Vita3K. See the [implementation order](docs/performance-next-steps-20260914.md).

[~] [Experimental concurrent object updates](docs/parallel-object-experiment-20260914.md):
the next task now runs whole second-pass object callbacks on workers requesting
cores 0 and 1, with the guest owner handling permitted kernel requests. This follows explicit approval
to accept broken gameplay/rendering while finding dependencies. The ordinary
build remains separate. Host scheduling/control checks pass. The first actual
campaign run exposed a shared cluster-list race; guarded list/datum operations
now complete the emulator off/on/off trial with real work on all three threads.
Physical worker affinity is verified. Follow-ups protect collision work, gate
jobs on loaded gameplay and service supported cache I/O and audio requests with
the other lanes parked. The [rocket-pickup audit](docs/object-audio-handoff-20260915.md)
identified another sound-cache yield. A physical pickup now succeeds with cache
I/O serviced. The recovered firing log identifies another intentional worker
STOP in a stream-volume update; the [owner handoff fix](docs/object-stream-volume-20260915.md)
retains both workers and awaits a physical retest. The separate
[submission audit](docs/rocket-submission-audit-20260915.md) found no stride or
buffer-content violation in 11,721 captured emulator draws and adds a guard
against submitting draws after failed fragment-constant setup.

The next physical valley walk reached a different STOP when firing the plasma
pistol: a deferred audio-settings commit. The
[commit/fade handoff](docs/object-audio-commit-20260915.md) services that call and
the following stream-volume path on the owner, retaining both workers. Host
memory/race checks and the native build pass. The installed candidate completes
normal and charged plasma shots on hardware, and its log confirms the formerly
rejected audio commit executes successfully. Broader combat stability remains.
The walk includes a 13.9 FPS logged window at 848×480; it is not a controlled
gain measurement or a sustained-frame-rate result.

A longer physical run exposed another owner-service STOP in object sound cleanup.
The [voice-stop handoff](docs/object-voice-stop-20260915.md) routes that exact
caller through the parked-worker audio path. Production memory/race tests and
native compilation pass. The installed build loads Blood Gulch on hardware;
the exact cleanup path and longer combat stability still need a gameplay retest.

A subsequent retained run reached a frequency-update STOP. The
[sound-property audit](docs/object-sound-parameters-20260915.md) covers frequency
and five related spatial-setting calls in the same update routine; all retain
the existing owner-side handlers. Host memory/race checks and compilation pass;
the emulator completes a charged shot and the physical updater confirms the
new build. Longer gameplay still needs to exercise these property handoffs.

[World and effect rendering](docs/world-rendering-20260915.md) is the next focus:
physical views range from 55 to 131 draws/frame with much higher completion
latency in the heavier view. An opt-in replacement-blend candidate preserves
channel masks and alpha tests. Three native-resolution Blood Gulch off/on/off
trials differ by less than 0.4%, without a useful demonstrated gain; it remains
off. Exact same-frame index reuse also shows no gain in three physical trials
and remains disabled. Next, isolate expensive world and effect passes and the
remaining translated CPU work, preserving the existing crash and scheduling fixes.

September 15: [lightweight mutex comparisons](docs/object-light-lock-20260915.md)
repeatedly improve one physical Blood Gulch view from approximately 11.8–12.0
to 15.1–15.2 FPS, with both workers enabled in every arm. The experimental build
now selects that mutex by default. This compares synchronization implementations,
not parallel versus serial object updates; representative campaign, combat and
driving gains remain unverified. [Private math](docs/object-private-math-20260915.md)
alone did not improve the earlier kernel-mutex comparison. Both changes remain
available for combined testing. Neither sustained frame-rate target is complete.

A [bounded mutex-wait candidate](docs/object-bounded-wait-20260915.md) replaces
contended sleep/poll attempts with finite waits that wake on unlock. It retains
both workers and all owner-service/critical-section rules. Host memory/race and
emulator switching checks pass. Three physical native-resolution comparisons
regress from about 9.1–9.2 to 7.2–7.3 FPS, with far more contended handoffs. The
existing wait is restored; the candidate stays off. Next: reduce lock frequency
where math inputs and outputs are wholly worker-owned.
A [private point prototype and caller trace](docs/object-private-point-20260915.md)
passes ownership and numerical tests, but finds zero eligible calls in two
emulator views. The trace attributes 98.86% of sampled point calls to the model
update writing `object+0x50`; the rest retain a collision transaction guard.
The prototype requires an explicit build flag and is not installed on hardware.
Next: establish ownership and dependencies for that model-update work before
batching it across workers.

The [combined worker comparison](docs/object-workers-combined-20260915.md) now
tests the current lightweight-lock/private-math build with object workers off,
on and off. Six physical native-resolution trials across two Blood Gulch views
place workers on at 9.12/9.19 FPS versus 9.68/9.66 off: about 5–6% slower.
The previous mutex-backend improvement remains valid, but a net whole-object
parallel gain is not established in these views. Normal and charged plasma shots
complete. The [model ownership follow-up](docs/model-update-ownership-20260915.md)
traces concrete bounds readers and nested collision paths; next is the serial
native hierarchy extraction with explicit inputs/outputs before new scheduling.

The [native child-hierarchy batch](docs/model-hierarchy-batch-20260915.md) is now
implemented as an optional synchronous extraction. Differential host and ARM
tests check outputs, guest state and scheduling; shared publication remains
guarded. Emulator checks pass. Six physical comparisons show no consistent gain
(10.08 FPS either way near the base, 10.82 off versus 10.72 on toward the valley),
so it remains off by default. Remaining quaternion/matrix lock waits lead the
next ownership audit; the existing multicore configuration remains active.

A [private quaternion experiment](docs/private-quaternion-workers-20260915.md)
now avoids the initial shared lock for calculations whose inputs, output and
scratch are wholly worker-owned. Host race/ownership and ARM arithmetic checks
pass; the emulator confirms real admissions and off/on/off restoration. It
defaults off: three native-resolution cryobay comparisons change total FPS by
only +0.34%; reduced quaternion waits move to another helper, while whole object
batches stay near 19 ms.

The [native-resolution query follow-up](docs/query-overlap-native-20260915.md)
finds a larger dependency: three cryobay comparisons improve from 4.691 to 5.762
FPS (+22.83%), saving 39.62 ms/frame by overlapping exact query generations.
The follow-up enables this existing option by default in builds that include
query history, while retaining the multicore work and standard graphics.
Broader gameplay validation and the sustained FPS goals remain open.

The [render-preparation hardware follow-up](docs/hardware-render-preparation-20260915.md)
retains query overlap and measures the existing NEON draw-scan option in three
native-resolution campaign trials. Pooled frame time falls by 1.655 ms (0.96%
throughput gain); the option now defaults on with explicit opt-out preserved.
The separate vertex-preparation worker adds 4.394 ms in its tested view and
stays disabled. These changes preserve exact draw data and standard graphics;
neither comparison establishes sustained 20 or 30 FPS.

The subsequent [gameplay capture](docs/gameplay-cadence-20260915.md) records
camera/AR drops and a periodic hitch, with both object workers active. Hardware
timing confirms that the 60-frame profiling report can stall for hundreds of
milliseconds. The installed report-batching fix reduces the measured Blood Gulch
median report cost from 383.057 to 11.277 ms; one 145.840 ms outlier remains.
This is a report-cost result, not a controlled overall-FPS comparison. An [exact index-reuse prototype](docs/index-reuse-20260915.md)
passes source-mutation, delayed-slot and emulator smoke tests; it remains off
after three physical off/on/off comparisons show no FPS gain. Its controlled
selector remains available; CPU cache validation and miss capture offset the
saved copies in the tested views. Further performance validation uses host
correctness checks and the physical Vita, as requested by the user.

[~] [Vertex preparation work sizing](docs/vertex-work-profile-20260914.md):
indexed checks are selectable in Graphics for ordinary gameplay validation.
An optional diagnostic splits comparison/copy cost by span size; it adds no
profiling calls to ordinary builds. Host checks and campaign rendering in
Vita3K pass. Physical profiling measures 11.4 ms/frame comparing indexed
vertices and 7.0 ms creating snapshots in one fixed campaign view.
The [large snapshot worker experiment](docs/snapshot-worker-20260914.md) now
shares bounded initial copies with idle core 0 and joins before guest execution
resumes. Host memory/race checks pass.

The completed physical snapshot comparisons show no pooled gain: polling was
0.91 ms/frame slower, and event notification was 0.84 ms/frame slower in the
tested cryo-room comparisons. Shared snapshots stay off. The next larger target
is [character pose and scene preparation](docs/pose-job-boundary-20260914.md).
The new [bounded remote phase capture](docs/guest-phase-timing-20260912.md#bounded-remote-capture-september-14-follow-up)
passes host checks and completes three valid windows on both Vita3K and
hardware. The [physical capture](docs/hardware-20260914-bounded-phases.md)
places the scene path at about 110 ms/frame inclusive, including native waits;
the finer 48-scope trace is now validated on hardware. It measures about
17.26 ms/frame inclusive in object updates and 35.90 ms in scene callbacks.
These overlapping regions guide the concurrent-object experiment.

The [combined matrix/object-scan/texture-state comparison](docs/optimization-bundle-20260914.md)
retains the established improvements and tests three additional candidates
together. Three physical cryo-room trials measure 6.768 FPS off versus
6.760 FPS on, with mixed trial directions. The options remain available;
this combination has no demonstrated gain in this room.

[~] September 14 [campaign results and next steps](docs/performance-next-steps-20260914.md):
indexed vertex validation saved 13.9 ms/frame across three cryo-room comparisons,
from 6.165 to 6.742 FPS. It remains opt-in pending broader gameplay checks.
Next: validate additional views, measure remaining vertex work and prototype
larger independent worker jobs with explicit input lifetimes.

[~] September 14 [rendering candidates](docs/hardware-20260914-candidates.md):
physical comparisons of texture binding reuse, native point transforms and
early visibility completion show small or inconsistent gains across views.
Fewer API calls alone did not establish a useful texture-cache FPS benefit.
The separate [exact query-overlap experiment](docs/flare-query-overlap-20260914.md)
helped two Blood Gulch views but not the tested campaign room; it defaults off.

[~] September 14 [native transform math](docs/native-math-unroll-20260914.md):
unrolled scalar VFP calculations preserve 48,896 compiled ARM test outcomes
while reducing accepted matrix/quaternion instruction counts. Hardware FPS
benefit remains unmeasured. Stable 20 FPS is the first milestone toward 30 FPS.

[~] September 14 [hardware rendering comparisons](docs/hardware-20260914-rendering.md):
the model-palette helper did not improve the tested Blood Gulch view. Existing
deferred flare queries saved about 13.3 ms/frame in matched trials, but were
already enabled. The later candidate and campaign reports above distinguish
those existing gains from new experiments.

[~] [Remote hardware testing](docs/remote-testing.md): opt-in LAN controller
input, completed-frame screenshots, log downloads and repeated off/on/off
trials. The [first physical Vita session](docs/hardware-20260913-remote.md)
launched Halo, entered Blood Gulch and collected three complete comparisons.
One-time pairing and foreground launch are required. The
[integrated updater](docs/integrated-updater.md) adds a stable launcher, verified
inactive executable slots and rollback. [September 14 testing](docs/hardware-20260914-updater.md)
confirms repeated physical Wi-Fi installs, including gameplay handoffs, exact
runtime hashes, slot changes and cleared staging state. Access recovered after
the earlier unconfirmed restart; the current runtime is confirmed on hardware.
Remote experiment selection, shutdown diagnostics and display-awake leases are
available. Long-running recovery reliability remains a separate validation task.

September 13 [object-transform hardware follow-up](docs/hardware-20260913-object-basis.md):
the enabled native helper records over two million accepted calls with no guard
rejections. Different views prevent attributing a frame-rate gain. A dedicated
[native-math off/on/off comparison](docs/native-math-benchmark-20260913.md) now
isolates either helper in one session; broader CPU sharing remains unfinished.

September 13 [hardware gameplay results](docs/hardware-20260913-gameplay.md):
the new executable is verified installed. One Blood Gulch worker comparison
measures 15.898 / 16.222 / 15.718 FPS off/on/off; moving views remain slower.
Detailed timings prioritize scene/object CPU work, with no busy-slot waits in
the analyzed gameplay reports. The two new native-math experiments were disabled
by configuration and still need separate hardware comparisons. Stable 20 FPS
and broader workload sharing remain open.

September 13 [port research and worker sizing](docs/ports-and-worker-sizing-20260913.md):
an immediate result dependency rules out moving the object-size calculation to
a worker by itself. An optional, locally validated model-batch size histogram
will help select useful job sizes. No additional worker or hardware gain is
claimed; ordinary helper machine code is unchanged when profiling is absent.

September 13 [bulk guest memory clears](docs/string-fill-20260913.md) replace
per-element work for repeated-byte word/dword fills. Host and Cortex-A9
equivalence checks pass; emulator counters confirm hundreds of eligible calls
per Blood Gulch frame. Hardware frame-time benefit remains unmeasured.

September 13 [constant synchronization](docs/constant-tracking-20260913.md) and
[contiguous capture](docs/constant-window-copy-20260913.md) reduce repeated CPU
copy/comparison work. Host checks, native builds and emulator gameplay pass;
the [native object-basis experiment](docs/object-basis-experiment-20260913.md)
also passes equivalence checks. These changes still need hardware measurements.

September 13 [Halo 2 shader reuse audit](docs/halo2-shader-reuse-20260913.md):
two inspected programs compile through the existing Vita shader toolchain.
Actual vertex bindings and viewport conversion remain unverified; this does
not establish a rendered menu or game compatibility.

September 13 [quaternion reuse experiment](docs/quaternion-reuse-experiment-20260913.md):
an optional exact-value cache passes original/native equivalence and 1,920
Cortex-A9 execution cases. Emulator caller counts show useful reuse alongside
costly misses; hardware benefit is unresolved, so it remains off by default.
Larger scene/object workloads and stable 20 FPS remain the priority.

September 13 [model matrix batch experiment](docs/model-palette-batch-20260913.md):
an opt-in native loop avoids repeated guest-call and intermediate register-state
work. Original-code comparisons, sanitizer tests, 288 Cortex-A9 execution cases,
the native build and emulator Blood Gulch/cryo-room checks pass. Ordinary builds
are unchanged; hardware gains and a larger safe parallel workload remain open.

September 12 [deeper workload audit](docs/guest-workload-audit-20260912.md):
independent model matrix products are a candidate for batching; object callbacks
and hierarchical transforms still require ordered ownership. The private deeper
diagnostic passes emulator collection checks. Before more hardware writes, resolve
the [storage readback anomaly](docs/storage-audit-20260912.md); saves are backed up
and a read-only allocation inspector is prepared.

September 12 [guest-phase hardware result](docs/hardware-20260912-guest-phases.md):
post-loading Blood Gulch windows attribute about 43 ms/frame to selected scene
work and 25 ms/frame to the object-update region, including their unselected
callees. A finer 48-scope diagnostic and batched report writes pass host/native/
emulator validation; executable readback matches after installation. Storage
integrity, safe worker batches and a measured FPS gain remain follow-ups.

September 12 [selected guest-phase diagnostic](docs/guest-phase-timing-20260912.md):
The initial 32 opt-in scopes distinguish selected self time from nested work and
explicit guest handoff time. Source `30deb24` supplied the capture above and has
been superseded by the 48-scope follow-up. More gameplay parallelism and FPS gains
remain open.

September 12 [vertex worker hardware result](docs/hardware-20260912-vertex-worker-results.md):
Blood Gulch at 360p measures 11.294 / 11.627 / 11.395 FPS off/on/off, favoring
the worker by 2.49% in one comparison. Core-0 execution is confirmed; the copy
is too small to balance CPU load. Next, time and isolate larger guest update
and scene-preparation batches. Repeatability and stable 20 FPS remain open.

September 10 [USB vertex worker update](docs/hardware-20260910-vertex-worker.md):
source `21fe9ca` installed and verified after USB remount. Core-0 vertex uploads
are enabled on the test Vita with an off/on/off comparison selected; other
settings are preserved. Hardware performance and stability testing are pending.

September 9 [parallel vertex upload candidate](docs/vertex-upload-worker-20260909.md):
core 0 copies immutable vertex snapshots while the guest records and core 1
submits. A bounded queue and separate CPU/GPU completion protect slot reuse.
Dashboard Performance gains an experimental switch; off/on/off comparison is
available. Hardware gains are unverified and the feature defaults off. Workload
sharing takes priority; independent shadow/reflection controls remain follow-ups.

September 9 [per-game progress maps](docs/game-progress.md): a shared local and
website viewer now reports generated functions, native entry helpers, unsupported
handlers and referenced APIs per profile. Exact matching and behavioral
validation remain unrecorded; translation counts are not whole-game completion.

September 9 [USB model/material update](docs/hardware-20260909-model-prep.md):
source `22331f4` installed and read back successfully; configuration preserved
and USB safely unmounted. Physical gains and the LiveArea wallpaper remain open.

September 9 [model/material preparation candidate](docs/model-preparation-20260909.md):
**Model detail** selects existing character/scenery LODs earlier, with Original
as the default. Unchanged shader colors now reuse their expanded values. Host,
owned-map, original-selector and native checks pass; hardware gains are pending.
These source changes are not in the published September 9 gameplay VPK.
The reported slow VPK installation also motivates a future shader bundle.

September 9 [tester issue 3](docs/tester-issue-3-20260909.md): the 512-entry shader
capture list overflowed into repeated per-draw logging. The bounded-capture fix
passes regression/native checks and is built locally; it is not installed or
released. Tester photos show a30 sky/HUD problems on an unidentified build.
The [Wii Blam archive review](docs/wii-reference-20260909.md) identifies authored
model LOD, visibility/material reuse and offline preparation as follow-ups.

September 9 [gameplay performance candidate](docs/gameplay-build-20260909.md):
development function markers removed and default sampling disabled. Host and
640 linked-ARM comparison cases pass; physical FPS remains unmeasured. Latest
USB logs show zero ordinary Finish calls across 79 timing windows and active
parallel geometry sorting. LiveArea revision 3 awaits a physical display check.

September 9 external tester reports: [performance review and reproduction plan](docs/tester-feedback-20260909.md).
Camera movement jumps near stick center, a flashlight rendering problem and one
stretched shadow remain open; the exact build/settings are unknown. Prioritize
measuring expensive passes and preparation, then investigate existing model LODs
and additional independent CPU jobs. The blue overlay number is Present-path
milliseconds, not GPU frequency or utilization.

September 9 tester follow-up: the [built-in multiplayer profile fix and LiveArea
update](docs/builtin-profiles-20260909.md) pass host and emulator checks; physical
Vita confirmation remains pending. The Windows image tool now accepts UTF-16
manifests, and installation instructions show exactly one `haloce` folder.
[Releases](docs/releases.md) now contain one VPK each, with separate networking
diagnostics. These changes do not establish a hardware FPS improvement.

September 8, 07:00 CDT [verified USB update](docs/hardware-20260908-weapon-menu.md): Graphics gains **Triple buffering** (experimental,
default off), the dashboard gains an offline **GPL-3.0-only About / License** page,
and deferred exact visibility reads are rebased onto the constant-buffer fix.
Native/host checks and private emulator validation pass for those changes;
hardware performance remains unverified. The installed candidate also adds
[weapon stencil, solo-lobby and menu-audio corrections](docs/weapon-menu-20260908.md).
Public-release preparation includes refreshed README/compatibility information,
[Windows instructions](docs/windows.md), [PR requirements](CONTRIBUTING.md),
and a [provenance/history audit](docs/release-audit.md). **Keep the repository private**;
the audit's unresolved content and clean-build gates must be addressed before publication.

September 8 modular foundation: [validated game profiles and separate libraries](docs/modular-architecture.md)
now preserve Halo emission while extracting its hooks from the lifter.
Remaining title-specific HLE and dashboard discovery are still future work.
The separate private website repository targets **xita.dev**. The README now
leads with [VPK installation](docs/installing.md), one-time game data setup and
labeled dashboard/gameplay screenshots. Source-building guides are for developers.

The [modular/site follow-up](docs/progress-20260908-modular-site.md) records the
private source/website pushes and recovery/candidate VPK packaging.

The [September 8 progress summary](docs/overnight-progress-20260908.md) lists what
is installed, what is local and the next hardware test.

September 8 native visibility experiment: the [bounding-box helper](docs/native-bounds-20260908.md)
passes full-context host/sanitizer and linked Cortex-A9 comparisons. Its longer
synthetic paths use about 39% fewer counted ARM instructions; quick rejections
retain a small overhead. It remains off by default with a separate off/on/off
hardware benchmark. The [collected hardware run](docs/hardware-20260908-native-bounds.md)
measured 9.49 / 9.35 / 9.55 FPS off/on/off: no useful gain, so keep it disabled.

September 8 indexed vertex validation: the [new experiment](docs/vertex-references-20260908.md)
checks only groups containing referenced vertex records while keeping owned GPU
snapshots. Native, host, ARM and private Blood Gulch/campaign checks pass. The
campaign sample reduced preparation work in the emulator, while a light Blood
Gulch view showed mask overhead. It remains off by default pending a controlled
Vita comparison; sustained 20 FPS is still unverified.

September 8 optimization research: the [Skate3-Mobile review](docs/skate3-optimization-review-20260908.md)
separates transferable engine ideas from Android-specific choices. The opt-in
[exact draw-scan candidate](docs/draw-scan-20260908.md) passes native, ARM, host
and private gameplay checks; hardware measurement is pending. A separate fused
[vertex-copy experiment](docs/vertex-copy-20260908.md) retains both owned snapshots
while avoiding a second read. Both pass local checks and await hardware measurement.
The indexed vertex comparison is the next hardware performance test. A separate
[rendering candidate](docs/shader-varyings-20260908.md) corrects fog interfaces
and routes additional full-screen effects through their complete shader programs.

September 7, 23:12 update (activity corrected September 8): the [first frame-constant hardware capture](docs/hardware-20260907-frame-constants.md) verifies the installed candidate and 937,344 logged direct bindings with no rejected constants or new crash dump. The user confirms **driving, shooting and looking around** in Blood Gulch; sampled throughput averages **11.00 FPS at 360p**. Repeated diagnostic camera values do not establish stationary play, and this is not a controlled gain over the prior session. Exact rocket/death reproduction still needs confirmation. Visibility waits remain **23.44 ms/frame**; deferred exact result reads are the next performance candidate, rebased onto the installed constant-buffer stage. No hardware files changed during this collection.

September 7, 22:22 update: the [new 360p Blood Gulch session](docs/hardware-20260907-fps-followup.md) averages **10.85 FPS** after loading, with 60-frame windows from **4.8 to 16.3 FPS**. The executable is unchanged; this is not a measurement of the pending crash candidate. The [rocket/death crash analysis](docs/hardware-20260907-rocket-crash.md) found the same GPU fault page at the vertex-ring boundary as the earlier driving crash. The frame-owned mesh constant candidate passed host/private death and respawn checks and was USB installed/verified at 22:33; hardware rocket/death retesting is next, before more performance comparisons.

**Priority (2026-09-05, revised).** The user accepts sustained **20 fps on real Vita
hardware** as the first playable milestone; **25 fps** follows once that is stable.
The latest instrumented a10 run through Keyes and combat logged
**4.2–11.6 fps**, about **7.3 fps** aggregate Present throughput across selected gameplay
intervals (the earlier Blood Gulch baseline was 11–20 fps; the user reports reaching
22 fps on September 5). See the [Keyes hardware report](docs/hardware-20260905-keyes.md)
for sampling limits and the next texture-cache candidate.
The user also reports performance gains from shader fixes. Prioritize
profiling, CPU optimization and frame pacing until representative gameplay meets the
50 ms frame budget at correct game speed. Local multiplayer remains the next feature
goal: the critical path is Phase 1 performance and stability → Phase 3. New dashboard,
multiplayer and plugin features follow the performance gate; campaign-completion and
rendering polish can follow unless a correctness bug blocks performance validation.
The September 5 follow-up makes geometry stability and playable performance the
immediate focus. Defer user checkpoint/resume testing until these allow a practical
play session; existing saves and logs support further local investigation.
The latest low-texture campaign observation is **9–11 fps**; its USB log confirms
`XV_TEX_MAXDIM=128`. This is a different play session, not a controlled A/B result.
The first verified 480p Blood Gulch run reaches **18.1 fps** in a sixty-frame
gameplay window; 49 windows with BSP draws aggregate **11.0 fps**, with slower
sections down to 4.6 fps. The user reports a noticeable improvement. See the
[480p hardware report](docs/hardware-20260905-bloodgulch-480p.md) for settings and limits.
The subsequent [Battle Creek run](docs/hardware-20260905-battlecreek-480p.md), with
Medium textures/Linear filtering/mip smoothing, aggregates **13.7 fps** over about
six minutes of sampled gameplay and reaches **20.6 fps**. Its screenshots confirm
large geometry spikes. Different map/settings prevent a direct comparison.
The subsequent integrated build regressed in Blood Gulch and crashed. Its
[crash report](docs/hardware-20260905-bloodgulch-crash.md) records invalid render
targets and a corrected frame-buffer reuse race; hardware retesting is pending.

**Latest testing direction:** use **Blood Gulch first** to reproduce and fix
shared rendering bugs before returning to campaign-specific work. The user has
reproduced the white sky haze there, and the live emulator view and draw trace
are captured. Prioritize sky/haze, geometry stability, flashlight and other
lighting effects, transparency, and HUD/weapon rendering. Measure performance
along the same route after each correction, keeping resolution and texture
settings fixed. The campaign freeze and resume issues remain tracked for the
later campaign pass. See the [Blood Gulch rendering tracker](docs/bloodgulch-rendering.md).

## Current state (2026-09-05)

**Overnight goal:** finish the current CPU step, then investigate flashlight and
plasma-charge disappearance, remaining geometry spikes/pop-in, campaign camera
recovery and menu resume, followed by black glass/radar and missing effects.
The [draw-preparation candidate](docs/cpu-draw-preparation-20260905.md) passes
      local tests and Blood Gulch emulator checks; hardware performance validation is
pending. Continue the rendering investigations while that validation awaits play.

September 6 follow-up: the user reports AI not shooting after receiving the
pistol. Investigate actual actor firing and projectile/damage creation separately
from effect rendering. The USB session collected on September 6 contains the
older executable and multiplayer maps; it does not establish this campaign bug's
cause. [Fresh bridge material and camera checks](docs/bridge-materials-20260906.md)
are recorded separately.

After the September 6 combined USB update, the user confirms much improved Vita
rendering but little performance gain. The current build's hardware phase timings
remain to be collected; do not infer a CPU speedup from emulator results or core
utilization. The next performance step is to identify the dominant draw-preparation,
render-submission or waiting cost on that build, with stable 20 fps as the target.

- [x] Boots to the main menu on real hardware; profiles persist.
- [x] In-game **Revert to Last Save** restores a checkpoint on hardware (user
      confirmation, September 5).
- [~] Main-menu campaign resume: the [checkpoint file-size correction](docs/checkpoint-file-size-20260906.md)
      fixes a proven ARM argument-layout bug and preserves the reserved file length.
      Halo previously erased its own header after seeing the shorter disk length.
      Native ARM regression and host adapter tests pass. Save and Quit followed
      by a full app restart now resumes the cryo checkpoint through the ordinary
      Campaign menu in Vita3K. Hardware resume and older damaged saves remain open.
      A subsequent [guarded offline recovery](docs/checkpoint-recovery-20260906.md)
      reconstructs one old hardware profile from its matching cache and restores
      the required profile CRC. The normal menu then resumes the later combat
      corridor in first person. Original backups and hardware files are untouched.
- [~] Reticle is visible, per the user. A September 6 Snipers grenade check
      now shows health dropping from four yellow bars to one red bar, shield
      depletion, and the blue shield fill recharging in Vita3K. The debug overlay
      was disabled for clear captures. Hardware HUD validation remains pending.
- [~] Opening cutscene was black on hardware with parts of the HUD visible
      (September 5 screenshot). The September 6 candidate now completes the full
      natural opening locally, showing bridge/hangar scenes and returning to the
      cryo room without logged draw-storage shortages. Hardware cinematic and
      HUD compositing checks remain pending.
- [~] Geometry spikes recur on hardware and now in the emulator (September 5).
      Turning left/right reproduces broken world geometry at a measured 20 fps.
      A trace found guest index lists changing before GPU completion, including
      47 affected draws in one frame. Per-frame index copies now retain those
      triangles. The user confirms the camera-turn spikes look fixed; a new
      trace checked 1,011 draws with zero mutations. The combined build was then
      tested on Vita: spikes initially seemed gone, but recurred less often.
      Hardware geometry stability remains unresolved. Angle-dependent model pop-in (rocks/Warthog) is still open. The existing W-clamp workaround is not a fix.
      A later cryo-pod capture found the display callback overwriting model data
      after exhausting its guest stack. The [callback stack correction](docs/callback-stack-recovery-20260905.md)
      passes one simulated hour of real guest callbacks and a native build;
      an emulator run remains stable beyond 11 minutes, including camera turns.
      Hardware validation remains pending.
- [~] Remaining model pop-in: [frame index capacity](docs/frame-index-capacity-20260906.md)
      exhausted its 128K slots in a10; later cinematic samples need over 277K.
      The final 384K-slot candidate adds 1 MiB total across the two frame buffers.
      Tests and the complete natural opening cinematic pass locally with no
      logged draw shortages; Blood Gulch charge and movement checks also retain
      their draws. Hardware checks continue.
      The [immediate-frame pool](docs/immediate-frame-capacity-20260906.md)
      separately needed 256 KiB after flare rendering was restored. A full
      natural intro now completes locally with no draw/constant shortages;
      its sampled peak is 219,840 bytes. [Conservative flare rejection](docs/cpu-flare-draws-20260906.md)
      avoids 134 of 249 quads and 21,440 bytes of immediate vertex copies in the
      cryo test view. Blood Gulch camera turns retain charged plasma and vehicles.
      Host checks pass; Vita appearance and performance still need measurement.
- [~] Flashlight disappearance: the [resident shader correction](docs/resident-vertex-shaders-20260906.md)
      fixes a confirmed null-handle selection error that dropped model draws
      before GXM submission. Shader-size units and resident addresses are now
      correct. Repeated cryo-room flashlight toggles keep pod bodies visible in
      Vita3K; a traced null-handle switch now submits the intended lit program.
      Hardware flashlight and plasma-charge/Ghost retesting remain pending.
- [~] Black glass: [captured transmission/reflection programs](docs/glass-programs-20260906.md)
      restore transparent cryo observation windows in Vita3K. Three new fragment
      assets are embedded for static/model glass and alternate texture bindings.
      Pod transparency also passes with the flashlight on; camera-angle and
      hardware validation continue.
- [~] Black radar spot: the [zero-alpha blip correction](docs/radar-blip-20260906.md)
      restores the original additive combiner for radar motion blips. Plasma
      charge and lateral movement show a yellow blip without the black spot in
      Vita3K. Hardware and other-character contacts remain pending.
- [~] Plasma projectiles: a September 6 frame-by-frame recording now shows
      normal bolts moving away from the muzzle and hitting the base, plus the
      charged orb in flight and its impact in Vita3K. This extends the charge-glow
      check after the immediate/visibility fixes. Hardware projectile appearance
      and broader trail/smoke coverage remain pending.
- [~] Plant alpha cutouts work in the latest emulator session, per the user.
      Smoke transparency and bloom/glow compositing still need validation and fixes.
      [Four additional captured programs](docs/effects-programs-20260906.md)
      replace missing blur/lighting/blend table entries; broader visual checks continue.
      [Immediate flare draws](docs/immediate-flare-20260906.md) now retain the
      particle-format VS56 quads that the UI-only bridge discarded. [GPU visibility queries](docs/visibility-queries-20260906.md) replace the
      constant one-pixel result. The idle weapon lights and charged plasma orb
      now glow in Vita3K; query completion and resolution scaling pass local
      tests. Hardware appearance and query cost remain unverified.
      [Visibility ID lookups](docs/cpu-visibility-lookups-20260906.md) now check
      the matching slot before scanning the table, preserving arbitrary IDs and
      generation handling. Host and native checks pass; hardware cost is pending.
      [Render-target feedback checks](docs/render-target-feedback-20260906.md)
      now ignore unused texture bindings and check the resolved active samplers;
      the old scheduler failed the new producer/consumer regression.
      The [surface-description ABI correction](docs/surface-description-20260906.md)
      removes a four-byte overwrite and restores 128x128 target dimensions
      previously reported as 0x128. Local viewport, sniper zoom, and campaign
      resume checks pass; hardware validation remains pending.
      A [six-pair coverage follow-up](docs/effects-coverage-followup-20260906.md)
      adds captured alpha-only, flare, filtered-composite and model-lighting
      programs; native compilation and lookup/source-equivalence checks pass.
      The [screen-composite router](docs/screen-composites-20260906.md) keeps
      two captured VS38 passes on the complete renderer even when their inputs
      do not alias the backbuffer; the UI fallback retained only one texture.
      A [grenade-effects recording](docs/grenade-effects-20260906.md) confirms
      flames, smoke and fading translucent dust locally. Its trace found one
      more model-lighting pairing during the flash; two matching variants and
      real-lookup regression checks now pass. Hardware and wider effects remain open.
      The [resumed campaign corridor](docs/corridor-effects-20260906.md) adds four
      captured particle/model-lighting pairings. Native flashlight, camera-turn
      and pistol-shot checks cover all 62 observed pairs without storage drops
      or captured-data mutations; hardware checks remain pending.
- [~] Active camouflage: user confirms it works in the September 5 local emulator
      candidate after texture-mode/DOT shader and background-copy corrections.
      The intermediate black silhouette sampled empty guest backbuffer RAM.
      The user now confirms active camo works on Vita; third-person coverage remains open.
- [~] Blood Gulch sky haze: corrected cube-mode sampling of bound 2D textures;
      the user confirms the sky looks fixed in the emulator and looks good on Vita.
- [~] Impact decals: user confirms marks remain after correcting persistent
      vertex attributes and shader coverage. The intermediate black rectangles
      and then invisible marks were separate failures; hardware retest pending.
- [~] Optional `XV_FRAME_CAP=20` presentation pacing is enabled only in the local
      emulator configuration. Repeated gameplay windows measure 20.0 fps; the user
      finds that rate playable once geometry is stable. This is not Vita throughput.
- [ ] First-person shotgun appears clipped against a nearby wall in the emulator;
      screenshot and draw trace captured. Check viewport depth mapping and clipping
      before attributing this to original-game behavior.
- [~] Sniper scope markings: the [two-texture HUD mask](docs/sniper-hud-mask-20260906.md)
      replaces the one-texture fallback. Markings now disappear while unzoomed
      and display at 2x/10x in Vita3K. Arms are also visible in the fresh baseline
      with earlier fixes. Hardware checks remain pending.
- [~] 480p option implemented: 848x480 rendering with a GPU upscale to 960x544.
      Host checks and Blood Gulch emulator play pass. Installed over USB with Low
      textures selected; the first hardware run aggregates 11.0 fps across sampled
      gameplay windows, reaching 18.1 fps. Details in the
      [480p hardware report](docs/hardware-20260905-bloodgulch-480p.md).
- [x] Original loading artwork and baked-lighting filtering: the user confirmed both
      fixes on Vita hardware on September 5. The earlier blank-screen report came from
      the earlier executable, which lacks the loading fix and its shaders. See
      [latest hardware follow-up](docs/hardware-20260905-followup.md).
- [x] Blood Gulch (solo) playable on the Vita: earlier tests measured 11 to 20 fps
      on foot; the user reports reaching 22 fps on September 5. Duration, scene and
      exact build for that result are not yet recorded. Full canyon visibility,
      Warthog, all weapons, sky, fog, audio.
- [x] Solo Split Screen uses the normal Start Game button and countdown. The user
      confirmed the emulator launch; the [combined build](docs/split-screen-20260905.md)
      was installed over USB at 22:06 CDT. Hardware retesting is pending.
- [x] Pillar of Autumn through the intro cinematic and cryo tutorial; the user reached
      Captain Keyes and subsequent combat on hardware on September 5.
- [x] Bump lighting and reflections correct on terrain and models (GXM cube-map layout,
      8-texel linear texture padding).
- [x] Pad recorder and replay for reproducible runs; frame-time and per-phase timers;
      on-demand draw-call histograms.
- [x] Per-core CPU utilization: kernel idle counters sampled about once a second,
      logged and shown as C0/C1/C2 in the overlay. Host counter tests and emulator
      overlay/unavailable-counter checks pass. The [first hardware run](docs/hardware-20260905-cpu.md)
      averages approximately 6%/23%/78% across selected gameplay samples.
- [~] Identify effective guest-thread placement: the hardware readings contradict
      the assumed core-0 game allocation. Optional thread identity/affinity logging
      is implemented; hardware attribution remains pending.
- [x] Game clock honest: Halo derives frame time from the vblank count, and the runtime now
      delivers exactly 60 vblanks per real second (the earlier 1000 Hz counter made the game
      run fast in heavy scenes and 2x at 60 fps on the emulator).
- [~] Cutscene skip camera: the [3925 address-layout correction](docs/cutscene-camera-xnaddr-20260906.md)
      prevents XNet initialization from overwriting the camera-state pointer.
      Opening-skip now restores the first-person view in Vita3K, using Halo's
      own checkpoint recovery. The old timed `XV_CAM_FIX` override is removed.
      Repeated opening skips and a [fresh Keyes skip](docs/bridge-materials-20260906.md)
      pass locally; hardware validation remains pending. The separate
      `0x51E90` freeze was traced to an incomplete BSP read retaining the preceding
      scene's data; [staged-read recovery](docs/campaign-read-recovery-20260905.md)
      lets the emulator reach Keyes and the hangar.
- [~] Touch zones for Black, White and the stick clicks, plus deadzone and look-sensitivity
      settings, merged; not yet verified on hardware.
- [x] Cached dashboard rendering is deployed; the latest hardware log records
      19.7–19.9 fps, versus the user's earlier roughly 3 fps report.
      Further responsiveness improvements remain possible.
- [~] Pooled offscreen render targets and ordered scene replay implemented; hardware validation pending (see [design](docs/render-targets.md)).
- [~] Audio playback-state and opt-in ad-hoc transport branches integrated locally;
      host tests pass. Two-Vita networking and campaign dialogue progression still
      require hardware validation.
- [~] Shared combiner link cache removes the twelve-variants-per-VS cutoff that
      forced sky shaders to fallback after menu/weapon use. Host checks pass;
      combined rendering/cutscene validation continues. See the
      [integration report](docs/branch-integration-20260905.md).
      The integrated snapshot was [deployed over USB](docs/hardware-20260905-latest-usb.md)
      at the user's request; hardware testing and the cutscene freeze fix remain open.

## Phase 1 — Gameplay Core & Campaign Completion

Goal: the campaign and multiplayer maps play correctly end to end at a stable frame rate,
with input, physics, AI and audio behaving as on the Xbox. Everything later builds on
this: the dashboard launches it, ad-hoc play networks it, plugins hook into it.

### 1.1 Technical objectives

- **Frame pacing.** Implement a high-precision frame pacing loop (30 FPS / 60 FPS toggle)
  using `sceKernelGetProcessTimeWide`. Halo's simulation ticks at 30 Hz; the pacing loop
  must keep the tick cadence fixed while rendering runs at whatever the GPU sustains, and
  replace the current vblank-counter wait (`XV_VBLANK_HZ`) with a deterministic schedule.
- **Input mapping.** Finalize dual-analog acceleration curves, deadzone mapping, and
  sticky-reticle aim assist mechanics. Bind front/rear touch zones for contextual inputs
  (Black/White buttons, L3/R3 thumbstick clicks); today the D-pad stands in for those
  four while the player is in control.
- **Physics and AI execution.** Verify the recompiled physics and AI paths across the
  campaign: vehicle physics at low frame rates, AI pathing and encounters, scripted
  sequences (the cryo-tube exit camera is the first open case). Every divergence from the
  Xbox is a recompiler or HLE bug and is tracked as one.
- **Audio desync fixes (dsound).** The DirectSound HLE (voice creation, stream packets,
  `DSoundVoiceIsPlaying`, mixing) must report playback state truthfully so scripted
  dialogue, music transitions and cinematic timing stay in sync. The integrated
  `dsound-state` branch models buffer and stream playback duration, looping and
  stops instead of returning a constant. Host tests pass; campaign validation remains.
- **Save data.** Serialize checkpoint save states and player profiles to
  `ux0:data/xita/saves/`. The game's own files currently land under `ux0:data/xita/save/`
  through the file HLE; the new layout is a deliberate change and needs a migration.
- **Performance.** Sustained 20 fps is the first milestone, then 25 fps and 30 fps. Use
  hardware profiles (a `--trace-funcs` diagnostic build for guest attribution;
  `XV_PROF=1` alone cannot instrument an ordinary build) and frame timers to select the largest measured cost
  first. Candidates include recompiler code quality (flat memory access instead of the
  per-page table, lazy-flag elision, native float for the x87 paths), texture decode and
  GXM submit on the second core, and per-draw overhead in the D3D translation (constant
  snapshots, program lookup). Preserve guest-memory semantics and gameplay correctness.
  Frame pacing alone does not make an 11–20 fps workload meet the 50 ms budget consistently.

  **September 6 hardware measurement:** the combined rendering/CPU candidate
  records Blood Gulch at 5.0–12.1 FPS, median 8.1, across 66 gameplay windows.
  Indexed draw preparation is about 9.45 ms per frame against a 118.35 ms
  game-side interval; the timings overlap and are not exclusive CPU costs.
  Next, attribute guest execution and separate render submission from completion
  and display waits. A proposed dashboard **Benchmark** action should repeat the
  same scene and report frame-time distributions, core usage and phase costs
  with the build/settings identity. This mode is not implemented yet. See the
  [measurement and benchmark design](docs/hardware-20260906-draw-prep.md).

  **Diagnostic build:** guest-function tracing and separate pump submission,
  previous-frame/target/final completion, display-queue and retirement timers
  are implemented for a new measurement build. Host attribution and GPU-lifetime
  regressions and the native build pass. The exact USB executable resumes campaign
  combat in an isolated emulator, producing function samples and disjoint stage
  reports. Installed over USB September 6 at 08:54 CDT; direct readback and a
  fresh mount verify the executable and 657 unchanged other files. The next USB
  run yields 30 world windows: median 6.5 FPS, with a ground-view window at
  17.5 FPS. Between-target GPU completion waits dominate the pump (85.2 ms
  median versus 3.74 ms submission). A [queued render-pass candidate](docs/queued-render-passes-20260906.md)
  retains final frame completion and passes host, sanitizer and isolated
  emulator checks. Installed and USB-verified at 09:25–09:26 CDT. The
  [next hardware run](docs/hardware-20260906-queued-render.md) has median 8.0 FPS
  (4.3–16.4) and an 82.0 ms pump; changed routes prevent a causal gain claim.
  Intermediate waits move into submission and final completion.
  The flashlight color artifact also occurs with
  the original waits and remains open. See the [diagnostic report](docs/performance-diagnostic-20260906.md).

  **Submission, shaders and visibility:** the subsequent 47-window hardware run
  records median 9.0 FPS, an 80.0 ms pump and 69.1 ms final completion wait;
  changed routes still prevent a controlled gain claim. The
  [three requested changes](docs/render-alpha-visibility-20260906.md) are implemented:
  nested submission-call timing and shader workload counts, 572 alpha-disabled
  shader variants, and a 100-microsecond cooperative delay for incomplete
  visibility reads. Enabled alpha tests retain their original shader; ready
  results remain immediate and GPU generations remain accurate. All 572 shaders
  compile, host/sanitizer checks and the native build pass, and the exact USB
  candidate starts Blood Gulch normally and preserves close-up foliage during
  plasma firing in an isolated emulator. All 45 loaded specializations report
  no discard instruction; geometry-buffer drop counters remain zero in that test.
  Installed over USB at 11:50–11:51 CDT; direct readback and a fresh mount verify
  the executable and 657 unchanged other files, including saves and settings.
  The [first hardware follow-up](docs/hardware-20260906-alpha-visibility.md)
  records median 9.75 FPS in Blood Gulch (44 world windows) and 8.8 FPS in
  Battle Creek (22). One ground-facing Blood Gulch window reaches 22.2 FPS;
  sustained 20 FPS remains unmet. All 68 specialized shader loads have no
  discard instruction. EndScene stalls reach 45.070 ms/frame in a Battle Creek
  window, while median final completion remains about 58 ms on both maps.
  Investigate EndScene resource/pass behavior and actual GPU shader cost;
  changed play routes prevent claiming a controlled performance gain.
  The next [scene-capacity candidate](docs/render-scene-capacity-20260906.md)
  requests four scenes per render target, bounds extra driver memory and falls
  back to smaller allocations on failure or budget pressure. It adds EndScene
  timing and scene counts per target without changing shaders or guest code.
  Host/sanitizer and native-build checks pass. The exact USB executable starts
  Blood Gulch, leaves through pause, then starts Battle Creek in one isolated
  emulator session; all 194 capacity reports show zero drops in that test.
  Installed and USB-verified at 12:44–12:45 CDT, with 657 other files unchanged.
  The [hardware follow-up](docs/hardware-20260906-scene-capacity.md) records
  median 9.65 FPS in 38 Blood Gulch world windows. Median EndScene falls to
  0.606 ms/frame, maximum 1.123 ms, while final completion remains about 59 ms.
  Different routes prevent attributing an overall FPS gain.
  [Additional quality controls and CPU/GPU work](docs/quality-options-20260906.md)
  are implemented: 360p/400p, material detail, lens-flare glow, cosmetic particles,
  decal lifetime/budget, 20/25/30 FPS limits, optional CPU 500 MHz with fallback,
  and extended compressed textures. Native BSP arithmetic, worker-assisted BC
  reordering, redundant state binding removal and safe clear coalescing pass
  host/sanitizer checks. Native and isolated emulator validation pass, including
  a10 Low quality and a live 32-mark decal budget. Installed over USB at 14:00 CDT;
  direct read and a fresh mount verify the executable and 657 unchanged other
  files. The [lower-quality hardware follow-up](docs/hardware-20260906-quality-settings.md)
  confirms 360p and quality controls applied, but median FPS is 8.5 on a different
  route. Final graphics-completion wait is 43.613 ms; the engine interval remains
  115.8 ms. CPU 500 MHz was rejected and fell back to 444 MHz. No overall gain is
  demonstrated; prioritize engine/draw preparation alongside remaining GPU work.
  The [draw preparation, native math and waits follow-up](docs/cpu-preparation-20260906.md)
  implements exact shader-identity reuse and unchanged-state detection, guarded
  matrix/quaternion replacements, event-based frame notifications and bounded
  visibility backoff with a post-wait recheck. A scheduler correction prevents
  stale WaitAll state from waking subsequent sleeps before their deadline.
  Numeric equivalence, shader-state, threaded handoff, visibility and scheduler
  regressions pass, as do sanitizer checks and the native build. Hardware FPS
  impact remains unmeasured; these changes reduce existing work rather than
  moving AI or physics onto another core.
  The exact USB executable passes isolated Blood Gulch and a10 checks, including
  camera turns, firing, pause exit and opening-scene skip to the cryo bay.
  Installed and USB-verified at 15:17–15:18 CDT, with 657 other files unchanged;
  the Vita is safely unmounted. Matrix fallback calls remain common and warrant
  profiling before broadening the guarded replacement.
  The [first hardware follow-up](docs/hardware-20260906-cpu-preparation.md)
  confirms native math/cache/events are active: 39.5% of matrix and 98.7% of
  quaternion calls take the native path, with 66.9% shader identity reuse.
  Shader-state preparation measures 8.2 versus 19.2 microseconds per draw in
  the preceding run. Both use 544p/High, but the latest recording is dominated
  by one view and has more draws; median 6.9 FPS and 98.475 ms final graphics
  wait do not establish a controlled FPS change. Prioritize query/completion
  latency, classification of matrix fallbacks and a fixed-view resolution test.
  The [completion/matrix follow-up](docs/completion-matrix-20260906.md) implements
  waits on submitted query generations with scheduler notifications and separate
  completion-to-resume timing. Exact in-place matrix layouts now use the native
  path; unusual spans still fall back and are classified in the log. An opt-in
  L+R+Select benchmark compares 544p/360p/544p with neutral input, camera checks
  and automatic resolution restoration. Host/sanitizer and native builds pass;
  the final executable completes the three phases in isolated Blood Gulch with
  an unchanged camera. The roughly 20 FPS emulator results are capped and do
  not predict Vita performance. Hardware measurements remain the next step.
  Installed over USB at 16:59 CDT; direct reads and a fresh mount verify the
  executable and 657 unchanged other files. Safely unmounted at 17:00 CDT.
  The [controlled hardware benchmark](docs/hardware-20260906-completion-matrix.md)
  measures 7.389 / 9.211 / 7.451 FPS at 544p/360p/544p with the same camera,
  a 24.1% gain from reduced resolution. Matrix native coverage is now 98.3%,
  and query results resume in about 96 microseconds after completion. Final
  graphics waiting falls from about 67 to 39 ms at 360p, but frame time remains
  108.57 ms. Continue draw preparation/translated routines and GPU pass work;
  notification latency is no longer a large measured cost. Logs are archived
  and the Vita is safely unmounted; 20 FPS remains unmet.
  Actual GPU pass timing and a repeatable benchmark route remain open.

  **September 6–7 overnight pass:** [implementation and validation](docs/overnight-performance-20260907.md)
  cover native FP locals in the measured polygon clipper, same-thread scheduler
  bypasses, cached sampler preparation, and a protected constant fragment for
  depth-only draws. Real waits, texture validation, discard and depth-replacement
  behavior remain. Full-context/memory differential tests, sanitizer checks and
  native builds pass. The final emulator executable completes Blood Gulch's
  resolution test and the a10 cutscene-to-player transition; six sampled frames
  have no draw-data changes before GPU completion. Extended memory is already
  enabled on the installed app; the prior hardware test used about 8.3 MiB of
  its 32 MiB texture pool with no measured texture decodes. Installed over USB
  on September 7; the linked deployment report records verification. Hardware
  [FPS for this pass is now measured](docs/hardware-20260907-overnight-performance.md):
  7.939 / 9.897 / 8.194 at 544p/360p/544p. All four paths run on hardware;
  sampler reuse is about 68% and the depth-only path handles two draws/frame
  in the tested view. Camera/draw counts differ from the preceding build, so
  these results do not establish its speedup. At 360p frames still take 101 ms;
  continue the clipper/draw path and measure common color-writing GPU passes.
  The 20 FPS gate remains open.

  **September 7 CPU comparison pass:** [clipping and palette preparation](docs/clip-palette-20260907.md)
  keep clipper integer registers local and reuse palette hashes after full-byte
  equality checks. L+R+Square compares just these changes off/on/off at the
  current resolution, with camera checks and preference restoration. Native,
  differential/sanitizer and host input/benchmark checks pass; the packaged
  emulator build exercises both paths and both benchmark types. The next
  [hardware collection](docs/hardware-20260907-clip-palette.md) confirms this
  executable and both new paths, with 6.315 / 7.559 / 6.327 FPS in the resolution
  test. CPU off/on/off was not run; its performance benefit remains unmeasured.
  This view has 316 draws/frame versus 199 previously, so the lower FPS does not
  establish a build regression. The 360p phase still takes 132 ms with core 2
  at a median 89% busy. Continue CPU preparation and common GPU pass work. The exact
  candidate also passes a10 opening/skip checks and nine geometry samples with
  no changes before GPU completion. Installed over USB at 08:34 CDT; direct
  reads and a fresh mount verify the executable and 657 unchanged other files.
  USB storage is safely unmounted.

  **September 7 unused texture preparation:** [implementation and validation](docs/unused-texture-preparation-20260907.md)
  skip texture stages unused by all compiled shader variants and fallbacks,
  retaining cube-selection inputs, coordinate scales and live checks for required
  textures. Shader overrides keep all stages. The isolated Blood Gulch comparison
  skips 30.3% of bound texture stages; this is a work-count reduction, not an
  established hardware FPS gain. Host differential/sanitizer and native checks
  pass, as do both emulator benchmarks, cancellation restoration, flashlight/
  movement/pause checks and the a10 opening/skip. Eleven geometry samples show
  no changes before GPU completion; 310 capacity reports show zero drops.
  L+R+Square now compares three changes together: clip registers, palette reuse
  and unused texture preparation. Installed over USB at 11:33 CDT; direct reads
  and a fresh mount verify the executable and 657 unchanged other files. USB is
  safely unmounted. The [12:05 hardware collection](docs/hardware-20260907-unused-texture-preparation.md)
  confirms 31.6% of bound texture-stage preparation skipped and all three paths
  active. Resolution results are 9.604 / 13.166 / 9.711 FPS at 544p/360p/544p;
  the CPU off/on/off test did not run. This view has 123 draws/frame versus 316
  previously, so higher FPS does not establish a code speedup. Graphics waiting
  falls from about 68 to 39 ms at 360p, but frames still take 75.95 ms. Investigate
  common GPU passes as well as CPU draw preparation; controlled CPU savings
  remain unmeasured. The user's standard-settings baseline stays fixed at 544p,
  textures 256, High material/glow/particles and a 20 FPS cap. Its configuration
  is byte-identical to the installed backup; no device writes were made.
  A 12:35 collection records another resolution test (8.261 / 10.507 / 8.465
  FPS, 193 draws/frame). The user confirms pressing Select and is repeating
  with Square for the CPU comparison; no benchmark-dispatch bug is established.
  After each hardware test, state the measured result, next development step
  and whether another user test is needed.

  **September 7 controlled CPU result:** the [13:05 off/on/off test](docs/hardware-20260907-cpu-comparison.md)
  completes at fixed 544p with matching camera and 229 draws/frame:
  **7.079 / 7.065 / 7.204 FPS**. All three switches are verified in the counters;
  the on phase skips 27.0% of texture-stage preparation. It is 1.06% below pooled
  off throughput, smaller than the 1.77% variation between off phases. No FPS
  benefit is established for this bundle in this view. Defaults restore and
  saved standard settings remain unchanged. The next development priority is
  common rendering-pass investigation: final graphics waiting stays near 70 ms,
  versus 6.2 ms submission and 10–12 ms draw preparation (overlapping elapsed
  intervals, not exclusive GPU/CPU costs). Resolve target/program/blend/alpha
  attribution before choosing a shader/pass optimization. No additional user
  CPU benchmark is needed now; the 20 FPS gate remains open. USB is safely
  unmounted and the new result does not evaluate all earlier CPU/threading work.

  **September 7 opaque material candidate:** [implementation and validation](docs/opaque-material-candidate-20260907.md).
  The `154066FD` family now selects its existing alpha-disabled program only
  when uploaded tex0 pixels and the captured alpha test prove it equivalent.
  All uploaded mips are checked; cutouts and unknown resources keep alpha testing.
  Versioned uploads preserve recorded draws when texture content changes.
  Host differential tests, ten sanitizer runs, native build and isolated emulator
  checks pass. The fixed Blood Gulch view removes alpha testing from 54 additional
  draws/frame; a static wall region is pixel-identical off/on/off. Nine sampled
  geometry frames show no pre-completion changes. The new **L+R+Square / GPU** comparison
  toggles only this optimization; earlier CPU defaults stay enabled. Standard
  settings remain fixed. Installed over USB at 14:14 CDT; direct read-back and a
  fresh read-only remount verify the executable and 657 unchanged other files.
  USB is safely unmounted. The [14:27 hardware result](docs/hardware-20260907-opaque-material.md)
  is **6.730 / 7.079 / 6.877 FPS off/on/off**, a 4.06% increase against pooled
  off throughput in this run. It removes alpha testing from 32 additional
  draws/frame; final graphics waiting falls by 5.71 ms and returns in the final
  off phase. Camera, settings, draw counts and prior CPU defaults match. The
  measured texture pool stays at 132 textures / 8,489 KB with no reuploads or
  purges. This is one positive run, not proof of all-scene gains. No additional
  identical benchmark is needed now. Next inspect the remaining 26 material
  draws for conservative uploaded-alpha bounds against the captured cutoff;
  retain the tested path wherever that proof fails. The other common pass's
  alpha depends on two textures/constants, so do not apply the tex0 rule blindly.
  The 20 FPS gate remains open.

  **September 7 synchronization/vertex audit:** [findings and next steps](docs/frame-sync-vertex-audit-20260907.md).
  The display, command/index/immediate, and UI storage are double buffered.
  Final GPU completion protects their reuse; changing ring counts alone cannot
  remove that dependency. The controlled view averages 231 actual GXM calls/frame,
  below the proposed sorting threshold. Mesh vertices already use mapped guest
  streams and shader transforms; index snapshots cost about 1.68 ms/frame and
  prevent camera-dependent reuse corruption. The unresolved cache-clean helper,
  late UI flush scheduling, and shared flush queue need an ownership/coherency
  solution before increasing frames in flight. Added per-frame draw min/max and
  500/800 threshold summaries; host checks and ARM profiler compilation pass.
  This is local audit instrumentation, not a deployed performance fix. The
  uploaded-alpha-range prototype added no eligible draws in the tested emulator
  view and was archived; the dedicated GREATER cutout candidate remains in local
  validation. Next resolve coherency, then test per-slot GPU retirement separately
  from the shader comparison. No unchanged hardware benchmark is needed now.

  **September 7 slot pipeline implementation:** [implementation and validation](docs/slot-pipeline-20260907.md).
  The audit's double-buffer/Finish path is now replaced with three
  independently owned recording/display slots and final-scene fragment
  notifications. CPU submission no longer implies completion. Late UI writes
  publish before frame enqueue; uncached immutable vertex/texture uploads fix
  ownership and the missing cache-clean dependency. This adds vertex copying
  and up to 48 MiB of upload/mirror storage. Native/host/emulator checks pass;
  installed and USB-verified at 16:32 CDT with standard settings unchanged.
  The [hardware result and recovery](docs/hardware-20260907-slot-pipeline.md)
  show **8.254 / 8.097 / 8.408 FPS** single-flight/triple/single-flight: no gain.
  The resolution comparison reaches 10.240 FPS at 360p versus 8.381 / 8.467 at
  544p. A later driving run crashed the GPU; root cause remains unresolved.
  Single-flight is now the default, retaining protected triple slots and
  notification retirement. The recovery executable is USB-verified with standard
  settings unchanged; hardware stability remains pending. The explicit active
  goal is sustained **20 FPS on the physical Vita**. Next reduce measured vertex
  snapshot costs and test material shaders separately. Claude's stale visibility
  and broad half-precision experiments stay disabled pending correctness checks.

  **September 7 vertex snapshot reuse:** [candidate and comparison](docs/vertex-resident-20260907.md).
  Acquired slots now reuse destination bytes only after an exact comparison with
  their cached mirror. Changes still receive immutable current-frame snapshots;
  initialized padding protects stream-length changes. There is no additional
  allocation or wait. Host/sanitizer/native/emulator checks pass. The fixed emulator
  view avoids 269 KiB/frame of uploads, with sampled wall/sky pixels identical,
  but extra comparisons slightly increase emulator preparation time. Vita benefit
  was initially unmeasured. Installed through the existing USB executable allocation; direct
  read-back and fresh-mount checks pass, and USB is safely unmounted. L+R+Square
  now compares this upload path off/on/off with single-flight fixed. Next collect
  its hardware comparison and driving stability evidence. The subsequent
  [hardware comparison](docs/hardware-20260907-vertex-resident.md) is negative:
  **7.404 / 7.306 / 7.569 FPS off/on/off**. It removes almost all upload bytes but
  raises stream preparation from about 7.1 to 9.9 ms/frame. Reuse is now disabled
  by default in the next candidate. Test faster exact comparisons separately;
  the user confirmed no driving in this run, so crash clearance is still open.

  **September 7 native comparison candidate:** [implementation and validation](docs/vertex-compare-20260907.md).
  Added bounded NEON equality for cached vertex spans, with residency disabled.
  44,026 native ARM calls, host/sanitizer tests and emulator rendering checks pass.
  The helper executes fewer instructions, but emulator stream time increases;
  Vita performance is unmeasured. L+R+Square compares scalar/NEON/scalar with queue,
  residency and graphics settings fixed. Retain only on measured hardware merit.
  Installed over USB at 19:00 CDT; direct read-back and a fresh read-only mount
  at 19:05 verify the executable and all 1,654 other checked files. Standard
  settings are preserved and USB is safely unmounted. Next collect this distinct
  comparison, then check the previously reported driving crash separately.

  **September 7 flare dependency audit:** [source and private runtime evidence](docs/flare-query-dependency-20260907.md)
  locates 59 recorded commands in Blood Gulch and 93 in the cryo bay before the
  next visibility-query pass. Moving the entire wait only to the first append
  reaches six/one commands; useful deferral needs retained old metadata, exact
  result generations and guards before cache-identity resets and query reuse.
  The reset is observed during firing and campaign. Brightness arithmetic passes
  161,440 sanitizer differential cases. The [bounded deferred-result prototype](docs/flare-defer-20260907.md)
  now implements the guarded schedule, with differential batch, caller-liveness,
  query-ordering and competing-fiber tests passing. Native/private runtime
  checks pass. One fixed-view emulator cryo comparison gives
  **13.543 / 14.445 / 13.406 FPS** eager/deferred/eager (about +7.2%); Blood Gulch
  remains at the 20 FPS cap. These are not Vita gains. The installed
  vertex-comparison executable and standard settings remain fixed; a 20:21
  read-only USB check finds the same old log, and USB is safely unmounted.

  **September 7 full index-range audit:** [captured ranges and cache implications](docs/index-range-audit-20260907.md).
  Unreferenced leading vertices are 23.0% of requested stream bytes in one Blood
  Gulch view, but only 3.2% in another and 2.9% in the cryo bay. These are request
  totals before cache reuse, not measured savings. Keep blanket rebasing lower
  priority. The opt-in diagnostic, independent native probe and three ownership
  checks pass; no hardware changes. Next audit decoded RGBA texture uploads,
  which remain linear while native BC uploads already use GXM swizzling.

  **September 7 decoded RGBA swizzle candidate:** [implementation and checks](docs/rgba-swizzle-20260907.md).
  The opt-in path preserves texels, mip policy, opacity and immutable uploads;
  both layouts can coexist for comparison. Host/sanitizer tests and private
  Blood Gulch/campaign checks pass, including static-region pixel equality.
  Emulator comparisons remain effectively unchanged (Blood Gulch capped at 20,
  cryo 13.703 / 13.628 / 13.546 FPS). Vita texture-cache performance is unmeasured.
  Candidate remains separate and uninstalled; first collect the pending installed
  vertex-comparison result. No claim of physical 20 FPS or driving stability.

  **September 7 remaining-alpha audit:** [captured upload bounds](docs/alpha-range-audit-20260907.md).
  Three Blood Gulch captures leave 28/20/20 material draws alpha-tested; all use
  uploaded alpha ranges 0..255 against GREATER 127. The cryo view leaves none
  in this family. No extra whole-texture interval proof applies. Keep this
  instrumentation out of normal builds; next validate the existing dedicated
  GREATER shader, which preserves the cutoff. Installed build/config unchanged,
  with no new vertex-comparison result in the read-only USB check.

  **September 7 new vertex-comparison hardware result:** [measured phases](docs/hardware-20260907-vertex-compare.md).
  Stream preparation falls 6.755 / 5.599 / 6.704 ms; whole-game throughput is
  7.003 / 7.241 / 7.145 FPS (about +0.17 FPS / 2.36% versus pooled off, with drift).
  The user's report of no noticeable improvement is consistent with that small
  result. 360p reaches 8.660 FPS and reduces query waits from about 23 to 6 ms;
  substantial CPU work remains. Prioritize the prepared exact deferred-query
  candidate, preserving standard settings and the single-flight recovery queue.

  **CPU implementation:** large texture conversions now split between a core-0
  worker and the guest thread, with disjoint output ranges and a completion join.
  Small/cached textures stay on the existing path. Host equivalence, lifetime and
  failure tests pass. The user reports 8–15 fps after deployment, with about 4 fps
  while driving a Warthog and drops during AR fire. Collected campaign logs show
  late 5.2–5.3 fps windows, zero texture decodes and core 2 mostly saturated.
  Physics/AI remain serialized. See the [hardware follow-up](docs/hardware-20260905-cpu-worker-followup.md).
  Continue with the measured guest hot paths, including draw preparation and
  the visible-triangle sort. The [new geometry worker](docs/cpu-geometry-worker-20260905.md)
  passes host equivalence and two/three-core worker tests. Native builds and
  emulator gameplay pass. The [first hardware test](docs/hardware-20260905-geometry-worker.md)
  confirms core-0 sort jobs, but only about 20 ms total helper work in the recorded
  windows; core 2 remains about 89% busy. No overall FPS gain is established. See the
  [texture worker report](docs/cpu-texture-worker-20260905.md).

  **September 5 direction:** treat Blood Gulch and a10 as separate performance
  cases. A 22 fps scene takes about 45.5 ms per frame, leaving 5.5 ms to remove to
  reach 25 fps in that scene. Profile rendering alongside guest execution: mesh
  draws currently search the shader-pair table, bind programs/state and upload
  uniforms per draw. Measure redundant submissions, texture re-decodes and the
  cost of individual shader passes before selecting changes. CPU-side rendering
  overhead and GPU shader cost both belong in the investigation. Existing cached
  shader links and constant snapshots should be retained. The reported 22 fps
  does not yet establish sustained performance across a route.

  **Texture cache correction:** selected-mip hashes were compared against level-0
  bytes, repeatedly decoding unchanged large textures. Validation and file-read
  invalidation now use the selected mip's address. Host tests reproduce the old
  failure and pass the correction; hardware FPS impact remains unmeasured.

  **Dusklight/Aurora reference:** the [September 5 code review](docs/dusklight-aurora-review.md)
  identifies frame-owned vertex/index storage, state-change tracking and compatible
  adjacent draw merging as useful techniques. The Vita-specific backend is private;
  findings come from its public upstream dependencies. Prioritize buffer lifetime
  and visibility for hardware geometry, then measured draw preparation costs.
  Resolution scaling is conditional on GPU cost; it does not reduce guest CPU work.

  **Acceptance:** start with a repeatable five-minute gameplay route in Blood Gulch
  on real hardware, including movement, looking around and effects. Return to the
  a10 cryo bay and campaign validation after the shared rendering corrections.
  Expand coverage to combat, vehicles and additional maps as they become playable.
  Compare the same inputs, configuration and hardware clocks before and after each
  optimization. Report average fps, slowest rolling one-second fps, and p95/p99 frame
  times, with profiler overhead measured separately. Each route must sustain at least
  20 presented frames in every rolling second of active gameplay; report loading
  separately and include gameplay stalls. Verify correct game speed, input, audio and
  rendering. An average above 20 fps alone does not pass. Multiplayer must meet the
  same requirement on both Vitas once available.

### 1.2 Rendering completion

Kept here because these are engine work, not frontend work:

- [ ] Implement single-pass tangent-space normal/bump mapping for terrain and models
      (replacing Halo's original multi-pass sequence: lightmap × bump, specular,
      base × detail with destination-colour blending).
- [ ] Add dynamic point lights (flashlight) and shadow projections.
- [ ] Optimize transparent particle systems (plasma bolts, smoke, explosions) using
      hardware alpha-to-coverage.
- [ ] Implement screen-space post-processing passes (shield flashes, scope overlays,
      night vision filters). Depends on real offscreen render targets.
- [ ] Alpha test (discard in the generated fragment programs driven by the NV2A
      alpha-test state).
- [ ] Compressed-texture mip chains on by default once verified on hardware.

### 1.3 Hardware-specific considerations

- **Memory.** The runtime runs in extended memory mode (`ATTRIBUTE2=12`, about 109 MB
  extra) and still hosts a 64 MB guest physical space, the recompiled code, the decoded
  texture pool and GXM buffers. Every new feature must budget its memory; the texture
  pool already purges under pressure.
- **CPU and rendering.** Game execution and the render pump already have separate
  threads. Hardware logs show high game-side elapsed time and variable pump cost;
  those overlapping timings do not establish GPU shader cost. Profile guest
  execution, CPU draw preparation, texture conversion and GPU work independently.
- **GXM rules learned the hard way.** Cube maps reserve a full mip chain per face and
  align faces to 2 KB; linear textures pad rows to 8 texels; there is no fixed-function
  alpha test. New rendering work must respect these or reproduce the class of bugs fixed
  this week.
- **Input.** sceCtrl in wide analog mode; front and rear touch via sceTouch; no Black,
  White or stick-click buttons exist physically.

### 1.4 Milestones

1. Frame pacing loop merged; 30 FPS mode holds the tick cadence on hardware.
2. Input curves, deadzones, aim assist and touch bindings merged; a settings block in
   `xita.cfg` exposes them.
3. Audio: campaign scripts that wait on dialogue advance correctly through a10 and a30.
4. Every campaign level from a10 to d40 loads, plays and reaches its next level on
   hardware; every multiplayer map renders.
5. Saves and profiles under `ux0:data/xita/saves/`, with migration from the current
   layout.
6. Performance gate: sustained 20 fps or more, first in Blood Gulch and then the a10 cryo bay on
   hardware, meeting the acceptance criteria in §1.1 before new feature work proceeds.

### 1.5 Code-review fixes (2026-09-04)

- [~] **P1 — Cross-page guest-memory accesses.** SSE, x87 and string helpers now split
      copies at guest-page boundaries in [recomp/xv_x86rt.h](recomp/xv_x86rt.h) and
      [recomp/xv_x86rt.c](recomp/xv_x86rt.c). Host regressions cover loads and stores
      across separately committed pages, including overlapping string copies in both
      directions. Audit follow-up: scalar `X_M16/32/64/F32` lvalues and HLE uses of
      contiguous `X_G` pointers still need page-aware handling. Gameplay verification
      remains open.
- [~] **P1 — Shader fallback sampler mismatch.** [xv_d3d.c](runtime/xv_d3d.c) now uses
      the cube mask only when the combiner shader was selected; heuristic fallbacks
      bind 2D textures.
      Verify forced link failure and cache exhaustion on Vita hardware.
- [~] **P2 — Reclaim small kernel allocations.**
      [recomp/kernel/xk_mem.c](recomp/kernel/xk_mem.c) now tracks allocation lengths
      in 64-byte units and reuses freed spans, including adjacent blocks. Metadata costs
      96 KB of host memory. Host regressions pass 100,000 allocate/free cycles, mixed
      sizes, fragmentation, full-pool exhaustion and recovery; hardware soak pending.
- [~] **P1 — Stale shader overrides during upgrades.** The connected Vita's installed
      shaders matched the repository, but 67 device-compiled overrides differed and
      predated fixes including bone-index bounds and depth handling. [xv_shader.c](runtime/xv_shader.c)
      now prefers packaged shaders; `XV_SHADER_OVERRIDE=1` explicitly enables development
      overrides. Host tests verify both precedence modes and missing-file fallbacks.
- [ ] **P1 — a10 near-plane stretching on hardware.** The user reports nearby surfaces
      stretching across the screen while moving or looking around (2026-09-04); screenshots
      and the Vita log were inspected. Retest with packaged shaders, then isolate clipping
      for triangles crossing the camera plane. The existing `XV_WCLAMP` experiment is
      disabled by default and is not a verified fix. Keep this separate from the stale
      shader issue until hardware testing establishes the cause.

Run the memory regressions without game data using `make -C recomp/host test`.
With Vita SDK headers installed, run shader-loader regressions using
`make -C recomp/host test-shaders`.
The full Vita build and host regressions pass, including AddressSanitizer and
UndefinedBehaviorSanitizer checks (LeakSanitizer unavailable in this environment).
`ux0:/xita-20260904-roadmap-test.vpk` was installed and tested on the connected Vita.
That a10 run reproduced 4–5 fps, geometry spikes and failed resume. The newer
[follow-up candidate](docs/hardware-20260904-followup.md) has unresolved save failures
in its experimental signing path. Signing is now opt-in while loading and profiling
are tested with the earlier unsigned-profile behavior. `ux0:/xita-20260904-icon.vpk`
changes only the installed build's app icon. The [September 5 follow-up](docs/hardware-20260905-followup.md)
records the verified installed build and the staged
`xita-20260905-loading-profile.vpk` diagnostic package. It passed emulator loading
and existing-profile checks; the user subsequently confirmed loading artwork and
baked lighting on hardware. Geometry and performance validation remain open.
The subsequent [Keyes/combat run](docs/hardware-20260905-keyes.md) confirms that
diagnostic executable was installed. The subsequent [CPU-monitor hardware run](docs/hardware-20260905-cpu.md)
verifies installation of the selected-mip cache correction and CPU overlay. It
records much lower texture-decode cost, with a different route and no controlled
FPS comparison. Core 2 carries most measured CPU load; identify the executing
threads before assigning more work to a supposedly unused core.

## Phase 2 — Custom Xita Dashboard & Frontend UI

Goal: a native launcher that replaces "boot straight into the game" with a home screen,
settings and control remapping, styled after the original Xbox dashboard.

### 2.1 Technical objectives

- **Launcher.** A 3D green-matrix, original-Xbox-style front end rendered with GXM: the
  glowing green tubes and animated grid, and a menu of the installed **games** (Halo:
  Combat Evolved first, Halo 2 next, each with its own recompiled engine and data under
  `ux0:data/xita/<game>/`), their content (campaign, maps, saves), and hand-off into the
  selected engine. The launch mechanism (separate installed application or a supported
  module boundary) must be designed and tested before promising an in-process switch.
  The dashboard is Xita's multi-game home
  screen, not a Halo-only menu.
- **Settings overlays.** In-game and launcher overlays for the runtime's tunables:
  frame pacing mode, look sensitivity and curves, aim assist, texture options, the debug
  knobs that stay useful (frame-time overlay, profiler). These replace hand-editing
  `xita.cfg`.
- **Touch control remapping.** A visual remapper for front and rear touch zones and the
  D-pad substitutions, saved per profile.
- **Content discovery.** Scan `ux0:data/xita/` for the game image and maps, validate
  them, and report clearly when the user's own copy is incomplete. No game content ships
  with Xita.
- **Game selection (requested September 5, reaffirmed September 8).** Add an installed-game selector before
  Launch Game, remember the last selection, and load that game's settings and engine.
  Show only runnable installed games; additional Xbox games need their own compatible
  recompilation and validation. Halo remains the first supported game.
  Define a versioned manifest with game ID, display name, supported executable
  revision, installed engine/title ID and data location. Keep configuration and
  saves separate per game; remember the selection, validate missing/incompatible
  installations, and allow only a tested launch target. Display names alone must
  not select Halo-specific hooks for another title. Game artwork needs provenance.

### 2.2 Hardware-specific considerations

- The dashboard shares the GXM context and memory with the game; it must release its
  resources before the engine starts and re-acquire them on return.
- Overlays drawn during gameplay compete for the same frame budget; keep them to a few
  draws and no per-frame allocations.
- LiveArea and the Vita's own UI conventions (Circle/Cross confirm, touch scrolling) apply
  in the launcher even if the visual style is Xbox.

### 2.3 Milestones

1. **Initial launcher implemented:** Xita boots to a settings dashboard with a
   prominent Launch Game button that opens Halo's normal menu. It has Graphics,
   Audio, Controls and Display pages and persists only the setting edited. Host
   tests and emulator hand-off pass. The user confirmed the dashboard on hardware,
   but initially reported roughly 3 fps. Cached CPU drawing and faster spans pass
   host/emulator checks; the subsequent hardware log measures 19.7–19.9 fps.
   Expanded graphics settings include texture caps, filtering, mip smoothing
   and the hardware-tested 480p resolution option.
   Eleven visual controls share a scrolling Graphics list, including
   effects, frame limit and extended compression. CPU clock stays in Performance.
   Host/native and isolated emulator checks pass; installed and USB-verified
   September 6 at 14:29–14:30 CDT with settings, saves and other files preserved.
   Multi-game discovery and direct campaign/map selection remain later work.
   September 8 adds a twelfth Graphics control for experimental triple buffering
   and an About / License page in the local candidate; these are not yet a
   hardware-validated release.
2. Settings overlay reads and writes every documented tunable.
3. Touch remapper with per-profile persistence.
4. Return-to-dashboard from the in-game pause menu without a crash or leak.
5. Game selection: discover installed engines/content, remember selection, and launch
   the selected game with its own settings.

## Phase 3 — Ad-Hoc Multiplayer & Co-Op

Goal: local versus and campaign co-op between Vitas over ad-hoc Wi-Fi, by mapping Halo's
System Link networking onto `sceNetAdhoc`.

### 3.1 Technical objectives

- **Transport mapping.** Halo's XNET and Winsock calls (the current loopback in
  `xk_net.c` serves the in-process server and client) now have an opt-in
  `XV_NET_ADHOC=1` transport: UDP maps to PDP and TCP to PTP. Discovery uses Halo's
  broadcasts over PDP plus the ad-hoc peer list; guest addresses map to peer MAC
  addresses. Host transport tests pass; two-Vita discovery and gameplay remain
  unverified. See [transport details](docs/adhoc.md).
- **Session flow.** System Link lobby, join and start over ad-hoc, with the existing
  force-start hack retired once real peers can join.
- **Campaign co-op.** Halo's Xbox build carries the co-op path for split-screen; System
  Link co-op is the same simulation with a second player over the transport. Determine
  what the 3925 build supports and where the runtime has to fill in.
- **Bandwidth and latency.** Halo assumes 100 Mbit LAN; ad-hoc Wi-Fi is far below that.
  Measure the per-tick packet budget and apply the game's own bandwidth settings.

### 3.2 Hardware-specific considerations

- `sceNetAdhoc` requires the ad-hoc mode initialisation sequence and a fixed local port
  range; power management can suspend Wi-Fi on the Vita and must be held off during a
  match.
- Network play needs separate pacing and prediction tests on both Vitas; a solo
  20 fps result does not establish multiplayer synchronization or responsiveness.
- Memory: the network path must not grow the guest heap; buffers live on the runtime side.

### 3.3 Milestones

1. Two Vitas discover each other and complete a System Link lobby over ad-hoc.
2. Blood Gulch slayer between two Vitas for a full match.
3. Campaign co-op through a10 between two Vitas.
4. Session recovery: a dropped peer returns the host to the lobby cleanly.

## Phase 4 — Plugin Architecture & Community Modding

Goal: let the community extend Xita without rebuilding it: runtime plugins with defined
hooks, and custom map content loaded from the user's data directory.

### 4.1 Technical objectives

- **Plugin hooks.** A stable C ABI for dynamic plugins loaded from `ux0:data/xita/plugins/`
  (Vita `suprx` modules), with hooks at the points the runtime already owns: HLE entry
  and exit, frame begin and end, draw submission, input polling, file open. A Lua
  binding on top for scripted mods, with a bounded interpreter budget per frame.
- **Custom map loading.** Load `.map` cache files from `ux0:data/xita/mods/` alongside
  the user's original maps, with tag validation against the 3925 tag layout, and expose
  them in the dashboard's content list.
- **Safety.** Plugins cannot touch game data outside the mods directory, cannot ship game
  content, and are sandboxed from the runtime's own state except through the hook API.

### 4.2 Hardware-specific considerations

- Dynamic modules on the Vita require the taiHEN/`sceKernelLoadStartModule` path and
  memory for each module; the plugin budget must be visible in the dashboard.
- Lua on the game thread costs frame time; the interpreter runs with a fixed instruction
  budget and yields.
- Custom maps built for the PC release differ from the Xbox cache layout; only Xbox-format
  maps are in scope until a converter exists.

### 4.3 Milestones

1. Plugin ABI documented; a sample plugin logs frame times through the hook API.
2. Lua scripting with the same hooks; a sample script adjusts input curves live.
3. A custom Xbox-format `.map` loads from `ux0:data/xita/mods/` and appears in the
   dashboard.
4. Plugin and mod management in the dashboard: enable, disable, reorder, report errors.
