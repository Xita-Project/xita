# Codex continuation — September 24

Read `codex-handoff-20260924.md` for Claude’s preceding work. Physical status confirms perf208 / 427df56+ still installed. Goal remains 20 FPS in a30; not achieved.

## Material native integration

Merged work/native-70110-20260924, preserving the effects Makefile/runtime integration and both report hooks. A separate retained candidate lives in `../material-native-candidate/build-x87`, copied from overlap-candidate/build-x87 (perf209). Installed the native using tools/install_native_70110.py; 72 tapped sites. Both build-command.json and make-vars.txt enable XV_NATIVE_70110=1; runtime default remains Off. Candidate version perf210. Build started; do not assume completion or installation.

Fresh Pi validation compiled from merged native source and the retained stage’s translated body, Cortex-A9 Thumb, guest -Os, native -O2, thread page table and render view. On Pi cores 0/1: 20,000 differential cases, 12,000 concurrent fast-mode runs, 4,000 concurrent verify-mode runs; zero mismatches. Includes 1,360 page-crossing-window cases. These are deterministic callee/HLE stand-ins, not full-game or Vita validation. Private logs: differential.log, threads-fast.log, threads-verify.log in the candidate directory. Pi artifacts isolated at ~/xita-codex-ce-20260924.

Before hardware mutations, read-only FTP capture saved 15 udata/tdata files (7,376,346 bytes), plus configuration copies, under candidate/save-backup-20260924; SHA256 manifest included. No saves were replaced and no new campaign launch has been issued.

## Candidate qualification

Perf210 full Vita build completed (return code 0), and linked ELF contains both material and effects natives. Code-only package produced from perf209 package, retaining every other entry byte-for-byte; only game-a.self and boot-game.txt changed. Executable 34,724,782 bytes, SHA256 851f54474c78f2c860622d9ce1485ec726260d68aee72bb933bd85e9f56cc0f0. Existing asset contract 775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897 retained. Neither native’s fast mode is enabled by default.

Fresh host ASan/UBSan run found fixture-only leaked result/snapshot allocations; added cleanup at normal test exit and reran: 2,000 cases, 134 page-crossing cases, zero mismatches, no sanitizer errors. No production native change for this fix.

Predeployment screenshot shows actual a30 outdoor gameplay on perf208. Uploading perf210 WITHOUT --apply first; do not treat upload as installation. Hardware verification and speed measurement remain outstanding. Keep-awake refresher running through tools/vita_remote.py lease 3600 every 300 seconds, retries every 30 seconds; no vitacompanion nosleep/press calls.

## Hardware staging and pause-menu obstacle

Upload finished: staged-update.log confirms perf210 executable hash verified, restart_requested false. Perf208 remains running. Attempted Save and Quit before applying: Start opened pause menu; three D-pad Down presses followed by Cross resumed gameplay rather than selecting Save and Quit. Reopened and tried three left-stick down pulses; screenshot still selects Resume Game. Controls released, game left at pause menu. Asked user whether physical controls can Save and Quit, or whether to restart without saving. Do not apply/restart while this save-preservation question is pending. Current disk backups predate this attempted Save and Quit; no fresh checkpoint persistence claimed.

The fetched perf208-before.log covers outdoor play, not a stationary pod baseline, and was fetched during update staging. gpu-summary.py blindly labels later windows pod; do not interpret its 92.2 ms aggregate as an equivalent pod measurement. Screenshots and settings/state must qualify future comparisons.

## Deferred queue placement experiment

Added off-by-default XV_REC_QUEUE_PADDED (build-time 0/1). Separates producer head/metadata, consumer tail, and shared wake state into 64-byte-aligned groups, retaining all queue operations and memory ordering. Static assertions check placement. Only runtime/xv_d3d.o gets the flag; a content stamp invalidates it on toggles. This addresses a possible cache-traffic cost, not a proven performance bottleneck. No hardware stage/package includes this experiment yet.

Host and Cortex-A9 Thumb binary on Pi cores 0/1 each passed four 150,000-record stress configurations (4 KiB to 1 MiB rings, batching, verify-only records, random drains, full-ring waits and wraps). Make selector accepts 0/1 and rejects 2/empty/multiple values. TSan stress run still active at documentation time; no passing claim yet. Private logs under ../queue-layout-candidate. Physical Vita still perf208, staged perf210 remains unapplied pending save preservation.

## Queue sanitizer result and isolated test-save selection

Padded queue TSan run completed successfully: four 150,000-record configurations, no race reports. Still no measured speed gain or hardware deployment.

Added developer XV_TEST_SAVE=<slot> startup selection. A nonempty slot must contain only ASCII letters/digits/hyphen/underscore, at most 32 characters, and resolve to an existing directory under ux0:data/xita/test-saves/. Invalid/missing test roots stop launch; never fall back to player saves. Unset/empty keeps the normal save root. The existing xk_init routes udata/tdata/cache beneath the selected root; a future hardware test must verify actual file paths. No existing profiles copied or changed by this code. tools/test_test_save.py extracts the production selection block and checks accepted roots, traversal/bad names, missing directories, default path, and no guest boot on rejection under ASan/UBSan. Pass. Not included in staged perf210 and not yet Vita-compiled.

## Perf211 ready for isolated hardware verification

Cloned perf210 stage to ../isolated-save-candidate/build-x87 and applied only the isolated-save runtime/header changes. Full Vita build passed. New code-only package xita-perf211c.vpk retains perf210 assets, changing game-a.self and boot-game.txt only. Executable 34,725,382 bytes, SHA256 468598e9483595e09bc45f4ce452a217b4511e3fa01bcd80d5916cdd8b95dba7. Queue padding remains absent from this retained candidate. Not uploaded or installed.

Prepared ux0:data/xita/test-saves/a30-perf211 using 15 files from the captured perf208 disk-save backup, verifying each uploaded file by a fresh FTP read and SHA256 comparison. This is not the unsaved in-memory checkpoint. Normal save files untouched. Set XV_TEST_SAVE=a30-perf211 via remote env before Launch on perf211, and verify the logged save directory before interpreting any campaign run. Still awaiting the save-and-quit user response before restarting current perf208.

## Pause navigation investigation

Read-only Vita pause-investigation.log reports ui_map 0, paused 1, menu 004E9E20, extras 0 repeatedly. Thus the observed stuck selection is not explained by the normal gameplay D-pad-to-crouch/zoom mapping staying enabled. This does not prove delivery of each short remote pulse. No further Vita inputs/restarts issued.

Started Pi harness-fin2 with a fresh temporary save through existing pirun.sh, cores 0/1, tag codex-menu-20260924, duration 180 seconds, native70110=2, save logging. Input schedule enters a30 then at frame6000 Start, Down at6060/6120/6180, A at6240/6360. First poll confirmed frame363; not yet known whether the run will reach menu events. Preserve this limitation when inspecting the result; a timeout before6000 is not a reproduction. Private output in native-70110-work/pi-runs after script completion, remote ~/xita-70110/runs/codex-menu-20260924.log.

Pi menu run codex-menu-20260924 finished at its planned 180-second timeout (rc124), last complete frame4260, 71 report windows. It never reached the input at6000; this is NOT evidence about pause navigation. Started corrected tag codex-menu2-20260924 using the same isolated harness/setup, Start at3000, Down3060/3120/3180, A3240/3360; 180-second timeout. No Vita changes or input. Await actual event/state results before diagnosis.

Corrected Pi run codex-menu2-20260924 completed (planned timeout rc124, 72 reports, frame4320). Pause opened at scheduled input (b2=1 active=0 by3120). Following A inputs, frame3362 purged textures and wrote/read the temporary cache/savegame.bin; later windows show active gameplay again with tick counter reset. No verified return to main menu or profile Save-and-Quit result. This establishes menu actions occurred in the Pi harness, not that Vita input delivery is correct or that the exact same menu selection was exercised. Do not automate the same unverified selection sequence on the user’s saves.

## September 25 — user saved and quit; hardware work resumed

User explicitly confirmed physical Save and Quit and requested autonomous work while away. Captured a fresh 15-file udata/tdata backup in ../isolated-save-candidate/save-after-quit-20260925 before updating. Applying perf211 via verified code-only updater; deploy-20260925.log receipt pending. Previous restart blocker is cleared. Keep-awake refresher remains active. Do not use earlier test-slot files as the newest player checkpoint: they are deliberately the preceding isolated test baseline.

## perf211 installed and hardware native verification

Updater completed: executable 468598e9483595e09bc45f4ce452a217b4511e3fa01bcd80d5916cdd8b95dba7 verified, slot1 boot confirmed; status reports perf211. Startup explicitly confirms isolated save root ux0:data/xita/test-saves/a30-perf211. Normal user saves were not used by this run.

Ordinary scripted a30 launch reached the lifepod (verify-a30.png). Combined mode1 checked 50,689 material calls / 3,083,712 tapped segments and 1,021,677 effects calls with zero reported mismatches. Eleven effects calls were excluded as callee-diverged: the 80360 texture-load callee changed cache residency between passes. This is not verification of those eleven calls. Zero-mismatch coverage is for this observed scene/run, not all gameplay. Verification deliberately runs both paths and its frame times are not performance results. Private receipts: isolated-save-candidate/perf211-verify-complete.log and verify-summary.json.

Started perf211-fast-20260925: same isolated root, material mode2, effects mode2 with default five fast functions explicitly listed, deferred recording off, scene/HLE timing off. Await settled scene-qualified hardware measurements. Keep-awake lease refresher remains running.

## Native fast-path result and next scheduling trial

Stationary lifepod confirmed by fast-scene.png. An intermediate 20-window sample averaged 73.875 ms total frame (13.54 FPS), range 70.0–81.8 ms. The final retained capture's last 20 60-frame windows average 76.96 ms (12.99 FPS), range 70.7–89.6 ms; fast-summary.json describes that final sample. The variability reinforces that no whole-frame speedup is established. These are window means, NOT individual-frame percentiles. Helper CPU examples are 57–58 ms, but total frame remains close to the historical ~74 ms baseline. No sustained20FPS claim or causal whole-frame gain established. Fast native counters show execution; verification counters are correctly zero in mode2. No repeated A/B loop used.

Started perf211-defer-b16-20260925 with both natives retained, deferred recorder on C0, priority offset -8, notify batch16, spin0, C0 snapshot assistant retained. This tests an intermediate priority and fewer wakeups rather than removing the snapshot assistant and saturating C2. Await actual whole-frame results; not promoted to default. Added a 30-second lease-renewal loop to close the restart gap left by the former five-minute renewal interval.

## Scheduling results and opt-in audio diagnostic fix

Deferred batch16/prio-8/C0-pump last20 settled reporting windows: 79.735 ms mean, 75.7–86.1 ms range. Moving only the pump to C1: 76.165 ms mean, 72.4–83.5 ms range. Both retained native material/effects; neither establishes a whole-frame improvement or reaches50ms. Do not promote these scheduling settings. Private captured logs in isolated-save-candidate.

Fresh Pi profile: harness-fin2 SHA256 5b1317249f99e015be4cd9841cae2efe84c18140d66e09a8651779682bcd44c0, matched local/remote, cores0/1, temporary save, 180-second planned timeout(rc124),71 reports. Frames>=2500 give30 windows. Material native mode2, deferred off; this older harness does not contain effects natives and is NOT an exact perf211 binary. Correctly resolved static load base0x10000. Owner samples: xd3d_ds_check14.5%, __udivmoddi4 4.7%; large Linux syscall fraction is not directly portable to Vita. Source audit confirms scheduler unconditionally invokes the same128-slot corruption-only scan in perf211. HLE-entry callers already gate on XV_DS_CHECK, but the function/scheduler did not.

Added cached atomic opt-in at diagnostic entry, preserving the existing environment-presence convention. No mixing/stream completion/simulation logic changed. Production-function fixture under ASan/UBSan:10000 default calls touch no guest memory; explicit1,0,empty all enable detection and retain report-once behavior. Built perf212 from retained perf211 stage with only this function change and version bump. Executable34726010 bytes SHA2568bd5bd9397538274b825c5bccf802a690fcd2b8cbfe88a4c83db092cb3d4a7b2; same asset contract. Deployment started; hardware gain pending. Next run returns pumpC0/deferredoff, keeps natives mode2 and isolated save.

## perf212 hardware status

Updater verified executable and boot-confirmed slot0. Ordinary a30 launch: isolated save root retained, natives mode2, deferredoff, pumpC0, snapshot assistant retained. Captured settled pod means: 73.535ms average over last20 60-frame windows, range 70.1–80.9ms (pod-summary.json/perf212-pod-measured.log). No clear whole-frame speedup established. Pi lockstep emits much higher scheduler-yield rates than physical Vita (~2200–2700 yields/3sec observed), so its14.5% diagnostic sample share cannot predict hardware savings.

Remote five-second forward walk exited pod; two-second AR fire succeeded (ammo60->37), outdoor screenshot perf212-after-move-fire.png; no crash during this short smoke sequence. A square press did not visibly reload to60, so do not claim reload verification. Ongoing log perf212-fast-20260925 now includes movement and MUST NOT be summarized as a stationary pod baseline. Full15-minute gameplay/AI-combat/save-resume qualification and20FPS goal remain outstanding.

Stopped stale VitaCompanion nosleep loop(pid1069286) and former five-minute renewal(pid2799106); own authenticated30-second keep-awake loop remains active. No source pushes, normal-user-save writes or Halo2 changes.

## End-of-run state — September25

20-minute observer capture completed. a30 was loaded/active by t+150s, giving >17minutes in-game; short scripted walk/pan/AR-fire sequences, otherwise stationary. No observed crash; remote frame counter kept advancing. This is a stability smoke test, NOT sustained combat/AI or checkpoint-resume qualification. Final20 outdoor window means average78.97ms(12.66FPS), range76.1–85.0ms; separate from73.535ms(13.60FPS) pod capture. No individual-frame tail distribution or20FPS success claimed.

Returned Vita to dashboard by app restart. Confirmed perf212 and `dashboard: ready`; no test-save override persisted. Backed up xita.cfg locally, added only XV_NATIVE_70110=2 / XV_NATIVE_EFFECTS=2, and verified uploaded bytes by reading back. Dashboard startup confirms both persisted modes. No experimental scheduling flags added. Existing user settings retained. Normal checkpoint files were not used by isolated runs. Own30-second authenticated lease loop remains active (shell session70068); all movement controls released.

Fresh Pi harness built from retained perf212 production stage plus current host-only compatibility files: vita_runtime_shim.c, neon_x4_compat.h, runtime_stubs.c, sampler.c, harness.c, and host helper-stack/role initialization prefix in xk_scene_thread.c. Initial host attempts failed for missing files/stack symbols and then skipped runtime initialization; those failed attempts provide no performance evidence. Corrected harness SHA256175bfbdf1fe513b64625f58917c396428fa89b0be3e3039284d8e45019f5a27f completed180-second planned timeout(rc124),69reports; natives mode2, deferredoff, cores0/1, temporary save. Frames>=2500 yielded28windows. Debug scan no longer appears in top owner samples. Helper samples remain distributed:54010 2.8%, n70_fast2.3%,66510 2.2%,597CB2.1%,native56F20 2.0%,A2380 1.6%,record_draw_body1.2%. Linux syscall/scheduler shares are not Vita bottleneck percentages; no Pi-derived FPS claim. Receipts: ds-check-candidate/pi-profile-summary.txt and native-70110-work/pi-runs/codex-perf212-profile2-20260925.*.

Next: use the updated harness to investigate repeated state/constant preparation and ordered callback dispatch (54010/A2380/A26B0), with live Vita draw-path costs as the deciding evidence. Do not repeat the failed C0-priority/pump-core experiments without a new workload reduction. Queue padding remains off and undeployed. Keep the faster native helper paths while judging total frame time; their correctness coverage is stronger than the current evidence for a whole-frame FPS gain. Goal remains unmet. Source commits local only; nothing pushed, no Halo2 changes.

## Sampler admission follow-up

Hardware perf212 reports zero accepted sampler batches; owner-phase logs also show zero BCB30 entries. Source confirms scene helper uses its own copied `ctx`, whereas owner-phase admission requires the original presenting context and live scheduler fiber. Do not weaken the generic owner-phase guard: UV/fog memoization depends on those boundaries.

Added a separate `xv_scene_thread_owns_context` admission API for the stateless, ordered sampler batch only (Vita and host helper identity first, exact context address, active dispatch depth; disabled backend rejects). `XV_MATERIAL_SAMPLER_HELPER=1` opts into this candidate; default remains off. Worker/diagnostic exclusions retained. Fixture now compares original guest writes/context for owner and helper admission, including stack/table aliasing. Command: `python3 tools/test_material_sampler.py --shard ../ce-perf7/ce-build/recomp/code_011.c`. No hardware build/deployment or FPS claim yet. Next: qualify actual ARM helper admission and native70110 tapped verification, then build a narrow perf213 candidate from retained perf212 stage. Stateful UV/fog caches deliberately untouched. Vita remains perf212 dashboard; authenticated lease renewal succeeded.

Sampler candidate build receipts: ARM harness and retained-stage Vita build both passed. Candidate `../sampler-helper-candidate/xita-perf213c.vpk`: executable 34,730,602 bytes, SHA256 c38683fa673e7aa72fc5e11e25716b94b5275b3e6ce26cee089517ecd20ecfcb. Code-only package retains perf212 asset contract and every non-code/non-boot-record entry byte-for-byte (validation reopened both ZIPs to avoid ZipInfo mutation). No deployment yet. Pi session codex-sampler213-verify-20260925 on cores0/1: actual scene helper accepts batches (early windows 2521 then5909 per stage), material tapped verifier so far zero mismatches. Await terminal run receipt before qualifying.

Pi sampler verification finished at planned timeout rc124 after180seconds,45 report windows:493279 material calls/19648948 tapped segments,zero mismatches. All45windows accepted sampler batches. Source receipt ../sampler-helper-candidate/pi-verification.json. This verifies the actual helper path plus tapped native execution in this run, not Vita performance or full game correctness. Perf213 code-only remote deployment started (deploy.log, shell session54674). Pending hardware verify run with XV_TEST_SAVE=a30-perf211, XV_MATERIAL_SAMPLER_HELPER=1, XV_NATIVE_70110=1, effects2, deferredoff. Do not compare verifier timings to fast mode.

Perf213 updater finished successfully: executable verified, slot1 boot-confirmed (deploy.log). Hardware ordinary-launch verification started via overlap-candidate/bench-a30still.sh tag perf213-verify-20260925,10minutes, isolated save a30-perf211, helper sampler1/material1/effects2/deferred0/pump0. Driver capture sampler-helper-candidate/hardware-verify-driver.log. No frame-time claim from verification mode. Next poll actual launch/save-root and sampler/native logs; finish verification before restarting fast. Keep-awake lease loop retained.

Additional audited follow-up: UV and fog memoizers are also tied to xv_owner_phase_active; exact copied-context admission fails. Even changing that gate alone would not enable safe reuse: both validate pt==g_xpt and image==g_img_base, while scene overlap uses X_PT/X_IMG_BASE snapshot mappings. Their mutable caches and boundary resets are shared. Any adaptation must use the active mapping, preserve per-scene generation/invalidation, and isolate helper-owned cache state from owner reports/boundaries. Do not enable by merely bypassing owner-phase checks. Current sampler candidate is stateless and uses X_W32, so it does not have those cache-lifetime assumptions. Added isolated production admission predicate tests for null/foreign contexts, inactive dispatch and non-helper callers; owner/helper guest-equivalence fixture remains8192 cases passing.

Hardware sampler verification reached the lifepod (verify-cutscene.png shows gameplay/HUD, despite filename). Retained complete capture from watcher .max: {"calls": 157803, "segments": 6259025, "mismatches": 0}. Final independent FTP fetch failed; used preserved longest successful watcher capture, not an absent file. Stopped watcher/launch parent deliberately after qualification; started perf213-fast-20260925 15minute ordinary launch (session92390), identical settings except native70110=2. No fast measurement yet. Native sampler opt-in is not persisted in user cfg.

Added off-by-default `XV_FRAME_TIMES=1` raw Present-to-Present telemetry in source xv_ui_gxm.c for the next candidate. It reuses existing t1/previous-present timestamps (no new per-frame clock call), stores60intervals, and reports3lines of20samples at the existing joined owner report. Zero denotes unavailable first/nonmonotonic interval; these are CPU Present intervals, not GPU/scanout times. Max formatted line465bytes before prefix/newline fits xv_log's512buffer. Isolated retained-stage ARM Vita compilation passed (`sampler-helper-candidate/frame-times-stage`, frame-times-compile.log); perf213 installed binary does NOT include this telemetry. Initial compile mixed source/stage pragma-once headers and failed duplicate types; compiling in the isolated coherent stage resolved it. No source push. Fast hardware session92390 still loading; preserve its tag/log and do not restart solely because the observer has not finished.

Perf213 fast run remains live (session92390), lifepod visually confirmed by fast-scene.png. Accepted sampler groups5760/stage/60frames (~96 material batches per frame) in early gameplay windows. Early FPS12–14 is not a demonstrated total-frame gain; retain longer settled sample before conclusion. No individual-frame tails available in perf213. Next cache candidate must use helper-only admission, per-dispatch generation and active X_PT/X_IMG_BASE mappings, with owner calls rejected while enabled; current owner-only caches must not share mutable state with a concurrent helper. No such cache change implemented yet.

Perf213 settled pod sample: last20 windows mean75.675ms (13.214FPS), window means71.5–82.8ms. fast-pod-measured.log/fast-pod-summary.json preserve this exact sample. These are not individual-frame percentiles. Sampler batching is active but no clear whole-frame gain over perf212; retain correctness evidence without claiming FPS improvement. Fast observer still live session92390.

Helper cache candidate implemented, NOT deployed: XV_MODEL_CACHE_HELPER=1 makes UV/fog admission helper-exclusive and validates active page/image mapping. Scene dispatch publishes a monotonically increasing generation (saturates/fails closed); stale tokens decline. Owner calls cannot mutate helper cache while enabled. UV packet-root reads use X_IMG32 in helper mode rather than direct flat-image access. Existing argument/FP/span/alias checks retained. New tools/test_render_cache_context.py checks foreign contexts, wrong thread, inactive dispatch, stale/zero/exhausted generations and mapping selection under ASan/UBSan; passed. ARM whole-game harness built at helper-cache-candidate/host-arm/harness, includes optional frame telemetry from a304ae4. Pi actual-path verify run codex-helpercache-verify-20260925 started on cores0/1,180seconds, natives70110=1/effects2, samplerhelper1/cachehelper1; result pending. Do not build/deploy/promote until actual memoization behavior and correctness are understood. Source changes uncommitted, no pushes.

Helper-cache candidate Pi run completed at planned timeout rc124,46reports:507543 material calls/19068702 tapped segments,zero mismatches. Actual UV scopes/hits and fog hits confirmed (example60framewindow:UV12600/2940 hits;fog6660hits/7980misses). This is integration verification; tapped native checks treat cache callees as boundaries, so do not claim507543 independent cache-vs-cold arithmetic comparisons. Existing cache arithmetic/keys are retained; new ownership/generation/mapping tests cover changed admission.

Vita build perf214 succeeded; code-only xita-perf214c.vpk retains prior asset contract and non-code entries. Executable34733622bytes SHA256cb8e36ccb75549d09e20cb734db514e5b735e984b1c612a44904d03364a774f2. Candidate includes sampler support, helper UV/fog admission (off default), and optional per-frame telemetry (off default). NOT installed yet. Next: finish/preserve live perf213 observer(session92390), deploy214, run isolated hardware verification with XV_MODEL_CACHE_HELPER=1 + XV_MATERIAL_SAMPLER_HELPER=1 + XV_NATIVE_70110=1; if clean, cold-launch native70110=2 and XV_FRAME_TIMES=1 for whole-frame evidence. Preserve saves and keep-awake. No claim of20FPS, no pushes.

Stopped perf213 observer intentionally after t+780s capture (not full15minute gameplay qualification). Preserved sampler-helper-candidate/perf213-fast-complete.log and fast-final-summary.json: final20windowmean76.19ms /13.125FPS,71.8–82.8ms windowmeans. Perf214 deployment started, session22422, helper-cache-candidate/deploy.log.

Added tools/frame_times.py to parse optional raw CPU Present intervals into mean, nearest-rank p50/p95/p99,max and counts>50/100/200ms. It reports unavailable zero intervals and incomplete windows, rejects malformed chunks, and supports selecting report end-frame ranges after scene qualification. Synthetic checks exercised known tails/thresholds, zero samples, truncated reports and malformed chunks. No real perf214 timing output yet; never apply percentiles to the old60frame means.

Perf214 installed: updater SHA256 verified,slot0 boot-confirmed; status reports0.2.0-perf.214. Started ordinary isolated verification launch tag perf214-verify-20260925/session1860,10minute observer, cachehelper1/samplerhelper1/native70110=1/effects2/deferred0/pump0. Driver helper-cache-candidate/hardware-verify-driver.log; ongoing capture overlap-candidate/gameplay-perf214-verify-20260925.log(.max). No hardware cache result or FPS claim yet. Next validate save root, actual reuse, native verifier counters and scene; then cold-launch mode2 with XV_FRAME_TIMES=1. Keep-awake loop remains active.

Perf214 hardware verifier reached lifepod (verify-scene.png). Retained137351calls/5041591segments,zero material mismatches; UV/fog hits confirmed. Preserved helper-cache-candidate/hardware-verification-complete.log + hardware-verification.json. Deliberately ended verifier observer before10minutes; began normal mode2 launch perf214-fast-20260925/session65216,20minute observer, XV_FRAME_TIMES=1, same isolated save/cache/sampler options. No native-speed measurements yet.

Extended independent fog ARM fixture with --helper-context: production memoization sees stale global roots while active snapshot roots remain correct; compares original retained region versus cache hit/miss for context,8MiB memory,FPSCR.289cases passed in helper-cache-candidate/fog-helper-oracle/prototype-results.json, including random values, root changes, stale roots, generation change and diagnostic exclusions. First attempted ce-perf7 oracle body failed its pinned emitter hash; selected matching ../ce-build body without weakening the gate. Fixed test argument shadowing and made root-change update the active table as intended. UV owner regression run is pending; its fixture needed current owner identity API and a fail-closed atoi stub (fixture has no environment values) to avoid unrelated Vita libc TLS linker dependencies. These are fixture-only changes, no installed runtime changes.

UV owner-path regression finished successfully:111 comparisons (helper-cache-candidate/uv-owner-oracle/results.json; log ends PASS111). Fixture owner identity now forwards to the retained noinline sceKernelGetThreadId wrapper so existing call-count instrumentation remains meaningful. This test covers unchanged owner behavior, not helper UV arithmetic independently. Fog helper289, admission/generation tests, Pi and hardware integration checks cover the new path to their stated scopes. Normal-mode hardware run session65216 remains active, no result claimed yet.

Important scene correction: perf214-fast resumed an OUTDOOR checkpoint, not the stationary pod. Confirmed by fast-scene.png and savegame reads. ~421draws/frame vs pod~260, pump~50–62ms and helper~87ms. Never compare this view directly against perf213 pod means. outdoor-timing.json preserves840rawintervals:mean130.902ms(7.639FPS),p50129.556,p95140.657,p99251.933,max501.326ms. These are CPU Present intervals, not physical scanout/GPUservice times.

Raw intervals expose deterministic large hitches every180frames: inferred frames5345,5525,5705,5885,6065 (offset4 in corresponding60-frame windows),~386–501ms. Source xk_diag_poll.c scans all screenshot directories every180ticks when XV_SHOT_DUMP is enabled; startup cfg confirms1. Strong cadence match, not yet a causal hardware result. Config backed up/read-back verified under helper-cache-candidate/config-* and changed only XV_SHOT_DUMP=0. This disables diagnostic directory scanning, not Vita screenshot capture. Preserved perf214-shot-scan-on.log; deliberately stopped old observer parents. Started perf214-no-shot-scan-20260925/session pending tool receipt,15minute normal-mode launch same options+explicitSHOT_DUMP0. Need compare scene and cadence after restart, not automated AB benchmark. Keep current native cache settings, no new binary for this change.

Prepared padded-record-candidate/build-x87 perf215 with only f5a0c5d Makefile/queue header patch plus XV_REC_QUEUE_PADDED=1 over perf214; build passed(session18803). Checked Makefile insertion target and existing host/Pi/TSan4x150k stress receipts. Not packaged or deployed. Do not activate deferred recording until current diagnostic-hitch finding is qualified; prior core-priority-only experiments did not improve total frames.

No-shot-scan run session96289 confirmed XV_SHOT_DUMP=0 on startup and isolated root. Scene changed back to lifepod (no-shot-scan-scene.png), so do not compare average FPS against prior outdoor view or claim its7.6->13.8 difference as optimization. First480rawintervals selected by report end>=5520:mean72.473ms(13.798FPS),p5070.973,p9584.998,p99123.364,max143.170ms,zero>200ms. The180-frame large-stall pattern was absent across this early sample, consistent with removed screenshot-directory scans; expand sample before final conclusion. Receipt no-shot-scan-early-timing.json.

Perf215 padded recording package ready, NOT deployed: padded-record-candidate/xita-perf215c.vpk, executable34734282bytes SHA256966ea4ace105bc7a144912dafe78c56b6567b33527b6335c850ed3696b548903, unchanged asset contract and non-code entries verified. Existing Pi/TSan queue stress receipts read and passed; no new testing claim. Current215 build only changes layout/stamp over214; deferred recording still defaults off and requires explicit runtime mode/settings to exercise.

Settled no-shot pod result preserved: no-shot-pod-complete.log + no-shot-pod-summary.json,1200rawframes (20windows,end6660–7800),mean72.4627ms /13.8002FPS,p5070.979,p9583.931,p99115.523,max150.921ms,30>100ms,zero>200ms. All1200frames>50ms,so20FPS target still fails. Prior400–500ms/180frame pattern absent; differing outdoor/pod views preclude attributing mean difference solely to scan removal. Began short five-second forward walk,two-second AR-fire,two-second right pan; record movement-status.json + no-shot-after-move-fire.png after completion. Subsequent observer samples no longer stationary pod. Main observer session96289 remains live.

Movement/fire smoke completed; screenshot confirms outdoors, AR36rounds versus60before, reserve/grenade pickup during movement, no obvious corruption in this view. Remote controls released. This is not AI combat/audio/save-resume qualification. Observer after movement must be interpreted separately from preserved pod capture.

Preserved perf214 no-shot ordinary run before215 restart: helper-cache-candidate/no-shot-full-before215.log and no-shot-outdoor-final-summary.json. Final1200 outdoor intervals mean85.7644ms/11.6599FPS,p5084.86,p9596.994,p99131.078,max200.104ms,41>100ms,1>200ms; all1200>50ms. Distinct scene from prior valley/pod; not causal A/B gain. Stopped observer parents3248239/3249518 intentionally. perf215 updater running session7815, deploy.log; full executable uploaded, verification/restart pending. Keepawake PID3092309 verified live and status awake3599. Next launch deferred2/core0/prio-8/batch16/spin0 retaining helper caches, sampler, natives2, shot0, isolatedsave and raw frame telemetry.

Perf215 deployment complete: updater executable SHA verified,slot1 boot-confirmed; remote status0.2.0-perf.215. Started15minute ordinary deferred recording run tag perf215-defer-20260925 with the above explicit settings, driver padded-record-candidate/gameplay-driver.log. No timing/correctness outcome yet; collect startup actual worker settings/save root and scene before interpreting measurements.

Perf215 live run session4365 verified: isolated save root, deferred draws/events active, draw detailed timing0, shot0. gameplay-scene.png at frame5570 shows lifepod with HUD/model rendering; no broad correctness claim. Early pod capture early-pod.log and early-pod-summary.json contain final6complete windows/360intervals:mean73.2762ms(13.647FPS),p5071.612,p9586.316,p99128.072,max153.838,7>100ms,0>200ms,all360>50ms. Not a demonstrated whole-frame gain versus214pod72.46ms. Latest worker windows ~246draws+697events/frame,286–289KiB/frame,zero full waits,zero slot/matrix/tag errors; worker elapsed33.6–36.5ms,36–37drains/frame with2waited,5.95–8.12ms drain time. HelperCPU47–49ms vs scene wall62–65ms,done-to-noticed7.6–8ms,noticed-to-go2.2–2.5ms. Queuecapacity is not the leading evidence. Coreutil samples~C075/C165–71/C260–67; do not assume tick fully saturated from older runs. Audit followup: worker rd_worker clocks every event twice via sceKernelGetProcessTimeWide; batching timing could remove ~1900clockcalls/frame while preserving event order and per-record release. Not implemented; likely modest, no gain estimate proven. Keep live run/keepawake; next collect settled1200samples and inspect scheduling/notice delay plus worker cost, without restarting this observer solely for timeout.

Perf215 settled pod1200frames: mean73.279ms(13.6465FPS),p5071.795,p9585.133,p99109.394,max150.354,20>100ms,0>200ms,1199>50ms. Preserved settled-pod.log/summary. Subsequent movement/fire screenshot confirms outdoors,AR35rounds,~12FPS; queue slot/matrix/tag errors remain0. Not15minute combat qualification; stopped parent3257923/watcher3259313 intentionally and preserved full-before216.log.

Implemented owner-only split of done->noticed: time helper had already finished before join entry versus recognition delay inside join. The original counter includes useful tick work and must not be treated as pure scheduler delay. Reads existing join-entry and post-semaphore timestamps; no added clocks/waits, reads helper end only after consuming done. Report same denominator and bounded split sums to original total. Vita build216passed in join-notice-candidate; code-only package34734558bytes,SHA25660a6ebab2e4cca5d25658d822eb191d0d1c3433388175cea45df662c15f12c7f; unchanged assetcontract/noncodeentries verified. Deployment next; no hardware split result yet.

Perf216 updater complete: verified SHA,slot0 boot-confirmed (join-notice-candidate/deploy.log). Started15minute isolated ordinary launch perf216-join-20260925, same215settings, driver join-notice-candidate/gameplay-driver.log. Pending actual scene and scene-join-notice reports; no performance claim. Keepawake PID3092309 verified live.

Perf216 live session79266: early-gameplay.log/summary and early-join-split.json preserved. Final360intervals mean73.9035ms/13.531FPS,p5071.493,p9584.928,p99141.894,max175.130,11>100ms,0>200ms,all>50ms. New split in six60-scene windows: beforejoin4.12–8.51ms,insidejoin0.09–0.48ms. Most old done->noticed is prejoin work/overlap, not semaphore recognition; do not pursue hostwait tuning as an8ms saving. scene.png captured at6908. Observer continues; no long-session qualification.

Pi supporting profile finished planned180s timeout124,68reports: codex-defer-profile-20260925 log/samples in native-70110-work/pi-runs. Harness hash7c3882d23c352f32a9606912da8a5e29c9326484d60dba7b7e4502e25918128e matches local helper-cache harness (pre215queuepadding/pre216Vita-onlytiming). Cores0/1,prior helpercache/sampler/nativefast+deferred2. pi-profile-summary.txt selectsframe>=1800,39windows. Owner15972samples:39.2%libcsyscall,8.2%udivmoddi4,2.9%nq_run_impl,1.6%17A8B0,1.5%nssolver,1.4%8DDF0. Helper7560samples:54010 5.1%,n70fast3.3%,A2380 2.8%,53E902.5%,665102.4%. Host yieldstorms/virtualclock and ~458draws/frame differ fromVita~260; never scale these percentages intoVitaFPS. LR fordivisionmostlyunresolved/scratch,not enough to assign caller. Nativepoint-location17A8B0 is a bounded next candidate shared by53callers perexistingnative1721b0notes:130retainedClines,no callsites,1preemptbackedge. Privatepinnedreference in join-notice-candidate/point-location-reference.{c,json},SHA35811f19270e8c5e14d406924f45fda37f12c21a72374ecd8b30df5b6ebce0a0. Must preserve doubleprecisionx87order,flags/register/stacksideeffects,mapping,backedgeyield/fallback beforeintegration; no candidateimplemented or speedupclaimed.

Implemented unhooked point-location prototype recomp/kernel/xk_point_location.h plus pinned private-reference differential driver/fixture and docs/native-point-location.md. First version failed case378 on x86+ARM: NaN payload in deadx87slot despite matching leaf. Added exceptional context-operation path; expanded4096cases compare wholecontext/4MiBmemory/preemptioncount+context-at-yieldhash/FPexceptionflags, including pointmutationatyield and shuffled/splitfloatmemory. Host and CortexA9ThumbstaticPi tests passed4096 each; ARMreceipt point-location-candidate/arm-result.log. No hook/builddefault/deployment change. Still needs alternateactivepagebinding,alias/rounding/faultinjection/performance+ingamequalification beforeintegration.

Perf216 observer remainslive(session79266) at~660s,keepawakePID3092309live. settled-pod.log/summary latest1200frames mean72.9919ms/13.7001FPS,p5071.28,p9584.468,p99123.735,max140.794,27>100ms,0>200ms,all>50ms. No20FPSclaim. No15minuteactivecombatqualification; this run predominantlystationary.

Expanded point-location fixture with separate activepagebinding(staleglobaltable),fourroundingmodes,pointaliasing savedregisterstack. Host+Pi eachpassed4096 fullcontext/memory/yieldtrace/FPexceptioncases. Hostmutants wrongside and omitteddeadslot bothcaught. Pi staticA9Thumb synthetic32node/no-yield200kcallmicrobenchmark: retained2467.6ns/call,candidate2007.3ns/call(~18.7%less),nohookoverhead/noVitaFPSinference. Receipt point-location-candidate/snapshot-arm-result.log. Candidate remainsunhooked; next add guardedopt-inverifier and actualgamecorrectness beforehardwarefastmode.

Perf216 observercompleted15minute requestedobservation (includesload,mostlystationarypod); runtime remainslive,statusframe17000,awake3579. Preserved completed-observer.log/completed-pod-summary.json. This is not15minuteactivegameplay/combatqualification. No new CEcrash observed; watcherlatestcore entryisoldgame-b(H2),notthisCErun. Keepawake retained.

Added default-off XV_POINT_LOCATION wrapper (0guest,1verify,2native),worker/unalignedstackdeclines,atomiccounterreport/onceinitialization,failuredisablescandidate. Verifier restores8prologuestackbytes/inputcontext/FPstate,retainsguestresult,comparesfullcontext+stack+FPstate,defersyieldsduringreplay andappliesguestbudgetafterreturn. Host+Pi4096wrappercomparisons passed; wrongsidefaultcaught byverifier. Patcherrejectsrepeat/drift. Headeraliasesadded forretainedshard X_G locals afterfirstVitacompilefailed; initialreportchainpatchhunkfailed andwasappliedbycheckedmarker instead.

point-location-build/build-x87 preparedperf217 from216,onlyhookheaders/code028wrapper/reportchain+stamp. FirstcorrectedVitabuildpassed but initializationCASchangeoccurredduringbuild; explicitlydeletedcode028.o andreran,session27503 finalbuildpendingverification. Hostfinalharnessrebuilt aftersamechange (deletedobject),host-build-final.log passed. Uploadedharness-codex-point-location;180s whole-gamePi verifier codex-point-verify-20260925 startedcores0/1,driver point-location-build/pi-verify-driver.log. No package/deploymentyet; Vita remains216 awake. Sourcewrapperneedsin-gameverification beforefastmodequalification.

Finalperf217Vitabuild(session27503)completed0; code028objectmtimepostdatesfinalhookheader. Code-only xita-perf217c.vpk34740518bytes SHAa449dc7ddfd7999435eee620d6ad2a0ca9897e99c996c7ae28f3538dcee92c8d,unchangedassetcontract/noncodebytesverified. Pi fullgame180secondverificationcompletedplannedtimeout124/68reports,765691pointcallsverified,0mismatches/declines (point-location-build/pi-verification.json). Remote217updateinprogresssession66921,deploy.log. Afterbootconfirmedlaunchisolatedsavea30-perf211 withXV_POINT_LOCATION=1 andall216settings; verifyhardwarebeforemode2. NoVitaspeedclaim.

Perf217 updaterverifiedSHA/slot1bootconfirmed; remotestatus217. Startedisolatedhardwareverification perf217-point-verify-20260925,10minuteobserver,XV_POINT_LOCATION=1,all216optionsretained. Driver point-location-build/hardware-verify-driver.log. Counters/scenepending; verifierFPSnotperformanceevidence. Normalusersaves/configunmodified.

Perf217 hardware verifier preserved in point-location-build/hardware-verification-complete.log and hardware-verification.json: 1,046,158 point-location calls, zero mismatches/declines; screenshot verifies lifepod. Intentionally stopped observer parents3291135/3292288, then cold-launched same binary with XV_POINT_LOCATION=2, otherwise unchanged isolated settings. Tag perf217-point-fast-20260925,20minute observer, session22304, driver point-location-build/hardware-fast-driver.log. Verifier timings are not speed evidence. Keepawake3092309 confirmedlive. Normal saves preserved.

Remaining wait audit: perf216 frame-acquire windows often accumulate7–13ms/present waiting for mesh/UI slot ownership, separate from inside-join recognition0.09–0.48ms. main.c xv_present waits for g_frame_completed; pump releases it only after final fragment notification and geometry/query retirement. These waits protect GPU-owned memory and cannot simply be removed. Existing gpu-packet after-submit intervals ~91–109ms include queued latency and observation uncertainty (~10–11ms/frame, occasional94–108ms gaps); they do not establish GPU service time. Next distinguish delayed pump observation/retirement from actual GPU occupancy. Source pump polls before whole-frame submission; a long submission can delay noticing older tickets. Recording-worker priority contention may also contribute, but prior priority-only changes did not establish a whole-frame win. No wait policy changed.

Perf217 fast launch session22304/PID3297334 confirmed live; initial observer120s loaded0/~22FPS was menu/loading, NOT gameplay evidence. fast-scene.png atframe5323 confirms opening pod cutscene (~12FPS overlay). Native counters active. Let run settle; do not compare early load windows against pod.

Prepared perf218 worker-timing-candidate. runtime/xv_rec_defer.h formerly called rd_now twice unconditionally for every worker event (~1900calls/frame in recentpod). New XV_REC_WORKER_TIMING defaults0, opt-in1 retains timing; report explicitly says disabled so worker0ms is not interpreted as no work. When enabled, statistic accumulation now precedes release of the consumed tail, so drained producer cannot reset it before the worker update. Queue order, per-record release, wait and fence policies unchanged. docs/d3d-record-defer.md documents option/elapsed-time interpretation. No synthetic FPS estimate. Final Vita build session79248 completed0 after explicitly rebuilding xv_d3d.o; version218. Code-only package worker-timing-candidate/xita-perf218c.vpk 34740806bytes SHA cad801b52911c2782a598d963610be6dab0c324978491df091b84568eaaa64dc; unchanged non-code entries/assetcontract verified. NOT deployed. Next preserve settled217 gameplay timings before deploying218 and ordinary cold-launch testing; keep qualified optimizations together. Keepawake3092309 remainslive.

Perf217 settled lifepod confirmed fast-settled.png. Preserved fast-settled.log/summary:1200 CPU Present intervals,end5640–6780,mean73.0889ms/13.682FPS,p5071.492,p9585.919,p99113.269,max183.009,21>100ms,0>200ms,all1200>50ms. Near prior216pod73.65ms; no demonstrated whole-frame gain. No15minactivecombat qualification. Stopped exact observers3297334/3298749 intentionally; perf218 remote deployment began session2387. Upload completed, connection refusal during restart expected; do not reupload solely for transient refusal. Need confirmed boot, then native217settings + XV_REC_WORKER_TIMING=0 ordinary run.

Additional audit: xv_d3d_check_geometry scans every command at retirement even when no histogram hashes were recorded. Potential diagnostic early-out via per-list marker, reset on BeginFrame/Swap and preserved through verifier shadow list. NOT implemented; cost unmeasured, do not call it a major bottleneck. Occlusion/census completion already have per-list early-outs. No release moved ahead of GPU fence.

Perf218 deploy session2387 completed0: SHA verified,slot0bootconfirmed; status0.2.0-perf.218. Refreshedlease3600 afterboot (statusinitialawake0), persistentkeepawake3092309live. Started ordinary20minute run perf218-worker-fast-20260925 session78095 with same217settings plus XV_REC_WORKER_TIMING=0, isolateda30-perf211. Driver worker-timing-candidate/gameplay-driver.log. Pending scene/counter/whole-frame evidence; no speed claim. No git push.

Prepared opt-in submission CPU diagnostic for perf219: runtime/xv_submit_cpu.h and four main.c insertion points. XV_SUBMIT_CPU=1 samples kernelrunClocks/enclosingwall aroundxv_gfx_render_frame, reports60submission sums/valid/failedcounts, rejects inconsistentcounterdata. Disableddefault returnsbeforeclock/kernelcalls. No fences/queuepolicychanges. Privatefixture submit-cpu-candidate/fixture passed disabledpath,known totals,invalidsamples,reset; finalVitabuild session40753 completed0. Isolatedstage built by applyingonlymain.c diff+newheader over218; version219. Notpackaged/deployed. Needrealhardware qualification; elapsed-minusCPU cannot distinguishwaitfrompreemption alone.
Perf218 native gameplay observer session78095 stilllaunching/loading atlastcapturet60; no settledFPSclaim. Keepawake3092309live. Next collect218settledpod andoutdoor evidence, then package/deploy219 withdiagnostic1 ifneeded forsubmissionbottleneck.

Perf218 settled-pod.log/summary:1140 intervals,end5700–6780,mean74.0385ms/13.5065FPS,p5072.565,p9585.904,p99125.249,max187.404,20>100ms,0>200ms,all>50ms. settled-pod.png confirmsview. No measurablewholeframegain versus21773.09ms. Short5sforward/2sAR/2span sequence succeeded; after-move-fire.png showsoutdoors/AR34rounds/~11FPS. DoesnotqualifyAI/audio/longcombat. Preservedfull-before219.log; stopped218observerparents3306151/3307526 intentionally.
Perf219 code-onlypackage submit-cpu-candidate/xita-perf219c.vpk34741566bytes,SHA d3739c4e7e93a17d457b6221d2fe965354013bffd31298dbdc40bb57bcb28a98,unchangedassetcontract/noncodebytesverified. Deploymentlive session9993 (notduplicate); afterbootlaunchsameisolatedsettings+XV_SUBMIT_CPU=1. Current goalstillunmet.
AuditedremainingFinishsites: settled218log RTqueue1, UIpurgeonlystartupframe389,noRTfailure logs. MainFinishsitesresourcechange/shutdown/error; RTregistrationdrainsresourceallocation,notordinarybinding. No evidenceper-framefullFinish explainssettledstall. GPUuploadbarriersareDSBoveruncachedmemory,notfullXRAMcacheflush. Do notremoveownershipbarriers.

Perf219 updater session9993 completed0, SHA verified and slot1bootconfirmed. Status0.2.0-perf.219; refreshedawakelease3600. Startedordinary20minute diagnostic run perf219-submit-cpu-20260925 session31037/PID3314043,same218isolatedsettings+XV_SUBMIT_CPU=1. Envconfirmedset; navigation/loadinginprogress. Driver submit-cpu-candidate/gameplay-driver.log. No submissionCPUresultyet; do not restart solelyforobservationdelay. Keepawake3092309live. Next inspect[submit-cpu] validcounts andscenequalifiedwall/CPU/remainders togetherwithframe-acquire/GPUbounds, thenchooseCPUsetupversusscheduling/driverwork.

Perf219 settled pod confirmedscene.png. submit-cpu-candidate/settled-pod.log + settled-submission.json: latest20submissionwindows1199valid/1rejected,0failed; mean16.72996mswall,5.80357msCPU,10.92639msremainder. Initialsummaryassert60valid/windowfailed; correctednormalizationuses1199actualvalidsamples,recordsreject. Nearbysameperiod1200Presentintervals,end7020–8160,mean73.5783ms/13.591FPS,p5072.179,p9583.309,p99114.276,max155.804,22>100ms,0>200ms. CPUremainderisnotprovenrecoverabletime; separatepreemptionfromdriverwaitnext.
Implementedpump-targetwaitsampler, perf220isolatedpump-wait-candidate/build-x87: weakpump-phaseIDgetter,atomicpublicationonlywhenXV_SCENE_WAIT_TARGET=pump; sampleronlyqueriesduringsubmission. Usesdifferentcorefrompump(otherwiseitwouldpreempttargetandbiasREADY): pump0->sampler1,otherpump->sampler0. Existing500usperiod/priorityretained; diagnosticFPSnotcleanbaseline. Counter/reporttable nowtrylockedandcopiedbeforenaming/formatting,fixesconcurrentreset/sort races. Initialbuild7853passed; correctedcross-coreplacementthenfinalbuild91251passed0. Notpackaged/deployed. Sourcechangesdocumented; noGPUownership/queuepolicychanges. Perf219runstilllive session31037,keepawake3092309. Nextpackage220,deployannounced,coldlaunchsameoptions+XV_SCENE_WAIT_SAMPLE=1 XV_SCENE_WAIT_TARGET=pump. Needhardwarestatesbeforenextschedulingchange.

Perf220 packaged pump-wait-candidate/xita-perf220c.vpk34742606bytes,SHA5d2c1e30670ade03cbbaaeaee5ee8230358025c1a21205dbbb167cd4bae3413d; unchangedassetcontract/noncodeentriesverified. Preserved219full-before220.log andstoppedobserver3314043/3315404intentionally. Deployment48317completed0,SHAverifiedslot0bootconfirmed; status220awake3584. Started20minuteordinaryisolatedrunperf220-pump-wait-20260925 withsame219options+XV_SCENE_WAIT_SAMPLE=1 XV_SCENE_WAIT_TARGET=pump. Driverpump-wait-candidate/gameplay-driver.log. Pendingpumpstates; do notinfergainfromdiagnosticFPS. Keepawake3092309live.
Reviewedold208priorityexperiments: boostingrecworkerreduceditswalltimebutstarvedpump, movingworkerC2saturatedguest; noautomaticprioritywin. Current219renderpumpactualpriority160,C0; audiohighpriority64C0,vertexcapture153C0. ExistinghelperCPU~47ms/frame anddrawrecordwaiting~4–9msstillimportant. Newpumpstatesneededtodistinguishreadyvsdriverwaitbeforechanges.

Perf220 early-pod.log/summary andscene.png preserved. Latest8windows9390pump-submitsamples,roundedweighted40.28%running/58.85%ready/0.875%waiting. SupportsC0contentiontrial,notGPUwaitremoval. IndividualframeintervalsMISSING: /env servercopiesonly511querybytes, silencetruncatedlastXV_FRAME_TIMES=1 inthislongerlaunch. All earlierrequestedpump/native/coreoptionsareinstartuplog; lastloggedFRAME_DRAWS1. No percentileclaimsfor220. Clienthadprintedfullrequestasifallsucceeded. Fixedtools/vita_remote.py env_batches tovalidatebeforemutation/split<=511bytes; deviceendpoint nowrejectsoversizedrequests. tools/tests/remote_env_batches.py checksboundaries,orderedbatch/trailingoptionretention,malformedinput; passed. Thisfixalsohelpsolddevicebuilds; nofullbenchmarkinvalidityclaimforpreviousrunswhoseframeintervalsactuallyexist.
Preparedperf221 pump-priority-candidate (isolated220stage) withXV_PUMP_PRIORITY_DELTA default0,bounded±16 thenactualpriority64..191; opt-in -12 trialplanned(actual160->148),audio/workerunchanged. Mainbuild51446passed; subsequentremoteendpointfixcopiedtostageandfinalbuild47327running. Notpackaged/deployed. Needfinalbuildreceipt,package,stop220observerafterpreservinglongestlog,announceupdate,launchviafixedclient withsame220settings+PUMP_PRIORITY_DELTA=-12. VerifyallenvstartupentriesincludingFRAME_TIMESandactualpumpprioritybeforeinterpretingdata. Keepawake3092309live;220observer13047stilllive.

Perf221 finalbuild47327completed0. Code-onlypump-priority-candidate/xita-perf221c.vpk34743150bytes,SHA766b51bea074511cd908943ee4c46086f8315ea1eda8df14ffac8ead476272db; unchangednoncode/assetcontractverified. Preserved220full-before221.log andfinal-pump-summary.json:20windows23341samples,roundedweighted40.13%running59.18%ready0.43%waiting. No220rawframetimes(confirmedenvtruncation). Stopped220observer3324119/3325216intentionally. Deployment47754completed0,SHAverifiedslot1bootconfirmed. Started20minordinaryperf221-pump-priority-20260925 withsame220options+XV_PUMP_PRIORITY_DELTA=-12 viafixedbatchingclient. Driverpump-priority-candidate/gameplay-driver.log. Needstartupverify actualpriority148 andallenvincludingFRAME_TIMES beforedataanalysis. Keepawake3092309live. NoFPSgainclaim; no push.

Perf221 startup verified all25requestedenvpairs includingFRAME_TIMES; startup.log/startup-verification.json confirmsnativepump148,prioritychangeresult0. EarlycampaignREADYfractionfell(~5–12%)butrunstalledbeforequalifiedsettledsample. Observergameplaylinesstoppedat~12030; remoteloopcontinuedbriefly,thenstatusrequesttimedout. Companionversionresponded1.07. Preservedpump-priority-candidate/stalled-run.log. NoFPSwin/stabilityclaim; noexactcrashcauseknown. BookendkernelCPUcounterstinyinsomewindowdespitesamplerRUNNING~90%; addedinterpretationlimit,do nottreatprevious5.8mscountervalueasexactexecutionor10.9msasrecoverablebudget.
Stopped221trialobserver3334259/3335418intentionally. Recoverylaunchperf221-recovery-20260925 session4043 usesSAMEbinarywithPUMP_PRIORITY_DELTA=0 andallotheroptionsunchanged,viafixedclient. Companionquit/relaunchsucceeded; status221frames555awake3573,remoteenvshowsdelta0. Driverpump-priority-candidate/recovery-driver.log. Needactualpump160/startupconfirmationandgameplayrecoverybeforefurtherchange. Keepawake3092309live. PriorityincreaseNOTretainedincfg. Originalusersavesuntouched. Next targetremainingrecording/sceneCPUwork; do notrepeatblindprioritychangesorclaimlocalpumpimprovementasfullframegain.

Perf221 recovery confirmed: render-pump actual priority160, screenshot pump-priority-candidate/recovery-scene.png atframe7616 shows settled lifepod and live HUD. Observer3339748 and keepawake3092309 verified live. recovery-settled.log/json preserve1200 rawPresentintervals,end6540–7680:mean72.5967ms/13.7747FPS,p5071.308,p9582.155,p99101.942,max168.285;14>100ms,0>200ms,all1200>50ms. This confirms recovery, not a gain or long active stability qualification. Higherpriority trial remains rejected pending stall diagnosis; identicalbinary originalpriority advancing does not alone prove crash cause.
Currentcost evidence: helper47.72msCPU/frame,scene63.93ms,recorddrain1.05ms in latest window,0queuefullwaits/recorderrors; about245.5draws+697events/frame,285.4KiB/frame,4906constantrows/frame. Removing queuecapacity waits cannot help this window because there are none. Native70110 active96calls/frame. Its helper counter reports0 but is disabled in untimed native mode; that value does NOT establish which thread ran the function (confirmed by xv_native_70110 counter guard). Next prioritize helper translated preparation and repeated constant/state work, not another blind affinity/priority change.
Audioaudit: xk_audio.c mixer executes decoding/resampling under global audio mutex, then blocking sink output outside lock. xk_os_vita.c pins mixerC0 priority64. It is already a separate thread; moving it is not new parallelism. No measured mixerCPUbudget yet, so do not call it dominant or move it blindly. RD_MATRIX recorder events currently validate matrix mirroring rather than change workerstate; potential reduction is unmeasured, must preserve object tagging/order and verifier diagnostics. No code changes/deployment in this recovery turn, no push;20FPSgoal remains unmet.

Fresh Pi profile codex-current-profile-20260925 completed planned120stimeout124/38reports,cores0/1,normal fast native70110/effects/pointlocation plusdeferredrecording. No concurrentharnessfoundbeforelaunch. Remote/localharnessSHA match9ca4966cd01503d10f5a7415a7ffdbbff2fa15bcf727bfcd2e0e29631d3367fe. Private point-location-build/profile-current.sh andprofile-driver.log reproduce; samples/log in d3d-record2-work/pi-runs; resolvedcurrent-helper-profile.txt usesmatchinghost-arm/harness. Settledlast8windowsend1860–2280 (480frames),director1camera31.50,-102.50,59.38;1372helpersamples:54.3%guest,11.7%recording,7.2%libc,4.5%kernel/HLE,22.2%other. ITIMER_PROF delivery bias and small sample count apply: percentages are leads, tool ms/frame is NOT hardware timing or calibrated threadCPU. Topremainingguest54010 5.5%,53E90 3.5%,66510 3.2%,A2380 2.6%;n70_fast4.1%confirmsnativeonhelper. Next inspect54010/53E90 repeated traversal/setup; oldsurface-scan prototype isnotqualifiedandregresses shortruns, so do notenablewithoutrealrunlength/ownership evidence.
Corrected priorrecovery note: NC_ON_HELPER onlyincrements in70110verifier/timedmode. Zero inuntimedfastmode isunmeasured,notabsenthelperexecution. Source report now explicitlylabels thatguard, gitdiffcheckpasses; notbuilt/deployed. Vita staysperf221originalpriority,keepawake3092309verifiedlive;noFPSclaim/newpackage/push.

Implemented uninstalled53E90unset-bitbatch prototype (xk_surface_mask_scan.h,test_surface_mask_scan.py,tests/surface_mask_scan.c,docs/native-surface-mask.md). RetainedbodypinnedSHA325e0b3eb548cb30a5f46f0d5922c1a635a9ad2c859a65cabfb1204b469b3b84; generatedguestbodyprivateonly. Firstfixture admitted0batches dueoverstrictSPpageguard; correctedguardchecksactualscratchslots. Host3ASan/UBSan andPiARM passed768fullcontext/memorycases,2505batches,3000matchingyields; coversmask/budgetvariety andmaskchangeatyield. Rootpointerpassedthroughoriginalimageaccessor forrender-viewcompatibility. Addedcounter==ECXguardafterPi check;host4session26060pendingreceipt. No productionhook/build/deployment. Needalias/remapping/yieldtrace tests,ARMcost andrealownership/runlength evidence beforeintegration. Keepawake3092309live,lastrecoveryobserver3339748advancing. Goalunmet.

Surface-mask followup: host4 receiptpassed. Addedexactyieldcontexttrace comparison;host/Piyieldfixturepassed768cases2505batches3000yields. ARMcost correctedargument-resetbenchmark:128entries/50000calls,nsoriginal/candidate empty389/401,bit06982/1695,bit317027/1210,dense11927/14746,alternating10844/15284,bits0+167237/2384. Initialcostruninvalidbecauseoriginalmodifiesarguments;correctedfixture restoresallthree eachiteration. Private surface-mask-yield-host/pi-result-corrected.txt authoritative. Densecostregressionblocksdeployment,notwholegoal. Nextrealmaskdistribution andcheapadmission;noFPSprediction. Expandedfixtureadds maskpage relocation andcountchange atyield;surface-mask-remap-host session1180 resultpendingdocumentreceipt. Allgeneratedreferencecodeprivate. Keepawake3092309verifiedalive.

Mask followup completed: remapfixturepassed768/1647batches/2148yields. Added3explicitphysicalaliasdeclines,allcontext/memorypreserved. Boundcheckmovedbeforemaskread (avoidsreadingmaskwhenoriginalwouldexitatcount); earlytwo-bitrejectionbeforefullaliasadmission. Finalhost/Pi passsurface-mask-alias-host;Pi coststilldense10.38->13.62us andalternating9.80->11.46us,notqualified. Privatewholegameprofilebuiltby surface-mask-profile/build.py replacingonlycode009 overpointlocationhostobjects;120srun codex-mask-density-20260925 completedrc124. Settledframes>1800:487424words/119groups,79.565%empty,7.136%allset,2.162%1–4setbits. Needword-leveladmission toavoidper-bitfallbacktax;nomajorFPSclaim. Diagnosticgeneratedcodeprivateonly. Vitaobserverfinishedordinaryrecovery~14FPS;keepawake3092309verifiedlive. Allsourceworkuncommitted,nopush/deploy.

Maskwordadmission trialcompleted: generatorlocalhintpopcount<=8at53EB9,helperstillvalidatesliveinputs. Host/Pipassed768cases1511batches2148handoffs+3aliasdeclines. Private surface-mask-word-host/pi-result.txt:ns/call sparse4819->1313,4843->1088,5148->1972;dense9026->9795,alternating7159->7569,empty202->218. Stillfallbackregressions andactualempty79.6%distribution makehardwaredeploymentunjustified. Parkprototype uninstalled;retainauthoredsource/tests/findings. NextlargergoalitemhiddenBSPdrawpreparation/recording,originalhandoffestimated65zero-sampledraws/frame butneedsfreshcensusandcorrectnessguard,notsimplyskipzerolastframe. Keepawake3092309verifiedlive. No newVPK/push.

Started fresh physical fragmentcensus usinginstalledperf221,normalpriority0,isolateda30-perf211,XV_FRAG_CENSUS=120,sampling/submissionCPUdiagnosticsOFF;otherqualifiednative/deferoptions retained,FRAME_TIMES1. Announcedrestart. Remoteinitialstatus221frame26590awake3581confirmedpriorrunalive;oldobserver3339748terminal,keepawake3092309live. Newdriverworld-census-candidate/driver.log,tagperf221-world-census-20260925,parent3357651,exec68978live. Envacceptedalloptions;navigationinprogress,notyetsettled. No newbuildorFPSclaim.
Sourceaudit: fragcensus disablesobjectproxycollectiononits sampledframes,usesper-commandcounters onlyafterGPUcompletion;zero samples do not independentlyauthorize skippingnextframe. Freshviewneededbeforeusingold65hiddenBSPdrawestimate. Additionalrisk: xk_occlusion.c tab[4096] isread/partlywrittenbyxv_occl_render(helper) andupdatedbyxv_occl_result(pump)withoutvisibleatomics/lock; reportcountersalsocrossreset. Needpublication/lifetimeauditandlikelysingle-consumerresultqueuebeforeextendingthispattern;notestablishedaspriorcrashcause. No runtime change yet.

Implemented sourcecandidate occlusionresultSPSC handoff: xk_occlusion_results.h (1024five-wordevents,aligned32bitrelease/acquire,stickyoverflow), xk_occlusion.c pumpenqueueonly,sceneconsumerappliesbeforemodeldecisions,no waits. Overflowfailopen disablesbothskippoliciesuntilrestart. Tables/resultcountersnowconsumermodified;reportrequiresexistingjoinboundary. HostASan/UBSan andPiARM passed1millionorderedpayloads,wrap,full/empty/stickyloss;retainedVitaunitcompilepassed (occlusion-results-candidate/vita-compile.log). Notfullbuilt/deployed;needintegrationtransition/collisiontests. No crashcause/FPSclaim.
Freshperf221fragmentcensus reachedlifepod: screenshotworld-census-candidate/scene.png frame5745 confirmsview;early-pod.log preserved. Frame5400:252draws123zero-sample,109016indices49634inzero-sampledraws,2targetswitches;worldshader16:20draws19zero,11469indices7974zero;shader40same20/19 and11469/7974;shader11:8/8zero14778indices. This supportsremainingworldworkopportunity butdoesnotproveallzerosareoccluded/safelyskippableorcost5ms. Proxysamplingdisabledoncensusframesbydesign. Runparent3357651/session68978stillactive,keepawake3092309live;noFPSqualificationwithdiagnostic. Nextfinishresultqueueintegrationchecks/buildanddesignconservativeworldcoverageevidence.

Perf222resultqueueintegration: actualxk_occlusion.c fixture tools/tests/occlusion_handoff.c passedhostASan/UBSan+PiARM forpump/tableisolation,hidden/visible,ownsampleprecedence,missingproxy,collision,framewrap,stale,overflowfailopen,stackretadjustment. Removedobsoletevolatileoncounterssceneowns. FullVita buildsession2461completed0. Isolatedocclusion-results-candidate/build-x87 copied221retainedstage,onlyocclusion.c/newqueueheader/native70110loglabel+version222changed. Code-onlypackagexita-perf222c.vpk34742946bytesSHAa49ab5cc65ad6205384866db9aa3561d3578cf9028f4fcd95f4ef5eec44a7b49,unchangedcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897andallnoncodeentriesverified.
Preservedcensusfull-before222.log;stoppedexactobserver3357651/3358954intentionally. Announced222update;remotedeploysession98487active,occlusion-results-candidate/deploy.log. Do notreuploadfortransientrefusal. Needbootconfirmthenordinaryisolateda30-perf211launchwithallqualifiednatives/defer,prioritydelta0,FRAG_CENSUS0,SCENE_WAIT_SAMPLE0,SUBMIT_CPU0,SHOT_DUMP0,FRAME_TIMES1. Checkcullingcounts,queueoverflowabsence,visualmovement/weapons andrawframetimes. QueuefixisnotyetphysicalFPS/stabilityevidence. Keepawake3092309live. No push.

Census aggregation savedworld-census-candidate/settled-census-summary.json frompreservedfull-before222.log:30sampledframes5400–8880,mean253.2draws124.2zero,109066indices49628zero. Selectedshader11 8/8zero14778indices;16and40each20draws19zero,11469indices7974zero,constantacrossall30. Thisisstableviewevidence,notproofzero==occludedorsafelyskippable;needfrustum/depthdisambiguation. Perf222deployment98487stillliveafterupload,remoteconnectionrefusedduringexpectedrestart;no retryupload. Bootconfirmationpending;keepawake3092309live.

Perf222deployment98487completed0:SHAverifiedslot0bootconfirmed,status0.2.0-perf.222frame406awake3582,lease3600refreshed. Startedordinary20minuteisolatedrunperf222-result-queue-20260925,driverocclusion-results-candidate/gameplay-driver.log,allpreviousqualifiedoptionswithFRAG_CENSUS0/SUBMIT_CPU0/SCENE_WAIT_SAMPLE0. Navigation/loadingpending;nextverifyactualstartupoptions,cullingcounts/nooverflow,settledframetimesandactiveplay. NoFPSgainyet.

Perf222ordinaryrunrevalidatedlive:parent3366553,watcher3367882,exec64486;status222frame2403awake3591. Allrequestedenvsetviafixedclient;loadingstillactiveatfirstwatchsamples,noperformanceinterpretation. ShaderauditofretainedprivateCG:halo_vs_16and40positionsareaffinedph(v0,c0..3),zscaled0.9999;11usesderivedr9beforeprojectionandmustnotsharearaw-v0boundtest. Potentialnextdiagnostic commonclipplane rejectionofcaptured16/40indexedpositions versusGPUzeros;requiresrecordedactualstride/count (includingpackedlayout),capturedconstantwindow,finite/roundingguardandGPU-ownedlifetime. cmd_tcurrentlyretainsnoalwaysvalidvertexbytebounds(geometry_bytesistraceonly),so do notblindlywalkstreamsor reusegeometryhashfields. No culling/diagnosticcodeimplemented inthis audit.

Perf222 settled-pod.png frame8669 confirms pod. Preserved settled-pod.log/settled-summary.json:1200 intervals end7200–8340 mean73.49003ms/13.6073FPS,p5071.918,p9583.222,p99124.297,max153.128,20>100ms,0>200ms,all>50ms. No whole-frame gain. Queue overflow absent, culling active(~7350 skipped/60frames). Existing VIOLATIONS counter means zero proxy samples despite positive own samples, not queue corruption; own samples win in apply_result. This reinforces that proxy/zero-sample results cannot be treated as universally conservative visibility. Sent5sforward/2sAR/2span with neutralpadfinally; after-move-fire.png frame9473 captured. Observer3366553/3367882 and keepawake3092309 remainlive; no restart/newpackage/push. No15minactivecombat qualification. Next conservativeworlddrawcoverage diagnostic must preserve captured-stream bounds and original draw order.

Worldcoverage diagnostic first unit implemented: runtime/xv_clip_diagnostic.h and tools/tests/clip_diagnostic.c. Pure indexed F32xyz/four-row common-x/y-clip-plane classification, bounded byte ranges/stride/offset/indexcount, finite and broad rounding margins. Diagnostic only: not a certified GPU numerical error bound, not draw admission, no depth-plane assumption. Host ASan/UBSan bounds/invalid/boundary tests passed. ARM fixture built and Pi execution on cores0/1 passed with the same checks. Not integrated/built into Vita. Need capturemetadata/provenance and capturedconstantwindow integration on sampled census frames, then compare classification to actual GPU nonzero samples before contemplating skipping. Lastperf222movement screenshot confirmsoutdoors/AR38rounds, no crash onshortsequence; noAI/audio/longrun qualification.

Perf223clipcensus candidate integrated: cmd_t retains owned stream0 bytes/actualstride (packedprefix16 accounted), completion afterGPUfence classifies only auditedhalo_vs16/40F32xyzwithmatchingreplaystride/fourcapturedrows. Reporteligible/outside/indices/GPU-nonzerocontradictions. No draws skipped, nodepthplaneassumption, numericalmarginnotcertifiedGPUbound. Immediateexcluded. ExpandedhostASan/UBSan+PiARM testsall4planes,mixedplanes,unalignedpackedinput,bounds/nonfinitepassed. Firstbuild76751passed; finalstride/eligiblecountbuild67323passed. Code-onlyclip-census-candidate/xita-perf223c.vpk34747478bytesSHA5c5c1851c7ca68f3edf69c4fec89b726716557733383bbd1d2cdfddf7bdfef76,unchangedcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897/noncodeentriesverified. Preserved222full-before223.log,stoppedexactobserver3366553/3367882intentionally. Announcedrestart; deploysession55530live,clip-census-candidate/deploy.log. Needbootconfirmationthenordinaryisolateda30launchwithsamequalifiedoptions+FRAG_CENSUS120; collectqualifiedsceneclipcensus, especiallyeligiblecount/nonzerocontradictions. DiagnosticFPSnotcleanbaseline. Keepawake3092309live. Nopush.

Perf223deploy55530completed0:SHAverifiedslot1bootconfirmed. Startedordinary20minuteisolatedrunperf223-clip-census-20260925,clip-census-candidate/gameplay-driver.log,FRAG_CENSUS120 withallpriorqualifiednative/deferoptions,priority0,expensivesamplingoff. Navigation/loadingpending. Sourcecommit087d422localonly. NoFPSclaim.

Perf223hardwarecensus reachedpod,scene.png frame5921verified. early-pod.log/early-summary.json:6samples5280–5880,40eligibleworlddraws each,only4commonxyoutside/312indices,0GPU-nonzerocontradictions. Shortforward5s/AR2s/pan8s succeeded,outdoor.png frame6648AR35rounds. Firstoutdoorssamples38eligible/4outside498indices; preserveoutdoor-capture.log/summary forfullcounts. NoFPSgain:diagnostic only. Simpleperdrawfrustumrejectiondoesnotexplainmost38zero-sampleworlddraws,andscanningallverticesforonly4drawsisnotyetjustifiedasoptimization. Needdeepervisibility/CPUworklead, notskipzerolastframe. ReprocessedexistingPiownerprofilecurrent-owner-profile.txt:3322samples,35.6%libcsyscallmostlypthread_sigmask(hostfibers),9.5%udivmoddi4withmostlyunrecoverableLR. Host-specificscheduler/librarycostsareNOTVitabottleneckproof;do notprojecttime. Keepawake3092309andrun3380212/session26153live.

Nextcandidate(uninstalled):runtime/xv_vertex_capture.c XV_CAPTURE_PARTIAL_WAIT opt-in0default. Queue-onlyfull withworstcasearenaheadroom waitsforatleastonecompletion,collectsFIFOcallbacks,doesnotretireCPUarena/GPUreuseaddresses;reprobesreuseaftercallbacks. Arena/resourceboundarieskeepfull drain. Outdoorcapture~234queue-onlypressure/60frames promptedthis;notprovenwaitduration/FPSgain. Newcounterreportsqueue-slotwaits. tools/tests/vertex_capture.c testsoptionon/offqueue/arenaanddeterministic32pendingworkerheldafterfirstcompletion:33rdsubmissionreturnswith31oldjobsstillpending,snapshotsuntouched;finalall33correctcallbacks/results. Full24confighostASan/UBSan run48362passed. Initialsuiteuncoveredpreexistingreuse0missingcap_census_notefallbackandstalesparsereusetest;fixedfallbackandtestnowassertsCPUreuse+referencedrowcontents,notunspecifiedholes. PiARMfirstcompilefailedGCC13missingvld1q_u8_x4;existingrecomp/host/neon_x4_compat.hpreincludefixes(67136passed). Piqualifiedallfeaturesfixture21377completed0oncores0/1,partial-wait-pi.log;noVita build/deployyet. Pihealth40.4C,throttled0x0,~3.4GiBavailable,idlebeforetest. Keepawake3092309live.

Perf224fullVitabuild77322completed0. capture-partial-candidate/xita-perf224c.vpk34748018bytesSHAd7614b196bef931640b8609b90a2f1ac16eeae73e5a86f7ce5917b33e901e46e,unchangedcontract/noncodeentriesverified. Preserved223full-before224.log;stoppedobserver3380212/3381546intentionally. Announcedupdate;deployment79544active,deploy.log. Needbootconfirmandordinarya30isolatedlaunchsameoptionswithFRAG_CENSUS0andXV_CAPTURE_PARTIAL_WAIT1. UserexplicitlyrequestsPi-firsttheorytestingandrecommendeduses;nextcaptured-preparationreplaycanreuseexistingmeshformat+archivedprivatecaptures(492files22.4MB in2026-09-05-224334-lighting-investigation/mesh-paired-valid-callback). Existingmeshcapturesstream0/constants/indicesonly,notfullframe/resources;donotclaimfullrenderreplay. ConsiderproductioncapturefixtureasARMbackendwithboundedreaderandreferencedvertexchecks. Needfreshcampaigncaptureslaterforrepresentativecurrentworkload. No capturedataingit.

Perf224deployment79544completed0:SHAverifiedslot0bootconfirmed. Startedordinary20minuteperf224-capture-partial-20260925launch,gameplay-driver.log,isolateda30-perf211,FRAG_CENSUS0,XV_CAPTURE_PARTIAL_WAIT1 andallqualifiedpreviousnative/deferoptions. Needstartup/actualpartialwaitcountsandqualifiedwholeframetimes;no gainclaim. Archivedmeshformatreaderaccepted492/492files,strides488x32+4x16,max265728vertexbytes;capture-partial-candidate/archive-inventory.jsoncontainsprivateSHA/provenance. Readvalidationisnotproductionreplayverification.

Perf224settledpodscene.png frame6375verified. settled-pod.log/settled-summary.json1200Presentintervalsend5400–6540:mean74.33343ms/13.4529FPS,p5072.254,p9588.755,p99109.110,max153.565,20>100ms,0>200ms,all>50ms. NoFPSgainversus22213.61;notqualifiedoptimization. Partialflag1active~7500–10600slotwaits/60frames,fullcapture drains~60/60 versusprevious~294;recorddrainlatest4.9–5.28ms/frame. Morepartialwaitsarecountsnotdurations. Hypothesisnextbatchcompletions8toamortizescheduling,beforeanotherVitadeploytestPi. Current224run3397019/watcher3398010/session93156live,keepawake3092309live.
Addedtools/replay_vertex_capture.py andtests/vertex_capture_replay.c,reusesactualproductioncapture/uploader/copyworkerfixture. Private492archivedmeshesvalid;full/sparse/packedoriginal8drawbatchhostASan/UBSanandPi0/1passed1476preparations0mismatch,checkedbytes20097280/9952352/10048768. Callerinputoverwrittenaftercapture;compareonlyreferencedbytesforsparse. New64drawbatchfinalhostASan/UBSanpassedbothoriginalandpartialpolicy;4malformedinputcasesaftervalidpendingjobexit2nosanitizerfindings. FinalARMbinary21232builtpass,needtransfer/rerun64batchbothpolicies. Archiveddataonlyvertexstream0—notshaderexecution/fullframereplayorVitaperfproof. docs/vertex-capture-replay.md. Noassetsingit.

2026-09-25 continuation: previous status-only goal turn was no progress; resumed concrete validation. Host batched-wait suite session4223 completed exit0, all24 sanitizer configurations. New XV_CAPTURE_WAIT_BATCH=1..16 (default1, partialwait still opt-in) waits for bounded completion frontier at queue pressure; deterministic tests exercise1/8/16. Cross-built replay-arm-batched and ran Pi cores0/1 all three sizes, full/sparse/packed each492 inputs:4428 preparations,0 mismatches, session62450 exit0, capture-replay-candidate/batched-pi.log. CLI now exposes --wait-batch. This remains uninstalled and has no speedup evidence. Final previous64draw replay bothpartialpolicies also completed0. Old gameplay observer and keepawake exited after interruption; new keepawakePID33070/session99578 confirmedlive. Vita status224/frame116094/awake3596. No new deployment. Next: measure preparation-only costs excluding file IO/validation on Pi, then select one ordinary Vita candidate if justified. User Discord FPS reports are observational, not substitutes for a30 completion criteria.

Continuation timing progress: replay now preloads64mesh batches before submission and optional XV_REPLAY_TIMING1 measures capture+join only, excludes IO/validation/poisoning, includes initialworkerstartup. Normalmode stillpoisons; changedhostASan three modes passed492each. Pi7alternatingtrials per3modes/3batches, all30996preparations exact, timed-pi.json. Fullmedians58.299/58.634/58.562ms, sparse58.579/58.290/58.452, packed43.482/43.342/43.319; overlappingranges/no meaningfulgain. Separatelexicalfullmodepressure check2partialwaits8drains, so corpusweakforsaturation; nohardwaredeploy/noFPSclaim. Need representativequeuepressure measurement/currenta30inputs or next evidenced deferred-recording contention target rather thanclaiming batchingqualified. Keepawake33070live.

Continuation queue evidence: perf224 settled-pod log has~9400partialwaits/60frames and~14500jobs butcapture/worker/jointimes0 because XV_VERTEX_CAPTURE_TIMING default0. Added opt-in existing-clock split cap_partial_wait_us/cap_full_wait_us and reporttimingenabled, noadditionalclockcalls, sameownership. Host24configASan/UBSan session86038completed0; finalreportformatcompile via replay passed. Freshremote log session1208stillrunning (15MBpartialfile), do not restart solely on observationtimeout. Need finishlogtransfer, build/deploy timing+batchcandidate and representativeVitatest; no measuredwaitmsyet.

Perf225 build2731 completed0. capture-batch-candidate retained224stage, onlyruntime/xv_vertex_capture.c+version225updated. Codeonlyxita-perf225c.vpk34748506bytesSHA29d635ab800d622b26deafd62f7220dd6c3f7692c4d5e597c91a9df6fd7bf989, unchangedassetcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897; otherentriespreserved. Deploy56772completed0/verified/slot1bootconfirmed. Fresh224log1208completed38822159bytes current224.log. Announcedinterruption. Startedcapture-batch-candidate/launch.sh ordinary20minutea30isolatedrun perf225-capture-batch-20260925, samequalifiedoptions, partial1batch8 and XV_VERTEX_CAPTURE_TIMING1, FRAG_CENSUS0. gameplay-driver.log. Needconfirmstartupenvandactualpodsceneafterloading, thenmeasurequeue-only/fullwaittimes. Diagnosticclocks addoverhead, notcleanFPSqualification. Keepawake33070running. NoFPSgainclaim.

Perf225 launch followup: session84091/PID44930 live, watcher45827live, keepawake33070live. Status225frame174awake3597verified, env receipt confirms allrequestedoptions includingpartial1batch8timing1. Firstwatcht+30s6784lines loaded0active0director0; stillload, notvalidsceneFPS. No restart. Privatecapture-batch-candidate/summarize-waits.py extracts last10enabledtimingreportsperframe; use onlyafterqualifiedpodscene. Nextpollsame84091/log, loadingtypically2–3minafterwatch, screenshotonceonloadedscene. Goalnotmet; currentturnverifiedwaitplusenv/status evidence.

Perf225 reacheda30pod verifiedpod.pngframe6157. early-pod.log420intervalsend5520–5880mean76.9324ms12.998FPS,p9583.825,p9998.864,max161.508,3>100ms,all>50ms. Diagnosticclocksactive;noFPSgainclaim. Settledlastfourqueuewaits13.39–14.13ms/frame,full0.0075–0.0377; earlyscene17–22ms partial. Workerelapsed15–16ms/frame,capture~1.7ms. Crucialnextlead: batch8still signalsdone on every completion becausecap_waiting isrearmedon eachwake; logs7233–7727done signals versus909–970partialwait invocations per60frames. Batchingreducedownerreturnsbutnotintermediatewakeups. Target-aware completionnotification could signalonlyatrequestedfrontier, preserving RMWlost-wakehandshake andtimeoutfallback. Mustdeterministicallytest1/8/16frontiers/wrap/failedsignal/races beforedeployment; donotremoveownershipwait. Run84091/watcher45827continues,keepawake33070live.

Completion-frontier implementation: cap_wait_targetatomic publishedbeforeownerarmingRMW, workerfetch_or0RMWon everycompletion preserveshandshake, checksunsignedhalf-range completion>=target thenexchange0/signalonlyatfrontier. Partialtargetretired+batch,fulltargetsubmitted; noownership/drainremoval,timeoutretained. Directtestsallbatch1..16ordinary/wrapincludingtarget0; existingforcedracesretained. Full24hostASan/UBSan96527exit0; Pi all-featurefixture83975exit0 cores0/1 (frontier-pi.log). Perf226retained225stagebuild74799started, capture-frontier-candidate/build-x87.log; NOTpackaged/deployedyet. Perf225stillrunning~13FPSpod; preservefinal225logs beforeupdate. Needpoll74799,packagecodeonlyusing225base,announce/deployandordinarytest.

Perf226build74799completed0; codeonlypackage capture-frontier-candidate/xita-perf226c.vpk created using225assets, receipt in package-receipt.json. Not deployed.

Perf225final-before226.logpreserved,1200intervalsend5520–6660mean77.20719ms12.9522FPS,p9584.962,p99124.09,max183.213,16>100ms,0>200ms,all>50ms. Diagnosticrun,noimprovementqualified. Intentionallystopped44930/45827observersbeforeupdate. Perf226deployment34611live afterannouncedinterruption; package34748534bytesSHAc334f901dae79d368ac2227ae601b75f69de4e9618b46bc069c06acfe61c4297. launch.sh preparedsameoptionsas225includingtiming1batch8,tagperf226-capture-frontier-20260925. Needbootconfirmationthenlaunch.

Perf226deployment34611completed0, SHAverifiedslot0bootconfirmed. Startedordinaryisolateda30launch capture-frontier-candidate/launch.sh (gameplay-driver.log), tagperf226-capture-frontier-20260925, same225timing1batch8settings. Needpollnewdriver, confirmenv/loading/pod, then signalcountandpartialwaitduration/wholeframeanalysis. No gainclaim. Keepawake33070live.

Perf226followup: driver45515/PID53814 andwatcher54720confirmedlive. envreceiptconfirmsbatch8timing1same225options; watcherfirstt+30s loaded0active0director0, notgameplay. Keepawake33070live. No restart. Prior225threadlogvertexcapturecore0priority153, recordingconfigured152, pump160; possiblepreemptioncostofperjobwakes. Needwaitload~150s thenactualpodsceneandcomparefinal225queue/donesignals; no226measurementsyet. Previousgoalturnprogress(deployment);thisverifiedwait.

Perf226 reachedpod verifiedpod.pngframe6006. early-pod.log/early-summary.json420intervalsend5520–5880mean77.07222ms12.97484FPS,p9584.391,p99118.548,max149.426,7>100ms. Matched225earlysample76.9324ms13.00: nooverallgain. Notificationchangeworks: signals~1040–1227for~1040–1226partialwaits (previous225~7700signals/970waits),failed0. Recentsettledpartialwaits9.20–10.71msvs225~13–14,full0.016–0.067ms. Localreductionnotwholeframegain. Latestscenehelpercpu49.69ms,wall73.60,internalwaiting~23.91,ownerwait15.51;recdeferdrain0.30–1.17ms,0fullqueuewaits;vertexworkerelapsed17–18ms (preemptionincluded). Needlongersettled1200sampleandordinarymovement, then inspect remaining scene criticalpath/native repeatedwork. Don'tkeepchasingnotificationcountsasanFPSproxy. Run45515/PID53814watch54720continues,keepawake33070live.

Perf226settled1200intervalsend5520–6660mean77.35741ms12.9270FPS,p9585.481,p99122.854,max279.409,24>100ms1>200ms. NoFPSgainvs225. Executed5sforward/2sARfire/8scamerapan, allremotecommandsreturned0; outdoors.pngframe8182verifiedoutdoorgeometryandAR36rounds (was60), noobservedcrash. Not15minactivequalificationorAIverification. Staticnexttargetaudit:70110document's7A960costisinclusive, dispatcherhas4routes7A130/7A1F0/7A3D0/7A460. EachroutecontainsSetStreamSource/SetIndices/DrawIndexedVerticesHLEloopplusguestpointer/indexprep; don'trewrite7A960wrapperalone expecting40%gain. Private draw-dispatch-audit.json recordsroutes,HLEtargets,backedges. Nextmeasure/rankroutebodyvsHLEcostusingexistingphasehooktool/Pi replaybeforechoosingnativebatchboundary; preserveordering,callbacks,preemption,gueststate. Live226run45515 continues,keepawake33070.

Drawrouteprofilingimplementation: patch_scene_phase_timers.py --hle-calls wrapsoriginalXV_HLE_CALLwithoutchangingmacrosemantics; fixedlastshardfunctionend=-1slicing. Test4modeschecksunchangedinstructions/order/unselectedfunctions/endbrace/idempotence;pass. docs/draw-route-profiling.md; commitfe49bd0. Private227stagecopied226, onlycode012timers/versionchanged:4routecallsunder7A960and6HLEsitesineachroute (duplicatedloopbodies),28sites. Build51735running,draw-route-candidate/build-x87.log. Needpollcompletion, codeonlypackagebase226, then diagnosticlaunchXV_SCENE_PHASES1 (othersamequalifiedsettings; canturncapturetiming0asquerynowanswered). NeedrouteinclusivevsHLEchildren,notmislabelwallremainderasCPUself. No227deploymentyet;226stilllive.

Perf227build51735completed0. Codeonlydraw-route-candidate/xita-perf227c.vpk createdfrom226assetentries, contractunchanged, SHAreceiptpackage-receipt.json. Preserved226final-before227.log, stoppedconfirmed53814/54720observers, announcedinterruption. Deployment54790live. launch.sh samequalified226options exceptXV_SCENE_PHASES1 andcapturetiming0; diagnosticonly. Needbootconfirmedthenlaunch,pollactualpod,extract7A130/1F0/3D0/460HLEchildrenandsceneparenttimings.

Perf227deploy54790completed0/verifiedslot1bootconfirmed. Started draw-route-candidate/launch.sh session14343, gameplay-driver.log, tagperf227-draw-route-20260925, phases1capturetiming0. Useraskedmultiplayerdifficulty; docs/adhoc.mdconfirmsoptintransportimplementedbuttwoVitaplayunverified. Keepperformancegoalactive.

Perf227podverifiedpod.pngframe5827,early-pod.logpreserved. First7A130settledwindows2.53–2.67msinclusive,drawHLE1.34–1.38ms,index0.18–0.19,stream0.18,remaining0.78–0.97ms. Notlarge40%guestloopopportunity. ApparentA26B0self8.29–8.76msISNOTpureownbody: staticauditfoundA2380callhasconditionalXV_MODEL_UVscopebetweenpushandcall, soexistingdefaultphasepatchmissesit; A2380childrenwronglyattributedupwards. Need--any-call instrumentation A26B0,A2380 toclosegap, native70110wrappercallincludedifbare. DoNOToptimizeA26bodybasedon8msself. Existingtoolalreadysupports--any-call. 227run14343/PID62814watch63738livekeepawake33070. Roadmapco-opclaimcorrectedcommit5a1ee26whileloading; noH2changes.

Materialboundaryfixprogress:228retained227stage,phasepatch--parentsA26B0,A2380--any-call--hle-calls installs8+6balancedcalls; instrumentation-check.json provesallnon-timertokensunchangedversus227. AddedconditionalUVhookregressiontestalongsideexisting4modes;2tests pass. Build65037live material-route-candidate/build-x87.log,version228;notdeployed. 227settled-pod.log/route-summary.jsonlast10reports7A130median2.41inclusive0.73remainder supportsnotprioritizingdrawlooprewrite. Needpoll65037thenpackage/deploy228afterpreserving227logs. Run22714343stilllive,keepawake33070.

Perf228 build65037completed0, postbuildcode015still8+6balancedtimerboundaries. Codeonlypackage material-route-candidate/xita-perf228c.vpk SHA f94355dac5fa52fdd1df9deb785df4d7f84fd8342b651986320cbf3d46834d11, unchanged227assets/contract, receiptpackage-receipt.json. Preserved227final-before228.log, stoppedconfirmed62814/63738observers. Announcedrestart;deploy85714live. launch.shpreparedsame227options,tagperf228-material-route-20260925. NeedverifiedbootthenlaunchandreadcorrectedA26B0/A2380/70110inclusivehierarchy. NoFPSgainclaim.

Perf228deploy85714completed0,34749330bytesSHAverifiedslot0bootconfirmed. Startedordinaryisolateda30launch material-route-candidate/launch.sh,gameplay-driver.log,tagperf228-material-route-20260925;phase1capturetiming0same227. Needpollnewlaunchhandle, envreceipt andpodscene, thencorrectedmaterialtimings. Keepawake33070confirmedlive.

Profiler aggregation correction: phase_report previously subtracted every outgoing child from a single incoming parent/callee entry. Shared callees could therefore show children exceeding their displayed inclusive time. Sum all incoming entries before reporting inclusive/self, and label the raw top list as parent>callee. The extracted-production regression passes ASan/UBSan with a synthetic shared callee (5 ms inclusive, 3 ms children, 2 ms remainder). This corrects reporting only; elapsed remainder still includes waits, preemption and instrumentation. Previous multi-parent self figures cannot justify a native replacement. perf228 remains around 13 FPS in its instrumented settled run; no target attainment claimed.

Perf229: reporting correction committed6096716; retained228 stage plus xk_scene_thread.c and version only. Build13897 completed0. phase-report-candidate/xita-perf229c.vpk SHA b5727746bcd56933ee56a5c97cc7dbcf6fc1114e4cb1c825f5b815aa57a074a0; only game-a.self and boot-game.txt differ from228, contract unchanged. Upload5741 completed0; payload34749218 bytes SHA983102f3d8938b40339d6a89518d25c6bc092791fb6e7f5ef818cf0de9140b03 verified, slot1 bootconfirmed. Prior228 final-before229.log preserved, confirmed observer76780/78202 stopped; keepawake33070 retained. Launched phase-report-candidate/launch.sh, ordinary isolated a30 run tagperf229-phase-report-20260925; same settings as228. Next inspect live driver/env receipt then loaded pod and corrected inclusive/self report before choosing material packet native work. 6EFC0 constructs shared render packets, so not safe to memoize merely by material identity (see model-routing-audit-20260919). No FPS gain claimed.

Access restored after sandbox interruption. Vita responded perf229 frame69497, lease3600 renewed awake3599. Old keepawake33070 absent; anchored process check found no duplicate. Started /tmp/xita-keepawake-20260925.py new exec29245 and observed lease renewed. Native material internal profiling candidate /tmp/xita-packet-profile-20260925/native-profile.patch passes900 hostcases, disabled host.text identical, ARM static harness built. Copied native-profile-arm to pi:/tmp/xita-native-profile-arm-20260925 and executed taskset0,1 300cases seed1: zero native/verify mismatches,10266 balanced profiler events;50GENrestarts. Pi log at scratch/pi-test.log. Candidate not merged/deployed; add/check safe scene profiler depth handling before broader instrumentation. Hardware capture59944 in progress current.png/current.log under phase-report-candidate. User asked restart: no needed, connection healthy.

Perf230 preparation: commit7485ac4 integrates opt-in native70110 CALL/HLE/UV/fog/sampler/preemption timing and scene-stack overflow pairing/report guard. Extracted production overflow test passes ASan/UBSan (20 nested scopes on each thread, correct outer attribution and recovery); report regression passes. Pi native candidate300cases zero mismatches. Screenshot current.pngframe70024 shows OUTDOOR a30, notpod; do not compare withpodbaseline. Remote log59944completed31665778bytes current.log. Keepawake29245active. Retained229stage native-material-profile-candidate build93759running version230; original build options plusXV_NATIVE_70110_PHASES1, source changesMakefile/native70110/scene_thread. Confirmednativecompileflag1. package.py/launch.shprepared, notpackaged/deployed. Next poll93759, package codeonly against229 and verify assetdelta, then announce restart/deploy after retainingcurrent229logs.

Perf229 late outdoor capture current.pngframe70024 confirmsoutdoors. current.log last20completewindows end68820–69960,1200intervals:mean66.1324325ms15.12117FPS,p9572.062,p9979.665,max109.795,3>100ms,all1200>50ms. phase-report-candidate/outdoor-summary.json. This isdiagnostic currentoutdoorstate, notmatchedpodcomparison or proofnewFPSgain. Perf230 build93759stillrunning; process135163make->cc1PID136068confirmedlive99.5%CPU at2m08s1.23GBRSS. Do not restart. Keepawake29245renewing,status229frame73370awake3583. Package/deploypending.

Perf230build93759completed0; slow compiler wascode011, notnativeunit. Compared allguestcode shards229vs230 byte-identical. Package63174completed0: xita-perf230c.vpk55068370bytesSHAe0bda88f64fc4265473f4bd87a3bd12e035991327d9cf43cc3c8244016866451, unchangedcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897, onlygame-a.self/boot-game.txtchanged verified. Announcedrestart. Upload65210running34759306bytepayload. KeepawakePID134059exec29245alive. Nextpoll65210 bootconfirmed then native-material-profile-candidate/launch.sh, sameisolateda30options and newtagperf230-native-material-20260925. Current229logandoutdoor screenshot retained; noFPSgainclaimed.

Perf230upload65210completed0 verifiedpayloadSHA456ca956c6f0d326727ffa764ded1ecb8b5e070e15a908165e6516f930c5df08 bytes34759306 slot0bootconfirmed. Started native-material-profile-candidate/launch.sh isolateda30run, gameplay-driver.log. Next confirmenv/status, loadedscene screenshot and firstnative70110childtimings, check omitted-depthwarnings. Keepawake134059 remains. Goalnotmet.

Perf230launch93037PID139128 watcher139953live. Status230frame424awake3594; envallconfirmed. Atwatch180sloaded1active1director1. New70110breakdown22children: settledearlyincl17.47ms remainder6.72;7A9602.98,803601.43,1820A01.07,6F3400.88,UVhelper0.84,fogbegin0.45,183F900.43,11BD00.42. Nativeinclusive muchlargerthan229; added per-call clocks and phase_end linear512-entrysearch can inflate parentremainder; notpurearithmetic orspeedregressionproof. No omitted-depthwarnings found. early-loaded.logretained. Screenshot44032pending loaded.png. Nextinspectimageandsettledsample; consider diagnosticaggregationlookupcost before attributing6.7ms nativebody. Keepawake134059exec29245alive. Goalunmet.

Perf230pod confirmedloaded.pngframe5970. Lastnative70110~17.28ms remainder6.63 vsold8msincl; diagnosticclock/call/spill/bookkeeping overhead material. Implementedvalidated256hintslotsperthread forphase_end lookup, commit61f1e83. Productionvslinear1M nested samples404keys/thread/recordreordering matcheshostandPi; Pi CPU0 singletrial575.621mslinear234.227mshint, supportingdiagnosticonly. ExistingdepthandreportASanregressionspass. Perf231retained230stageonlyscene_thread/versionchanged, phase-lookup-candidate build41108active; package.py andlaunch.shprepared. 230settled-pod.log/settled-summary.json retained. Nextpoll41108, packagecodeonly230->231,announce/deploy afterpreservinglogs; run ordinarya30capture. Keepawake134059exec29245alive,230launch93037live.

Perf231build41108completed0,package5650completed0. phase-lookup-candidate/xita-perf231c.vpk55068958bytesSHA332d5c11185f2c06ec9b9d4523aab04044e39102c9d587b453202803ea14080c;contract unchanged, onlygame-a.self/boot-game.txt changed verified. Preserved230final-before231.log/material-summary.json:last10native70110incl17.14 remainder6.58,draw2.93texture1.40vertexconstant1.04,nooverflowwarnings. Stoppedconfirmed230driver139128/watcher139953. Announcedrestart; upload18976live payload34759278bytes uploaded, waitingverify/activation. Nextpoll18976 then phase-lookup-candidate/launch.sh ordinaryisolateda30. Keepawake134059exec29245retained.

Perf231deployment18976completed0 payloadSHA9b4dda4008a54ba2274ff8dc1c86cd17140d01edcb92a601c32852e9ea98fb72 slot1bootconfirmed. Launchedphase-lookup-candidate/launch.sh; gameplay-driver.log. Awaitenvreceipt/status/loadedpod screenshotandnative70110breakdown. No gameFPSgainclaim.

Perf231run81905live driver145785; envreceiptconfirmed; watcht+90stillloading. Parallel shaderconstant audit foundfinite identicalupdatesstillmarkdirty, so deferredrecorder copiesdirtyboundingrows beforeconsumerdedup. AddedUNCOMMITTED opt-inXV_VSC_EQUAL (requires existingXV_REC_VSC2), bitwiseequalfiniteone-pageupload returns2 andskipsdirtyextension whilepreserving pendingrange; NaN/Inf/pagecrossing keeporiginalfallbackandlogging; existingmode1unchanged. Counters[vsc-equal]checks/skips/bytesevery60frames. tools/tests/test_vsc_equal.py extractedproductionsetter comparesbaseline/candidatekernelanddelayedconsumerstates10000steps,516skips, hostASan/UBSanPASS. CrossbuiltARM at/tmp/xita-vsc-equal-arm/test;Pi86274 executionresultpendinginspection. Notbuilt/deployed; nextdocument/commitafterreview, runPi/realrecordertimelinevalidation andjudge231settledtimings beforecandidate232. Keepawake134059alive.

VSCequalityextendedtest extractsproductionSetAllConstants/SetTrackedConstants andchecksrecordedrows+generation withinline/deferredmirror sourcechanges/UIinvalidations, hostandPi10000stepsPASS516skips. Commitc97de2b opt-inXV_VSC_EQUAL1, requiresREC_VSC2alreadyincfg; counterbytesare settercopyavoidance, notqueuebytes. Perf232vsc-equal-candidate retained231stage onlyxd3d/versionandnative70110PHASES0flag; deletedexactnativeobjecttorebuildflag0. Build31197active. launch.shphases0VSC_EQUAL1, otherqualifiedoptionssame. This removesheavyinstrumentation; do notattributeallFPSdifferenceversus231toVSC. 231settled-pod.log/summaryretained, timerremainder~5.2ms vs2306.6 confirmsobservercost. 72115screenshotloaded.pngcompleted. Nextpoll31197 package/deployafterannounce; hardwarecandidateunverified. Keepawake13405929245alive.

Perf232 build completed0. Packaged code-only xita-perf232c.vpk55066294bytes SHA f4cfb99f3266b5fcf8d576dba8539fb7d4a2798bf115d3886a775ac3bdfd4cca; assetcontract unchanged775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897. Verified ZIP names identical and only game-a.self/boot-game.txt differ from231. final-before232.log saved3763428bytes. Stopped confirmed231driver145785/watcher146672, keepawake134059 retained. Announced interruption; update session86561 uploading34749718byte payload. Await verified boot before launch. Previous status-only turn no progress; this turn packages/deploys qualified candidate. No FPS claim.

Perf232 update86561 completed0: verifiedtrue, bootconfirmedtrue slot0 payload34749718bytes SHA f0d90d8e5488435af2b14b822958b9405a6022613381527f880b7f5a5c557d5e. Companion1.07 reachable. Started vsc-equal-candidate/launch.sh redirected gameplay-driver.log; isolateda30 run phases0 VSC_EQUAL1. Need qualify loaded scene/screenshot then settled log and counter/frame analysis. Keepawake134059 remains active.

Perf232 hardware observation: launch81316/PID153211 remains live, keepawake134059/session29245 confirmed renewing; manual lease3600 renewed after restart. Startup log confirms REC_VSC2, SCENE_PHASES0, VSC_EQUAL1. launch-check.png frame5001 viewed canyon flyover; pod-check.png frame5880 viewed same stationary AR60 lifepod. early300samples76.42ms. settled-pod.log/summarize.py/settled-summary.json saved: last20 completewindows5640..6780,1200intervals mean76.653788ms13.04567FPS,p9585.307,p99100.551,max186.589,13>100ms,all>50ms. About194.62uploads/frame skipped (37.32%eligible),11382.65setterbytes avoided/frame; queue276.385KiB/frame4185.85constantrows vs231293.195KiB5262.8rows. Different instrumentation and scene draw variation mean no isolated FPS attribution. No meaningful target progress in fullframe despite real traffic reduction; retain opt-in pending active gameplay. No recorder consistency errors in inspectedtail. Next trace submission/waits criticalpath (present~24ms,pump~20ms,scenejoin~10ms overlapping) rather than repeating80360 native already tested slower. Need active gameplay/cutscene distribution, AI/audio/save checks stillunmet. Goal remains active.

Perf232 followup: status8712awake3596, keepawake134059alive. Ordinary movement5s/pan3s/AR2s sequence59057completed0; outdoor-fire.png frame9278 viewed a30 outdoors AR35 vs60; outdoor-fire.log saved2910520bytes. No crash in this shortsequence, not15minutequalification. Reviewed prior219/220/221pumpdiagnostics toavoidrepeatingprioritytests; current slotwait16–25ms protectsfinalnotification. Added opt-in XV_FRAME_QUEUE_TIMING to main.c: publish timestampbeforeexistingrelease, pump-only60packetaggregatesusingexistingnow, noownershipchanges. This measuresmissingpre-submitbacklog,scheduling/display/pacingnotGPUtime. docs/frame-queue-timing.md. Candidate privateframe-queue-candidate copied232stage+main.c; objectcompile78643active, notpackaged/deployed/versioned. Needpollcompile, review build then use newmeasurement inordinarynextlaunch; current232continues.

Framequeue objectcompile78643completed0 against retained232Vita stage. gitdiffcheckpassed. Still uninstalled; no performance conclusion.

Perf233 fullbuild83661completed0; code-onlypackage55066198bytesSHAeaabf483fb96ea5c6ae22d02f38c95a8b77ae6565f5bbee39cc09492694d0506, unchangedassetcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897. VerifiedsameZIPentriesandonlygame-a.self/boot-game.txtdiffer. Saved232final-before233.log3618463bytes; outdoor-summary1200intervalsend10020..11160mean79.6592ms12.5535FPS,p9591.417,p99124.656,max218.037,37>100ms1>200ms. Differentoutdoorviewnotpodcomparison. Stoppedconfirmed232driver153211/watcher154153. Announcedrestart; update99466live. frame-queue-candidate/launch.sh retains232settingsplusXV_FRAME_QUEUE_TIMING1. Waitverifiedbootthenlaunch. Keepawake134059retained. No new speedup claim.

Perf233 update99466completed0, payload34750078bytesSHAbbe284fbf143275b6fbc7a50ea640909eafd116edb1a29b5a22ba623e10be55b verifiedtrue slot1 bootconfirmedtrue. Startedframe-queue-candidate/launch.sh into isolateda30, gameplay-driver.log. Need actualstartupenv, scenequalification and queue-delay reports before conclusions.

Perf233 launch46117/PID162268 watcher163105 confirmedlive; keepawake134059 renewed. startupenvQUEUE_TIMING1verified. pod.png frame5862 viewed stationarylifepod. settled-pod.log2213702bytes, frame-queue-candidate/settled-summary.json1200intervalsend5940..7080mean77.594906ms12.88744FPS,p9587.556,p99116.71,max378.978,16>100ms1>200ms,all>50. queue-summary.jsonlast20reports1200packetsmean32.7895msmax394.6451109>1ms. This identifies significant pre-submitbacklog, not 32msrecoverabletime. Cap0confirmed;remote_frameonlycopiesonrequest. Extendedmain.c queuefields trackfirstinspection andobserved backbuffer/queuelimit/pacinggates; noextraclocks, existingnow, ownershipunchanged. Gatecountsoverlap,wallincludespreemption. Privatequeue-gates-candidate copied233stage+main.c; Vitaobjectcompile6706completed0. Notversioned/fullbuilt/deployed. Needbuild234/reportreasonthenchooseoptimization; do notrepeatprioritytrialblindly. Existing233runcontinues. Goalnotmet.

Perf234 fullbuild15541completed0; package55066254bytesSHA77002ccf59ebb9cbffe872106aaa76615c419b6bd09d10d342f7d41503e0fe55, unchangedcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897. VerifiedsameZIPentriesandonlygame-a.self/boot-game.txtchanged. Saved233final-before234.log2694515bytes. Stoppedconfirmed233driver162268/watcher163105. Announcedrestart. Deploy89037active; payload34750306bytes. Privatequeue-gates-candidate/launch.sh carries identical233settings includingFRAME_QUEUE_TIMING1; summarize-gates.py prepared. Needbootconfirmationthenlaunch andcollectqualifieda30gatecounts. Keepawake134059running,status233awake3580beforeupdate. Previousgoalturnprogress: hardwarequeueevidence+implementednextdiag. Goalunmet.

Perf234deploy89037completed0, payload34750306SHA307f1cec8509d8af2d25dfe9218e21ab31a9d381b3146cfcd599f3fdb272ea60, verifiedtrue slot0bootconfirmedtrue. Startedqueue-gates-candidate/launch.sh into isolateda30. Needpollnewlaunch, confirmenv/loadedpod, then summarize-gates.py + frame_times.py data; no gate resultyet. Keepawake134059 retained.

Perf234 launch30563 remainslive, watcher169922. keepawake134059/status234awake3590confirmed. pod.png frame5658 viewed samepod/AR60. earlysettledlogincludedtransition(first4980)NOTusedasfinalFPS. settled-pod-late.log2343550bytes +gates-summary/settled-summary: last20windows6300..7440mean76.967285ms12.99253FPS,p9585.078,p99118.508,max208.673,16>100ms1>200ms. Queue1200packets1.537249msbeforeinspection31.73939msafter;1086observedbothbackbufferbusy/fullqueue,0pacing. Mainpre-submitbacklogisdisplaypressure,notinitialpumpnoticingdelay. DoesnotyetdistinguishGPUcompletion/callbackeligibility/scheduling. Addedopt-inXV_DISPLAY_CALLBACK_TIMINGthreeclocks/callbackandcallback-owned60trackedframeaggregatesafterexistingrelease. No wait/releasepolicychange. Private display-callback-candidate copied234stage+main.c; objectcompile94450completed0. Notfullbuilt/versioned/deployed. Needbuild235oneordinaryrunwithcallbackflagthenchooseGPUworkvscallbackschedulingtarget. Keepcurrent234running. NoFPSgainorcompletionclaim.

Perf235fullbuild42446completed0, code-onlypackage55066956bytesSHAaa6f646aa5acf475ce17581bc05d6fe84b550c0ec93d7bf70d971692b342474c;unchangedassetcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897. ZIPnamesandnoncodebytesverifiedidentical234. Preserved234final-before235.log2695430bytes. Stoppedconfirmed234driver169080/watcher169922. Announcedrestart; deploy4893active,payload34750566bytes. display-callback-candidate/launch.shsame234settingsplusXV_DISPLAY_CALLBACK_TIMING1; summarize-callback.pyprepared. Needverifiedbootthenlaunch, actualcallbacklogs/podscene beforeinterpretation. keepawake134059retained. No speedupclaim.

Perf235deploy4893completed0 verifiedtrue slot1bootconfirmedtrue, payload34750566SHA406f83a6c621f2454497b002b497140f083c2c760e2ea1b50377aec71d30d511. Starteddisplay-callback-candidate/launch.sh isolateda30. Needpollsamejob/status, confirmenv/callbackreports andqualifyscene, thencallback/gate/frame summaries. NoFPSclaim. Keepawake134059retained.

Perf235callback1 startupstalled: observerlog787lines endingfirst60framereport,no display-callback line; remote status33818timedout/stillpendingcheck, companion1.07reachable. Saveddisplay-callback-candidate/stalled-startup.log. Stoppedconfirmeddriver176136/watcher177184. Recovery.shsame235binarycallback0 launchedsession99008; env0confirmed,status235frame926awake3576responsive. Exactstallcauseunproven; sharedlogger mutex/file IO inGXMcallback unsafe diagnosticdesign. ReplacedcallbackXV_LOGwithSPSCsingle-slotcountermailbox,producerretainsaggregateifbusy,pumplogsafteracquire/copy/release. Variableframes>=60mustnormalizeactualcount. tools/tests/display_timing_mailbox.c hostASan/UBSan+PiARMcores0/1bothPASS1millionordered64bitpayloads/full/empty; session18564completed0. Private display-mailbox-candidate retained235stage+main/header; Vitaobjectcompile19551pendingorcompletedtoolresult. Notversioned/fullbuilt/deployed. Needcompilefinishthenbuild236/deploywithcallback1, retainrecoveryuntilready. Keepawake134059active. No callbacktimingevidence/noFPSgain.

Displaymailbox Vitaobjectcompile19551completed0; diffcheckpassed. Hardwarepath stillunverified.

Perf235callback0 recovery reachedpod: recovered-pod.png frame5934 viewedliveAR60samepod, recovered-before236.log1782710bytes. This confirmsrecovery,noproofexactpreviousstallcause. Perf236mailboxfullbuild76338completed0. code-onlypackage55067216bytesSHA896a4f098f989b4995e437fef77383feda9e1b1a1e59da8e3d973c2a2bd6a1a0; unchangedcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897. Verifiedonlygame-a.self/boot-game.txtdifferfrom235withsameZIPnames. Stoppedconfirmedrecoverydriver177903/watcher179268. Announcedrestart; deploy87469activepayload34750898. display-mailbox-candidate/launch.shsame235settingswithcallback1; parsernormalizesactualreportcount>=60. Needbootconfirmthenlaunch/verifycallbackreports/pod/FPS. Keepawake134059active.

Perf236deploy87469completed0 verifiedtrue slot0bootconfirmedtrue;payload34750898SHAedd2b4367619ccdda43f3fae4e77cfbd9cafbcdc308a319440273e202f9d1fba. Starteddisplay-mailbox-candidate/launch.sh, ordinaryisolateda30callback1. Needpollsamejob, verifyactualcallbackreports andloadedscene beforeconclusions. Keepawake134059retained.

Perf236 callbackmailboxhardwareworks: launch44181/driver183626watch184484advanced, pod.pngframe5982viewedstationarysamepod. settled-pod.log2143024bytes; last20windows5640..6780mean77.541896ms12.89625FPS,p9587.59,p99131.42,max194.993,26>100ms0>200,all>50. Callback1200frames setup0.062568msvblank8.541253msmax27.592. Directcallbacklogging235stallnotreproducedwithmailbox; no15minqualification. Callbackexecutiondoesnotexplainwhole~32msdisplaygatedelay,stillcannotseparateGPUeligibilityvsbeforecallbackpreemption. NoFPSgain. Sourceaudit mainbackbufferusesselectedresolution; oldglobalhalfprecisiontrialunhelpful(docs/hardware-packet-timing-20260918.md),donotrepeat. Preparednative-resolution.shsame236/settingsonlyXV_RENDER_HEIGHT544; 31explicitoptions+XV_LEVEL=32 fitsremoteoverridecapacity. Announcedsinglecoldnative-resolutiondiagnostic,stoppedconfirmedobserver183626/184484,launchednative-resolution-driver.log. Needpollnewjob/effectiveresolutionstartup960x544,qualifysamepod,collectcallback/gates/GPUbounds/rawintervals. Restore360afterdiagnostic; donotattributeview/loadingdifferences. Keepawake134059active.

Perf236native-resolution74560 verified startup cfg360ignoredremote544, effective960x544native; clocksreported444/222/222/166sameas360. native-pod.pngframe5722viewedmatchingstationarypod/AR60. native-settled.log2129667bytes; native-summary840intervalsfrom5760mean131.537313ms7.6024FPS,p95139.955,p99178.875,max279.385,832>100ms6>200ms. Compared3601200samples77.541896ms12.896FPS:large resolution-pathcost. Nativealsoeliminatesupscale,notpurepixel-countisolation;cannotdeclarefullyGPUbound. Callbacklast20reports setup0.060084msvblank7.676323msmax35.432, queuefirstinspection1.862967mshead80.861974ms1122/1200backbuffer+queuelimit,0pacing. Supportsrendering/fragment/framebufferworknextoverblindCPUpriorityadjustment. CompilerO3alreadyon; broadhalfprecisionandreplaceblendtrialsalreadyunhelpfuldonotrepeatblindly. Stoppedconfirmednativedriver187094/watcher187997afterannouncingrestore; restore360.shsame236settings+explicitHEIGHT360 launched, restore360-driver.log. Needverifyrestorationeffective640x360 thennext targetedgraphicscandidatewithcorrectness. Keepawake134059active. Goalnotmet.


### 2026-09-26: material-pixel audit and opt-in cube-coordinate candidate

Confirmed perf236 restoration to 640x360 from its startup log; remote status
remains responsive and the lease refresher remains active. The historical
settled a30 fragment census (frames 5400..8880, 30 samples) now has a private
`world-census-candidate/settled-fragment-programs.json` summary. The largest
reported programs average 0.958 screens / 2 draws (B5691565), 0.736 / 1
(49E75D47), 0.5163 / 75.8 (154066FD), and 0.5 / 1 (842F21C9).
These are rounded surviving-sample counts, not GPU execution times. The
program list is top-N; absent entries are not proven zero. The census blend
bucket includes masked ONE/ZERO replacement writes, so its large blended
fraction does not establish transparent overdraw as the dominant cost.

The B5691565 material has eight combiner stages, four texture lookups, and
cube-mode coordinates applied to a 2D texture. A narrow build-time experiment,
`XV_PS_CUBE_SELECT=1`, selects the same numerator and denominator before a
shared division. Strict major-axis comparisons, tie-to-Z behavior, signs and
Z denominator floor are retained. No approximation, precision reduction,
texture removal, blend changes or draw reordering. It is OFF by default.
Compiler lowering may make this equivalent or slower; no speedup claimed.

`tools/test_cube_uv_select.py` emits the actual old/new helper bodies into a
portable C++ arithmetic harness. PC ASan/UBSan and static ARMv7 hard-float on
Pi CPUs 0/1 each passed 1,378,375 triples (random bits, axis ties, signed zero,
subnormals, extremes, infinity and NaN; finite results compared bitwise,
NaN payload differences ignored). This does not validate Cg lowering or GPU
sampling derivatives. Default generator output remains byte-identical over
1,644 captured texture-mode variants; existing program tests pass 2,292
identity checks and 1,526 source-equivalence cases.

Next: compile ONLY the targeted candidate and corresponding baseline through
libshacccg, inspect generated program resources/code, then consider a bounded
hardware deployment with a rollback and unchanged material appearance. Do not
regenerate/package all shaders or treat Pi arithmetic tests as shader/FPS proof.
Perf236 remains installed; the opt-in shader candidate is not deployed.


### 2026-09-26: cube-select compilation rejected before deployment

Compiled baseline and candidate for both dominant cube-addressed materials in
an isolated libshacccg workspace (`cube-select-candidate/compiler-pref`), without
changing Halo 2 or the physical Vita. All four compiled successfully; compiler
warnings were unused generated locals. The emulator faulted while exiting the
compiler, after its log recorded completion and all outputs were written. No
emulator rendering/performance validation was attempted.

GXP header comparison (primary instructions / temporary registers):
- B5691565: baseline 138 / 17, candidate 142 / 17.
- 154066FD: baseline 130 / 9, candidate 137 / 12.
- Primary/secondary register allocation, phase count and secondary instructions
  were unchanged (24 / 80 registers, one phase, 23 secondary instructions).

Header fields were checked against the upstream Vita3K SceGxmProgram structure:
https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/include/gxm/types.h
These counts do not prove GPU timing, but give no reason to deploy this
speculative rewrite: it failed its intended compiled-work reduction criterion.
Removed the opt-in generator experiment and its dedicated harness from active
source; retained both privately with generated shaders, compiler logs and
`program-comparison.json`. This does not remove any deployed optimization.
Perf236 stays installed. Next target is repeated combiner arithmetic in these
same measured materials, comparing compiled instruction/register cost before
hardware testing rather than assuming shorter Cg source is faster.

### 2026-09-26: nonnegative material-result clamps, perf237 candidate

A second targeted compiler experiment replaces lower-bound -1 clamps only when
combiner input mappings prove the result nonnegative. Unknown signed operands
and biased scales retain their original clamps. Source generator support is
caller-opt-in (`NONNEGATIVE_CLAMPS=False` by default), not a global shader change.
Six synthetic tests cover input mappings, zero constants, partial sign proofs,
scale bias, mux and dot outputs; existing shader-program tests still pass.
Proof-driven emission exactly reproduces the two independently compiled private
candidate sources. No changed texture lookups, coordinates, ordering or blend
state. Finite arithmetic sign proof alone does not establish GPU visual results.

Compilation diverged by material:
- B5691565: 138 -> 135 primary instructions, 17 -> 13 temporary registers;
  binary 1716 -> 1684 bytes. Selected for hardware trial.
- 154066FD: 130 -> 133 instructions, 9 -> 17 temporary registers. Rejected.

The compiler baseline for B5691565 is byte-identical to perf236's deployed
embedded shader (SHA ac4b8e6f93aa41b14bb18602194bc60290cd0e2ea90a6381d02a02a58762cb72).
Perf237 changes only this embedded `_na` program, plus build/version metadata;
external app assets and update contract remain unchanged. Log confirms perf236
actually binds that variant against halo_vs_09. Build under
`range-clamp-candidate`; known-good package remains perf236c. Do not claim a
speedup from instruction/register counts alone.

Latest settled baseline, screenshot-confirmed pod at 640x360, frames
14880..16020 (1200 intervals): 77.087804 ms / 12.9722 FPS, p95 85.778 ms,
p99 109.246 ms, max 169.976 ms; 20 intervals >100 ms, zero >200 ms.
Private before237.png/log and baseline-summary.json preserve evidence.

Perf237 built successfully and was deployed with remote update verification and
confirmed boot (slot 1). Payload 34,750,898 bytes, SHA
40d4cb135fdaf1affb5a7195937f09a4ff11e9e5e7b87ac553767ace378400e1.
Package xita-perf237c.vpk is 55,067,387 bytes, SHA
05d338e3928715437aab7948950169c03cc7f86f2850f4cf2b980ba8b614c51a.
Only game-a.self and boot-game.txt differ from perf236c; asset contract unchanged.
Scripted launch is live as exec session 89349, driver
range-clamp-candidate/gameplay-driver.log, tag perf237-range-clamp-20260926.
Keep-awake PID 134059 remains active. Next check that this existing launch
reaches the pod, capture a meaningful screenshot, then collect settled frame
intervals; do not relaunch because observation expires. No hardware speedup or
visual acceptance is established yet.

### 2026-09-26: perf237 hardware result and texture-memory follow-up

Perf237 pod237.png (frame 6551) shows the same pod/AR60 view with no obvious
material regression. Settled 1200 intervals, window ends 5880..7020:
77.602135 ms / 12.8862 FPS; p95 88.205, p99 104.879, max 200.376 ms;
15 >100 ms, one >200 ms. Against perf236 77.087804 ms, this establishes no
measurable FPS gain from the smaller shader. Keep it provisional in the next
candidate (real compiled-work reduction, no claim of hardware benefit).
No 15-minute active gameplay or complete rendering qualification yet.

Texture placement audit found the 32 MiB `g.dec_base` pool allocated through
`USER_RW_UNCACHE` in main RAM. Existing startup reports show 89,088 KiB free
CDRAM after graphics initialization. A new opt-in allocation path moves ONLY
this pool to CDRAM: same capacity, textures, layouts, upload/lifetime/purge
logic, GPU read mapping and publication barriers. Vertex/index/command buffers
are unchanged. CDRAM allocations round to 256 KiB; failure to allocate, obtain
the base, or GPU-map frees the failed block and falls back to the previous main
RAM allocation. Logs identify the pool actually used. CPU-side texture writes
and alpha scans may become slower; benefit is not assumed.

`XV_TEXTURE_CDRAM_DEFAULT=0` preserves normal builds; startup
`XV_TEXTURE_CDRAM` overrides it. Make tracks default changes through a config
stamp so an incremental build recompiles the allocator. Perf238 opts in with
build default 1, preserving the 32-key remote environment capacity. It retains
perf237's one embedded shader. Host ASan/UBSan tests cover both defaults,
overrides, alignment and allocation/base/map failure cleanup/fallback; ARMv7
static harness on Pi CPUs 0/1 passes the same allocator logic using API mocks.
These mocks do not verify actual CDRAM mapping or GPU reads; hardware is next.
Native Vita build passed in texture-cdram-candidate. Package contract unchanged;
only game-a.self and boot-game.txt may differ from perf237. Do not attribute a
speedup until live logs confirm CDRAM and settled gameplay improves.

Perf238 upload/apply is LIVE in exec session 30080 (started after deliberately
stopping perf237's watcher 204862 and launcher 203950). Poll this same session;
do not duplicate the upload. Package 55,067,479 bytes, SHA
477ce1a294461debf67c7b94cdf3044a977f8e5ac34b274a47f786cb00db6e59.
After confirmed boot, run texture-cdram-candidate/launch.sh (same saved-test
profile and 360p settings); launch has NOT yet been started. Keep-awake process
134059 remains active. Need actual [texture-memory] CDRAM confirmation, screenshot,
settled frame distributions and active gameplay before drawing conclusions.

### 2026-09-26: perf238 CDRAM verified, no demonstrated FPS benefit

Upload session30080 completed verified=true, boot_confirmed=true, slot0;
payload SHA d2b6cb641f3dc46c26c2511c69bdfde77a88e993420f2c5a6d2a41b84ebe2511,
34,755,238 bytes. Launched once in session65534/PID210485 with tag
perf238-texture-cdram-20260926. Collector session92613/PID211396 completed
normally; it did not restart or change the launch. Watcher remains active.

startup.log confirms perf238, `[texture-memory] 32768 KiB CDRAM pool`,
640x360 rendering, and after-gfx free main 266240 KiB / CDRAM 56320 KiB.
This is the intended placement, not fallback. pod238.png frame6058 was viewed:
same AR60 lifepod, no obvious new material/texture/lighting regression.

Settled 1200 intervals, end frames6360..7500: mean77.075359ms /12.9743FPS,
p50 75.688ms, p95 86.764ms, p99 119.795ms, max172.705ms;
1199 >50ms, 24 >100ms, zero >200ms. Baseline perf237 77.602135ms and
perf236 77.087804ms: NO demonstrated FPS gain. Do not market this as a speedup.
Texture pool placement alone is not the dominant lifepod limit in this test;
this does not rule out bandwidth elsewhere or shader work.

Followed with 5s forward, 3s right camera turn, 2s AR fire, then pad release.
outdoor-fire238.png frame8339 shows outdoor terrain/trees, muzzle flash and
AR36; remote log download after-fire.log succeeded. This is only a short
movement/effects smoke test, not sustained NPC combat, audio verification,
checkpoint qualification or the required 15-minute active gameplay session.
Current Vita remains perf238 outdoors; lease refresher134059 still owns wake.
The source option stays off by default. Main-memory headroom improved 32MiB,
but that is not progress toward the FPS acceptance criterion by itself.
Next pursue substantial shader/pass work reduction; avoid another pool-placement
trial without new evidence. Target 20FPS remains unmet.

### 2026-09-26: empty remote draw traces exposed an asynchronous diagnostic gap

One offline RGB-intermediate-half variant of B5691565 compiled to 135 primary
instructions (unchanged from perf237) and eight temporary registers vs13.
No hardware deployment: this changes numerical precision without demonstrating
a large compiled-work reduction, and a prior global half trial was unhelpful.
Artifacts remain private under cube-select-candidate. Physical Vita unchanged.

Requested two separate one-frame remote traces during perf238 outdoor gameplay.
The first fetch preceded completion; preserved the later complete logs.
Both requests reported guest-frame start/completion (12407 and15395) but emitted
ZERO [draw-state] records. Evidence: texture-cdram-candidate/
outdoor-draw-trace-complete.log and outdoor-trace-recheck.log. Empty trace output
must not be used as evidence that the material draws were absent. This finding
alone does not invalidate the independent Present-interval distributions.

Remote tracing previously used g_dev.frame+1 while owner Present can advance
before the asynchronous scene/recorder draws. Changed the remote trace window to
actual BeginFrame/EndFrame (and legacy renderer Swap) boundaries, which follow
recorder draining. Guest Present no longer consumes or closes remote requests.
Manual guest-frame histogram selection remains separate. End reports both
recorded commands and the number of actual draw-state log records, making empty
captures explicit. No full histogram/per-vertex dump or new GPU wait is enabled.
The ordinary disabled per-draw path keeps its relaxed atomic load; acquire
fencing occurs only while the diagnostic is armed.

Updated production-function harness exercises independent guest/render counters,
1000 draws while the guest counter advances, queued follow-up requests, empty
capture, wrap, manual-trace overlap and absent network hook. Host ASan/UBSan and
ARMv7 Pi CPUs0/1 pass. New regression would fail the former guest-counter window.
Perf239 is a diagnostic repair candidate under render-trace-candidate; retains
perf238 settings, texture pool and embedded material shader. Initial Vita build
passed; rebuild running after preserving the disabled hot-path fence policy.
Current build session recorded by tools; NOT deployed yet. Next verify nonzero
hardware draw-state records before relying on this diagnostic to choose material
specializations. No FPS gain is claimed for this repair.

### 2026-09-26: perf239 trace exposed helper log suppression; perf240 buffers it

Perf239 installed/boot-confirmed, payload SHA
87aa4a582611b6d2337bb729dab2d10bac8ee26858412a88347e4cc8f8677278.
Pod239.png frame6011 visually matches the stationary pod/AR60 view.
Settled-after-trace.log windows6300..7440 (1200 intervals) mean76.608784ms,
13.0533FPS, p9587.195/p99114.374/max207.446ms; 23>100ms,1>200ms.
No established performance gain; excludes the traced frame5990.

Trace validation FAILED usefully: closed frame5990 had422 commands and268
reported draw-state calls, but ZERO actual draw-state lines. Frame selection
now follows recording, but xv_log_write intentionally suppresses both scene
helper and deferred-worker messages (prior repeated logging caused stalls).
Do not remove that normal hot-path safeguard. Private evidence under
render-trace-candidate/pod-draw-trace.log and trace-validation.json.

Perf240 buffers only trace_draw_state output into an on-demand bounded1MiB
allocation. Existing drained Begin/End boundaries hand ownership between
recording workers and the recording owner; workers only format into RAM.
After workers drain, explicitly flush once through critical_write, bypassing
helper suppression (Present can itself run on the scene helper). Allocation
failure/overflow are visible in a separate summary; validator requires zero
lost lines plus exact reported/actual draw-state record agreement. The traced
frame incurs diagnostic I/O and must be excluded from performance results.
Captures all18 PSC rows for the two heavy B5691565/154066FD materials to evaluate
exact specialization opportunities. No shader choice or rendering changes.

Host ASan/UBSan and ARMv7 Pi CPUs0/1 pass bounded-buffer tests (worker join,
no sink calls on worker, whole-line overflow rejection, failed allocation,
disabled no allocation, exact capacity). Existing trace-selector tests pass.
Perf240 full build session37930 completed0; package running63653 under
render-trace-buffer-candidate. NOT deployed yet at this note. Keep-awake134059
remains running; current device239. Next verify package contract, deploy240,
run existing launch.sh/collect-trace.py once, inspect actual capture. No FPS
optimization is claimed for this diagnostic repair. Goal remains unmet.

Perf240 package verified:55068699bytes SHA
e40c01403784d52341d2c1fc9d2f44a0d737014cb21ab050ef1d189a46a36a3b,
asset contract unchanged775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897,
exact ZIP changes game-a.self/boot-game.txt only. Stopped confirmed239 watcher
226197/driver225377; keepawake untouched. Announced restart; update session64593
is uploading payload34756402bytes. Poll same session; do not restart upload.
After boot-confirmation run render-trace-buffer-candidate/launch.sh and its
collect-trace.py, redirecting their logs there. Latest changes122be38f local;
no push. Previous goal turn progresses via reproduced trace failure, tested
fix and new hardware candidate. Target not met.

Perf240 update64593 completed0/boot-confirmed slot0, payload SHA
64cdcf0b38272dc9b7c674bea713f58eb51ef3b2e45fbfac07d9c6c46addfc5c.
Launch10195 and collector57135 started in render-trace-buffer-candidate;
logs gameplay-driver.log/collector.log. Collector waits gameplay end>=5700
then captures pod240.png and ONE trace. Validator now requires buffer marker,
zero dropped lines, successful allocation, same end frame and actual records.
Added tools/summarize_draw_trace.py to group validated state by PS/VS/pass,
texture dimensions and constants. It rejects the actual239 failed trace and
synthetic dropped/allocation/count/end-frame failures. Counts are NOT GPU time.

### 2026-09-26: perf240 valid material capture; substantial specialization lead

Collector57135 completed: frame6036 closed6036,410 commands,256 draw-state
records counted both producer and downloaded log,367572 buffered bytes,0 lost
lines, allocation succeeded. pod240.png6056 viewed matching pod/AR60.
material-summary.json generated by tools/summarize_draw_trace.py from
render-trace-buffer-candidate/pod-draw-trace.log. Trace is diagnostic, not timing.
All113 B5691565/154066FD draws had4x4 stage3 textures:111 use154066FD (91vs09,
20vs27),2 useB5691565vs09. PSC0=(1,0,0,0),PSC8=(0,1,0,0),PSC5=zero in all.
The tiny textures may be neutral/black dummies; size alone is NOT a color proof.
Other stages sometimes also4x4. Need exact uploaded-content proof before any
sampler substitution. Code already keeps recorded uploads immutable until pool
retirement and has cache-owner descriptor opacity proof that can be extended.

Compiled PRIVATE cost probes through isolated XVSC00001 (not gameplay/emulator
FPS validation). New process89885 compiled13shaders0failures then same known
exit SIGSEGV; compile.log and specialization-compiler-driver.log preserved.
Old idle compiler process198830 still exists; avoid launching another duplicate.
No actual gameplay program changes yet. specialization-costs.json:
B569 baseline135ins13temps; axis125/9; black83/11; axis+black74/6.
154 baseline130ins9temps; axis121/9; black76/7; axis+black69/7.
Axis means literal PSC0.rgb(1,0,0),PSC8.rgb(0,1,0),PSC5.rgb0; black means
tex3 sample replaced with float4(0,0,0,1); t3 alpha unused in these programs.
Cg/GXP variants axis_,black_,axisblack_ under private cube-select-candidate/
compiler-pref/ux0/data/xita/shaders. B569 baseline uses range_ retained237;
154 baseline uses original baseline_, not regressive range_ variant.

Next implement guarded material specialization: prove all uploaded stage3 RGB
samples/mips black, retain immutable version semantics, capture flag in cmd,
check exact constants/program+2D-cube variant, preserve alpha-test mode and
fallback/link/override rules. Cache fragment variants distinctly; embed extra
programs code-only. Validate classification and guard/negative cases host/Pi,
then compile/deploy candidate and verify actual specializations/hardware visuals
and frame distribution. Do not assume4x4 meansblack; do not extrapolate the
~half instruction counts to FPS. Goal20FPS remains unmet. Current device240,
launch10195/driver233366 still observing; keepawake134059 active. Collector is
finished, don't reissue request. Normal helper log drops last20reports avg4.75;
not enough evidence to prioritize logger formatting as a heavy bottleneck.

### 2026-09-26: guarded black-placeholder specialization implemented, perf241 ready

Added tiny decoded RGBA upload proof (<=4x4, all uploaded mip RGB samples zero,
alpha ignored; no BC/cube proof). Metadata captured with immutable upload version;
cache-owner descriptors only, same pin/lifetime rule as opacity. Texture size
alone never qualifies. Exact PSC0/8/5 RGB checked against command snapshot.
Candidate opt-in XV_MATERIAL_BLACK=1, disabled if device shader override enabled;
nonblack custom border conservatively rejected. Validate final resolved shader
path after 2D-cube selection, only B5691565_7F_t8 and154066FD_7F_t8. Mode4 is a
separate cache identity using _axisblack_na; only used when existing alpha path
selected mode1. Loaded program reports alpha_test_mode1 for depth preparation.
Missing/link-failed program falls back to normal mode1. Texture/order/blend/depth
and uniform upload unchanged. Embed optional variants in executable (same assets
contract). Runtime source default off. Periodic material-black counters report
candidate/proven counts; link log proves successful specialized link, no FPS claim.

Host ASan/UBSan + ARMv7 Pi CPUs0/1 pass classifier/constant tests (every mip texel,
every RGB bit, alpha/padding, wrong constants including next-float and NaN/Inf)
and actual production linker harness (distinct cache, successful reuse, failed
load fallback, unchanged alpha). Existing remote trace selector test passes.
Generator tools/specialize_ps_black.py preserves unused alpha and rejects other
stage3/constant/output dependencies; its outputs byte-match cost-tested Cg.
No proprietary shader source/GXP in commits. Private stage material-black-candidate
full build30512 completed0. Source authored files staged separately there.
Perf241 package55070711bytes SHA85a25790d8cb3dd1ec7f03a8b8365c5e6e01810031c01dd43684cea009df0741,
contract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897 unchanged;
onlygame-a.self/boot-game.txt differ from240. launch.sh uses241 tag and replaces
XV_HLE_TIMING=0 with XV_MATERIAL_BLACK=1 to fit32 remote slots. Verified device
xita.cfg has NO HLE_TIMING setting and runtime default is0, so timing stays off.
Next deploy, coldlaunch, check qualification/link counts and pod visual/frame
distribution, then active outdoor/effects. Current physical build240, no241FPS
claim yet. Keepawake134059 persists. Do not repeat trace unless qualification
counters require inspecting new state. Original20FPS completion gates unmet.

Announced241 restart, stopped confirmed240 driver233366/watcher234302. Update
session60178 active uploading34761626-byte payload. Poll same handle; after
bootconfirmation start material-black-candidate/launch.sh and collect.py with
logs gameplay-driver.log/collector.log. Collector waits qualified frame5700 for
pod241.png, then7440 for settled-pod.log, qualification.json and6300..7440 raw
frame summary. No draw trace; ordinary gameplay. Source9e4f1359 committed locally,
not pushed. Current goal turn concrete progress via implemented/tested candidate.

Perf241 update60178 finished0, bootconfirmedslot1 payload SHA
013f12eed616b4ab98c25bff11aa980a125317d3f385a61a3018239a22458274.
Launch55236 driver244501, collector80637 PID244523 running; env log confirms
XV_MATERIAL_BLACK=1. Keepawake134059 remains live. Early material counters zero
are MENU frames, not evidence of failed campaign qualification. Wait for the
already running scene/collector; do not relaunch. Related154066FD_0D_t8 (vs27)
is already58instructions3temps with color1 zero/unused reflection pruned; current
candidate correctly only covers7F (vs09), up to93draws in240capture. 3F variants
still130instructions; audit later if their scenes need it. No new FPS claim.

### 2026-09-26: perf241 confirmed pod gain; first shader specialization works

Collector80637 finished normally. settled-summary.json has1200 Present intervals
end6300..7440: mean60.013240ms (16.66299FPS),p5058.184,p9574.002,p9992.330,
max138.460ms;1138>50ms,8>100ms,0>200ms. Prior matching239 capture76.608784ms,
13.05333FPS,p9587.195,p99114.374,max207.446. About16.6ms lower/~28%higherFPS
in this single settled pod capture. Not sustained20FPS or full gameplay proof.
qualification.json:3 successful specialized links (154 variant vs3blend5/6,
B569vs3blend6). Last20 counters ~7030candidates/5730proofs per60frames,
~95.5proofs/frame. All earlier MENU zero counters irrelevant. pod241.png frame6271
viewed: matching lifepod/AR60, no obvious new rendering defect.

Short active smoke: forward5s, camera-right3s, ARfire2s, release pad. Input14478
completed0; outdoor-fire241.png9343 viewed outdoors with AR32 and normal terrain/
trees/weapon, overlay13FPS. after-fire.log2986232bytes fetched successfully.
No crash in that short sequence; this is NOT the required15-minute active run,
AI/audio qualification, save/resume verification, or proof of outdoor20FPS.
Current device241 remains outdoors. Keep awake134059 unchanged.

Residual settled last20 scene-thread reports: helper CPU49.106ms/frame,
scene wall52.459ms, owner wait3.7735ms. Inclusive owner FA920 roughly48ms;
never add parallel/inclusive times or label them isolated CPU self. Deferred
recording drains around0.39ms/frame at one observed window, no fullqueue waits.
GPU work reduction has exposed a ~50ms scene preparation floor in the pod;
open outdoor view still shows meaningful display wait (~19ms in snapshot).
Need keep qualified specialization stacked, examine remaining scene preparation
and outdoor rendering, not assume more fragment savings alone solve all views.

Private original world shader ps_DEB42ED7_3D_na (canonicalA01D09CF,vs16) already
uses analytic normalization-cube replacement:47instructions0temps, t8variant65/6.
Do not repeat analytic cube optimization. No change to this shader. Related
model0D has58instructions and unused reflection already eliminated. Next target
must respect existing optimizations and current physical evidence. Scene detailed
phase timing currentlyoff XV_SCENE_PHASES0; coarse reports above remain enabled.
Goal20FPS through pod/outdoor/NPC/cutscene and stability correctness gates still
unmet. Source9e4f1359 contains optimization; build241 opt-inXV_MATERIAL_BLACK1
must remain in subsequent launch configs (source defaultoff pending broader use).

### 2026-09-26: outdoor alpha-preserving specialization candidate perf242

Requested ONE outdoor render trace on current241 without relaunch. Captured
material-black-candidate/outdoor-trace.log4326212bytes; validated via parser,
frame11441:296draw records,475161bufferbytes,zero drops. outdoor-materials.json
shows161vs09/154066FD draws, all alpha state0x0001047F(GREATER127), blend0/5/6.
World20draws DEB/A01, others unchanged. Opacity proof means some of these already
use NA; raw alpha request alone does not prove all use generic test. Log shows
both normal generic154 and newaxisblack_na programs linked. No241gt linked.
Found main.c explicitly xv_cutout_override(0), so DO NOT enable the separate
GREATER-only feature blindly. Preserve that existing setting.

Extended guarded optimization to generic full-alpha program as cachemode5.
Original generic alpha block and uniform preserved BYTE-FOR-BYTE. All same
texture/constant/material proofs required. Mode5 loadedfs alpha_test_mode0;
load failure/cache-full falls back mode0. Mode4 successful NA path unchanged.
Routes only originalmode0/1; modes2/3 untouched. Generic optional filenames
_axisblack.frag.gxp embedded separately. Source option remains same opt-in1.
Actual linker harness extended for distinctmode5/4 and both fallback policies:
hostASan/UBSan and ARMv7Pi CPUs0/1 pass. Generator validates alpha block via
existing specialize_ps_alpha before rewriting material arithmetic only.

Stopped old idle owned compiler198830 (isolated XVSC, not Halo2). New compile
session10751 produced15shaders0failures. Generic cost probe:
154 baseline225instructions17temps ->155instructions9temps;
B569 baseline232instructions19temps ->162instructions11temps.
Data material-black-candidate/generic-costs.json. This is compiler evidence,
not an FPS result. Private GXP/Cg genericblack_ in compiler workspace.

material-black-alpha-candidate stage copied241; fullbuild58875 running. Package
script targets242 from241 unchangedassets. launch/collect scripts prepared242tag,
same settingsinclMATERIAL_BLACK1 and normal6300..7440podcollection. Need inspect
build result, package/verify exact ZIP changes, deploy and coldlaunch. Current
physical241stilloutdoors; keepawake134059 live. Do not overwrite qualified241
candidate. Full20FPS target/correctness/session gates remain unmet.

Perf242 fullbuild58875 completed0. Package55072192bytes SHA
fc1676ce9d72b45aba4bf30731a8a7c86bccc475e6c180c25094f7e9a7ed6f60,
unchangedcontract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897;
exact ZIP changes game-a.self/boot-game.txt verified. Announced install.
Session15400 is first downloading241 outdoor-before242.log, then stopping
confirmed241driver244501/watcher245337 and uploading/applying242. Poll this SAME
handle. After bootconfirmation run material-black-alpha-candidate/launch.sh and
collect.py, logs gameplay-driver.log/collector.log. Keep awake134059 untouched.
Source8059f904 local committed, no push. Full targetnotmet,241podgain retained.

Saved241 outdoor baseline before242: outdoor-summary.json selects1200intervals
end15600..16740, well after diagnostic trace11441 and movement9330. Mean73.745615ms,
13.56013FPS,p5072.153,p9585.997,p99127.441,max168.824;1185>50ms,30>100ms,
0>200ms. Same stationary outdoor view after the earlier scripted movement;
future outdoor comparison needs view qualification, not just map name. Update
15400 uploaded all34765806bytes; verification/apply still pending, do not restart.

Perf242 update15400 completed0/bootconfirmedslot0. Payload SHA
c82bbca58016818da36df63c69822bcb68aed85d9a23dad2f23f027751e0a685.
Started existing material-black-alpha-candidate/launch.sh session78856,
driver255612, and collect.py session95263/PID255631. Log files there:
gameplay-driver.log,collector.log. Keepawake134059 confirmed live. Wait on these
same processes for pod242.png, settled-summary.json and qualification.json;
then inspect generic _axisblack links/visuals and do outdoor movement/effects.
No hardware242performance result yet; original20FPS acceptance stillnotmet.

While242launch78856/collector95263 continue normally, inspected old cutout
restriction provenance: main.c force-off comes from39de2bfda; docs/slot-pipeline-
20260907.md says UNVALIDATED experiment, and docs/alpha-range-audit-20260907.md
says dedicated GREATER shader was prepared but not hardware-tested. No documented
failure found in these notes; don't assume it is broken or blindly enable it.
Prepared OFFLINE follow-up only by applying existing specialize_ps_cutout to
new genericblack154Cg. Compiled isolatedXVSC session50679:16shaders0failures,
gtblack_ps_154066FD_7F_t8.frag.gxp1228bytes,78instructions4temps versus generic
black155/9. GXP capabilities at0x14 identical and declared/aligned size valid.
Original math before alpha block/return unchanged by existing generator; GREATER
selected only for enabledfunc4 would preserve alpha/NaN rejection. Runtime242
unchanged; this variant NOT embedded/deployed/routed. Candidate for later only
after current242measurements, perhaps explicit guarded mode2 ofMATERIAL_BLACK
rather than changing the old global startup override. No FPS claim for it.
Old idle compiler198830 ignored TERM and was explicitly cleaned withKILL before
new isolated compiler launch. Do not touch ClaudeHalo2 emulator resources.
Current242watcher reporting loading(menu)frames at90s; no qualified resultyet.

### 2026-09-26: perf242 full-alpha material gain verified outdoors

Collector95263 completed0. Pod1200intervals6300..7440 mean58.7345275ms,
17.02576FPS,p5056.637,p9575.983,p9997.553,max146.721;1023>50ms,10>100ms,
0>200ms. Slight improvement over24160.01324ms but tails not better; don't
oversell this small pod delta. qualification.json has5successful specialized
links incl both generic _axisblack and _axisblack_na. pod242.png6311 viewed
same scene/AR60; weapon idle animation differs, no obvious new visual defect.

Movement24219: forward5s, rightturn3s, fire2s, release pad. Screenshot
outdoor-fire242.png8495 viewed same general hill/trees view as241, AR16,
transparent foliage edges intact. HUD17FPS. No crash in short sequence. Different
ammo count vs241 is not a firing-rate proof: actual HTTP/input timing wasn't
measured and hold durations bound loops rather than exact device exposure.
Don't claim full weapon/audio/AI correctness from a screenshot.

Outdoor collector9425 completed (collect-outdoor.py): settled-outdoor.log3650180
bytes,1200intervalsend9780..10920, warmup excluded. Mean56.524073ms,17.69158FPS,
p5054.485,p9567.405,p9997.937,max213.540;1148>50ms,12>100ms,1>200ms.
Prior241scriptedstationaryoutdoor73.745615ms/13.56013FPS,p9585.997,p99127.441,
30>100ms. About17.2ms less/~30%higherFPS in closely matching scripted outdoor
view; no claim of exact pose identity. One214msstall remains, so not stable20.
This confirms an outdoor gain from the alpha-preserving specialization; retain
both241and242optimizations with MATERIAL_BLACK1 in subsequent builds/configs.

outdoor-coarse-timing.json last20reports: helperCPU47.3885ms,scene-wall49.6565ms,
ownerjoinwait12.189ms, game51.53+displaywait5.185ms. These overlap; DO NOT SUM
helper/owner/game. Scene preparation is now near the50ms target by itself;
remaining waits/tails stillmatter. Need reduce real scene work while preserving
shader gain. Offline guarded GREATER cutout probe78instructions vs155generic
is stilluninstalled and may reduce remainingGPUcost; only use enabledfunc4 with
allmaterialproofs, preservefallback and defaultglobalcutoutdisabledpolicy.
Currentphysical242outdoors/remotealiveframe12316, awake3570; keepawake134059
live. Launch78856 watcher stillobserving; podcollector95263andoutdoor9425done.
No15-minuteACTIVE session,NPCcombat/audio/save-resume qualification yet. Full
20FPSpod/outdoor/cutscene completiongates remain unmet. No push.


### Perf242 post-shader CPU profile — running

Confirmed physical perf242 responding and keep-awake PID134059 live. Previous
ordinary outdoor observer deliberately stopped (255612/256539). Started one
cold diagnostic launch with all retained settings unchanged except
XV_SCENE_PHASES=1; private material-cpu-profile-candidate/launch.sh,
launch session23740/PID263719, collector session54253. Environment receipt
confirms all32keys. Frame4097 screenshot launch-check.png shows the loading
screen, not a qualified gameplay result. Collector waits for5700 screenshot and
6300 logfile. Timed guest callee scopes cover model prep, tick, effects and
native70110's caller (native body's expensive internal taps remain compiledoff).
Do not compare instrumented FPS against ordinary performance baselines.
Private analyze.py reads the actual tick/scene phase format, separately by
thread; output self times already subtract instrumented children, elapsed
includes timer overhead/preemption/waits. Top30 edges are incomplete listings;
never sum inclusive entries or scene+tick. Next qualify pod, collect settled
phase windows, identify real remaining critical-path work, restore phases0
for performance/gameplay qualification. Keep awake and do not restart an active
launch just because a collector observation window expires.


### Perf242 CPU profile collected: scene and tick costs depend on view

Diagnostic pod screenshot frame6221 verified the loaded lifepod; both specialized
shader families linked (5 links, last5730 proofs/60frames). Collected
material-cpu-profile-candidate/settled-pod.log and pod-phases.json, last6windows
near6180..6480. Scene5DBC0 inclusive53.305ms, model5B76019.835ms;
5B760->5B4A0 15.358ms/177calls per frame. Native70110 inclusive8.092ms;
reported self5.732ms **includes its untimed native callees**, internal expensive
taps compiledoff. 54010 inclusive6.642/self6.03ms also includes **four untimed
indirect callback sites**, not proven dispatcher overhead. 53540 self2.967ms.
Tick FA92061.11ms includes1.836simulationticks/frame; object4C98015.47ms,
4B9D014.99ms, transforms8DDF012.512ms incl/5.728ms reportedself.
Native4B9D0 and92330 alreadyenabled; do not propose reimplementing existing
query/feature natives as if missing. Generated4B9D0/49600 alreadyx87-regs.

Scripted forward5s/right3s/ARfire2s/release completed; outdoor-profile.png
frame7829 verified foliage/world/weapon view, no visible new regression. These
hold durations are host bounds, not measured device input durations. Outdoor
settled-outdoor.log3186114bytes, last10windowsnear7920..8460:
scene5DBC052.048ms,5B76020.402ms,5B4A0total16.768ms;
70110incl9.607/remainder6.551,54010incl6.039/remainder5.475;
tickFA92041.516ms (1.725ticks/frame),8DDF011.101incl/5.136remainder.
Pod's large collisioncost therefore does NOT remain the principal outdoorwall.
Scene is the outdoorcriticalpath; tick matters strongly atpod. These diagnostic
elapsed values contain timer cost, waits/preemption, nested scopes, and repeated
ticks; not additive and not normalFPS qualification. Do not call the earlier
cutscene-phases.json filename verifiedcutscene evidence: it selectsnear5640..5880
without a contemporaneous screenshot; exactscenephase uncertain.

Next narrow evidence target: the four indirect callbacks inside54010, currently
lumped into its apparent self time; identify actualcallback targets before
attempting a dispatcher rewrite. Native70110's untimedcallees also require
careful attribution; don't repeat full per-call internal timers that previously
cost severalms themselves. Preserve shadergains, querynatives andx87locals.
Stopped diagnosticobserver264767/263719 afterlogcapture. Announced ordinary
restore, material-cpu-profile-candidate/restore.sh nowcoldlaunches sameperf242
withXV_SCENE_PHASES0 andallotherretainedsettingsunchanged; newtag
perf242-normal-restored-20260926. Keepawake134059leftalive. Goalnotmet.


### Perf243 callback attribution prepared

Added tools/patch_scene_callback_timers.py: narrowly instruments the four
indirect calls in the owned CE54010 generated body, retaining the original
capturedtarget -> guestreturnaddresspush -> xv_call sequence. Existing
XV_SCENE_PHASES gate controls collection; each callback uses its actual captured
targetaddress, including after xv_call mutatesguestregisters. Requires exactly
the four auditedreturns540E3/541C4/54221/54257 and failsclosedonlayoutdrift;
idempotence verified. Removing inserted declarations/timercalls from the patched
privatebody reproduces the originalstatementtokens inexactorder. No generated
code addedtorepo. Stage scene-callback-candidate clones242withidenticalflags,
only code_009callbacksites+version243changed. Buildsession38475completed0.
Normal242restoredlaunch remainslive; material-normal-cold-candidate collector
session5378 verifiesanotherordinarycoldlaunchbeforethe nextdiagnosticupdate.
Keepawake134059live; noemulatorvalidation orHalo2changes.


Perf243 package verified:55072559bytes,
SHA841e2969d920bb8d0088ee98e5dffb2f32bb19b1a82140c07b9505fd480e5b62;
onlygame-a.self+boot-game.txtchanged, contract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.
Payload34765998bytes SHAbd28fae6d7c7013c22dbea8a1251682dcf2cb06f2234fe1c1d00efb72205e745.
Normal242secondcoldlaunch material-normal-cold-candidate settled-summary.json:
1200 intervals6300..7440: mean58.1474ms (17.19767FPS), p95=73.777ms,
p99=97.144ms, max152.139ms, 11>100ms, 0>200ms. Screenshotpod242.png
frame6423verifiedsamepod. No20FPSclaim; repeats17FPSgainaftercoldlaunch.
Stoppedconfirmednormalobserver268803/269778 aftercapture; announced243update.
Deploymentexec76442live, dashboardresponded242beforetransfer, upload26.3/34.8MB
lastpoll. Do notrestarttransfer. Candidatecollector/analyzerready,launchafter
verifiedboot; normal242rollbackretained. Keepawake134059continues.


Perf243deploy76442completed0: verifiedtrue,bootconfirmedtrue,slot1,
payloadSHAexactlybd28fae6d7c7013c22dbea8a1251682dcf2cb06f2234fe1c1d00efb72205e745.
Startedscene-callback-candidate/launch.sh andcollect.py, logsdriver.log and
collector.log. Awaitconfirmedenvreceipt,qualifiedpod243.png andsettledlog;
thenanalyzeactual54010childrenwithanalyze.py. ThisisdiagnosticnotFPSacceptance.


### Perf243 callback attribution completed

Qualified pod243.png frame6128 and outdoor243.png frame9554, both viewed;
movement/fire sequence completed without a crash or obvious new visual defect.
Private scene-callback-candidate/settled-pod.log3133017bytes, last10phasegroups
near7800..8340:54010 inclusive7.113ms, self4.076ms (15children).
settled-outdoor.log4146699bytes, last10groups10080..10620:
54010inclusive6.471ms,self3.588ms. Leadingcallbacks:62870~1.24ms,
628F0~0.93ms,62270~0.35ms; remaining callbacks individuallysmall.
Timingsdiagnostic,notnormalFPS; timer overhead nowmoves some cost into dispatcher
remainder. Original~5.5–6ms apparent self included these previouslyuntimedcalls.
Do NOT assume a6ms puredispatcherrewrite saving. Preserveordering/callbacks.

Private callback-reference/manifest.json pins owned bodies54010,62870,628F0,
62270; generatedcode remainsoutsidegit. 628F0is a33lineforwarder to77FE0,
not0.9ms ofwrapperoverhead. 62870and77FE0 contain39/65x87ops respectively;
existing x87-regs-d3-final explicitlyrejects both forlowdensity (manycall-syncs),
notunsupportedinstructions. R=0regeneratedcopiesexistbut priorARMwork showed
call-heavyregsloweringregressions; do notblindlyoverride density3.
54010has0x87ops,4indirectcalls,501originalClines (533withnewtimers).
Itsown3.6–4.1ms suggests nativeordered-list traversal/setup, retainingcallback
order and every guest-visible state/memory effect. Needs in-process reference
comparison and ARMcost proof; no replacementimplementedyet. Existing TPIDRURW
pagetablelookupalreadyhoisted(oneMRC/function); do notclaimitper-memorycost.
Existing53E90maskscanprototype remainsuninstalled due commonmaskregressions.

Stopped confirmeddiagnosticwatcher276403. Announcednormalrestore; launched
scene-callback-candidate/restore.sh tagperf243-normal-restored-20260926,
phases0, allretainedshader/native/deferredsettingsunchanged. Awaitenvreceipt;
keepawake134059retained. Goalstillincomplete; noadditionalFPSgainclaimed243.


### Scene index prefix implemented and ARM verified, not installed

New authored prototype xk_scene_index_run.h and pinned retained-loop comparison
tools/test_scene_index_run.py + tools/tests/scene_index_run.c. Batches only
54132's signed consecutive-index prefix, stays within one mappedpage, preserves
fullregister/flag/budgetstate, does no guestwrites, stops beforeloop-exit/yield;
minimum4 entries. Hardwareadmission/productionhook NOT implemented. Needactive
helpercontext+ownedordinaryinputs, activeX_PT mapping, no diagnosticobservations
bypassed; preserveoriginalfallback. Full details docs/native-scene-index-run.md.

Host ASan/UBSan + PiThumbA9compile pass2400cases/5984batches/8678yieldstates,
9declines. Private scene-index-run-host-v2 reference.c remainsoutsidegit.
Firstv2costrun overlapped profileharness; discard its timing, correctnessstill
valid. Isolatedpi-isolated-result.txt: original/candidate ns/call length1 49/59.5,
2 55.1/82.4,3 62.4/106.3,8 99/97.9,32 284.8/147.4,128 983.6/312.3,
512 3788.7/963.2. NohardwareFPSprojection.

Real a30 original-path Pi histogram completedplanned120s(rc124),38reports.
scene-index-profile/build.py modifiesonlycode009overpoint-locationhostobjects;
run scriptpoint-location-build/profile-current.sh, tagcodex-index-runs-20260926,
cores0/1, nativesfast+deferred2. Settledframe>=1800:121completegroups,
123904runs,21221267steps,171.27steps/run,87.47%>8,37.47%>128.
Parserinitiallyhitinterleavedpre-settledlines; correctedparserrequires12numeric
bins+sum=runs andrejectsbadgroups. Settledrejected0. summary.jsonpreserved.

Private scene-index-verify/build.py predicts a copiedxctx at first54132entry
perrun, lets ORIGINAL code advance nbackedges, comparesallcontextbytes there,
aborts onmismatch/prematureloop-exit. Originalpathremainsactive; nofakecallback
or schedulerreplacement. Planned120srun99717completed0(wrapper),remote124,
37reports, lastverificationframe2255/356352prefixes/0mismatches.
Receipts scene-index-verify/summary.json; full logs d3d-record2-work/pi-runs/
codex-index-{runs,verify}-20260926.log. Both Pijobsfinished; noH2interference.

Vita remainsnormalperf243 (phases0),keepawake134059active; scene-indexprototype
notdeployed. Nextintegratehelper-only opt-in andhardwareverify beforeordinary
fastqualification. Existing shaderimprovementsretained. Goalnotmet.


### 2026-09-26: perf244 scene-index guarded integration

Implemented an opt-in private-stack hook for the qualified 54132 prefix. The
stage patch tool checks the exact retained-loop hash; removing the hook restores
the shard byte-for-byte. Mode `XV_SCENE_INDEX_RUN=1` predicts and compares against
original execution; mode 2 applies, mode 0/default retains original execution.
Only the active helper's private context and 256 KiB stack are admitted; checked
address builds decline. Global index lists remain original-path pending ownership
qualification. Uses cached active arena/page table and retains real loop exits,
page transitions, scheduler yields, callbacks and order.

Host ASan/UBSan and isolated Pi Thumb A9 each passed both modes through 2,400
full-context/memory/mapping cases and 8,678 yield states per mode. Additional
admission tests passed for foreign callers, private-stack boundaries, wraparound,
and checked-build exclusion. No hardware speedup claimed yet.

Private stage `scene-index-candidate/build-x87` compiled successfully as perf244.
VPK `xita-perf244c.vpk` is 55,074,588 bytes, SHA256
`1c4b104532cb2031e4dfca92d4dddf47d8b03665827fc53eb46e6ad64d7ad72b`.
Only game-a.self and boot-game.txt differ from perf243. Payload 34,767,482 bytes,
SHA256 `c763777c29891284f571a0322b374efc8fa86e6537b913436e152de368678376`;
contract unchanged `775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897`.

Deployment and hardware qualification were started this turn; consult
`scene-index-candidate/deploy.log` and subsequent notes for completion.
`verify.sh` preserves previous options, replacing explicit SCENE_PHASES=0 with
SCENE_INDEX_RUN=1 to stay under 32-key remote limit. Actual xita.cfg has no
SCENE_PHASES key, so its default stays off. Keepawake PID134059 remains active.


Perf244 upload completed: payload SHA verified, slot 0, boot_confirmed=true.
The ordinary scripted a30 launch is live with INDEX_RUN=1 and the protected test
save. Driver session 2474, collector session 73152; tag
`perf244-index-verify-20260926`. Collector writes hardware-verify.json at frame
6540 and a pod screenshot after frame5700. Do not treat its diagnostic timings
as acceptance. Current source commit e0074486; no push performed.


Perf244 qualification correction: reached the expected lifepod view (screenshot
pod244-verify.png frame6221) without a visible regression, but verified-prefix
messages were written to stderr, which is not captured by Vita xita.log. The
collector returned checked=0, NOT a pass. Ordinary xk_os_log would also be dropped
on the scene helper. Perf245 therefore uses xv_log_criticalf for verification
messages and a one-time fast-mode admission message, leaving the prefix logic
unchanged. Host admission tests pass with a shim of the Vita critical log API.
The source correction is awaiting the final stage rebuild; no hardware fast-mode
performance result has been claimed.


Perf245 final stage build succeeded with the critical log sink. VPK
scene-index-candidate/xita-perf245c.vpk: 55,073,978 bytes, SHA256
`e0d0e4c2fbda6d802fa1e96603dc3b2fb8d7a7ef219aa6b3053d3d1d21775f14`.
Payload 34,767,298 bytes, SHA256
`c6891934dd8009fbd9c06b2946bbf338d27f9662da003adda6103a3a89fb9ac1`.
Contract unchanged; only game-a.self/boot-game.txt changed versus243.
Previous perf244 captures/receipts preserved with perf244- prefixes. Old244
watcher explicitly stopped before deployment; keepawake remains running.
Update session76102 is live; receipt deploy245.log. After it completes, launch
verify245.sh and collect-verify245.py (tag perf245-index-verify-20260926).


Perf245 deployment completed verified=true, boot_confirmed=true, slot1. The
verification launch is now running: driver session3276, collector73192. Read
verify245-driver.log, collect-verify245.log, hardware-verify245.json and
verified245-pod.log when available. Re-poll these live handles; do not relaunch
because a collection observation expires. Keepawake PID134059 still active.


### 2026-09-26: perf245 admission result and perf246 adjustment

Perf245 reached the expected pod checkpoint and logs INDEX_RUN mode1 through
critical logging, but the collector at frame6600 reports checked=0/no failures.
This is NOT a pass: no 65,536-prefix report arrived, so sparse/declined admission
cannot be distinguished from that coarse threshold. The main 54010 workload
uses the scene-global list at38BE14; stack-only admission misses that path.

Audited retained callers/producer (details native-scene-index-run.md); expanded
admission only to this bounded scene list on the active helper, preserving the
private-stack bound word and <=0x4000-entry capacity. Host2400-case verification
and fast fixtures still pass; host sanitizer/Pi admission boundary tests pass.
Reporting now includes first verified prefix and every4096, fast still once.

Perf246 is building in scene-index-candidate/build-x87, session30184. Source
updates not deployed yet; installed perf245 remains in diagnostic mode1.
Keepawake PID134059 active. Current245 watcher driver3276 / collector73192;
collector finished, watcher may still be live. Check handles before stopping.
Next: package246, deploy (same only-two-entry payload contract), run verify246.sh
and collect-verify246.py. Require actual completed comparisons and inspect pod
screenshot; then normal mode2 cold launch, keeping previous qualified options.
Do not count 244 stderr silence or245 coarse-threshold silence as validation.


Perf246 build/package succeeded. VPK55,075,082 bytes SHA256
`1222c576f9ea602f4b44a8200f4cfb581cacc688e2d89edf135ca5fb4addc840`.
Update session79204 currently transferring; final payload receipt deploy246.log.
Additional `--hook --global-list` fixtures pass on host sanitizers and Pi for
both modes: 2400 cases,8678 yields,5987 fast batches. Source tests added without
changing the currently deploying payload. See scene-index-global-differential.


### Perf246 hardware verification passed; normal fast run live

Installed payload SHA256
`940c8a604b399e4944e6d699901617b8b8bac4b293048e07a1f9780b4de04630`,
34,767,218 bytes; updater verified=true,boot_confirmed=true,slot0.
Mode1 reached the qualified a30 pod screenshot atframe6280. Collector frame6600
saved 372,736 compared prefixes, zero mismatches, verified246-pod.log and
hardware-verify246.json. This is real hardware evidence; 244/245 incomplete
admission/logging results were never counted as passes.

Stopped only the246 verification watcher321069/driver320221, then started normal
mode2 cold launch with all earlier qualified settings retained. Active driver
session39262, collector44843; tag perf246-index-fast-20260926. Source scripts
fast246.sh and collect-fast246.py; outputs fast246-driver.log,
collect-fast246.log, fast246-pod.log, fast246-summary.json,
fast246-qualification.json, pod246-fast.png. The collector requires an applied
batch activation receipt, captures a pod screenshot, then summarizes6300..7440.
Inspect that screenshot before accepting timings. Keepawake134059 remains live.
Do not restart this run merely on observation timeout; poll its handles.

Next: inspect normal pod timing against qualified perf242 pod58.15–58.73ms and
outdoor56.52ms, then move outside and test effects. No FPS gain claimed yet.
Pi full-hook cost receipt scene-index-global-differential/pi-cost.txt shows
short-run overhead and long-run gains; these are not hardware FPS predictions.


### Perf246 normal pod result and next shader candidate

Normal mode2 pod screenshot frame6498 matches the previous pod view. 1200
intervals6300..7440: mean56.4401ms (17.7179FPS),p50 54.183,p95 73.000,
p99 89.404,max133.626;882>50ms,6>100ms,0>200ms. Activation receipt records first
121-entry applied batch. Earlier qualified242 pod58.15–58.73ms: a modest observed
improvement, not sustained20FPS. Keep this candidate stacked while testing outside.

Next GPU candidate implemented locally, not deployed: XV_MATERIAL_BLACK=2 enables
captured GREATER only on the existing proven154066FD_7F_t8 black-material path.
Mode6 distinct cache identity, generic-black then ordinary fallback; fragment
alpha policy2. Source and tests in docs/native-material-greater.md. Host/Pi
link/cache tests pass; exhaustive alpha-policy/reference selection host test passes.
Generated source exactly matches prior private compiled gtblack shader, GXP1228B,
capability word preserved. Stage material-greater-candidate/build-x87 compiling
perf247, session58067. Do not deploy until current246 outdoor observation finishes.


Perf246 outdoor observation: qualified outdoor246.png frame9712; 1200 intervals
11040..12180 average56.0225ms/17.8500FPS,p50 54.247,p95 68.574,p99 99.928,
max139.767;12>100ms,0>200ms. Previous242 outdoor56.5241ms: small variation-sized
change; do not claim a clear outdoor win. Pod gain remains modest. A short input
check (fire3s,look right3s,left3s,forward2s; host durations, not exact device
exposure) completed frames13633..13901; screenshot13877 shows ammo60->17 and
intact world/weapon. Log captured around this check reports zero scene abandons.
This is NOT the required15-minute active combat gate or full weapon/AI validation.

Perf247 candidate ready, currently deploying session50814. It stacks CPU index
mode2 and uses MATERIAL_BLACK=2 to enable the captured GREATER material variant.
VPK material-greater-candidate/xita-perf247c.vpk55,075,827 bytes, SHA256
`46ac8aabdf82db651861d3751d5dba9ce7a03294e8023d2e5d7e55544f91e1da`.
Payload34,771,490 bytes, SHA256
`1d13ed82124692eeed3ea1784a0ce29dae5dc18305f83d815c8c92a7af28f5b5`.
Same contract; only game-a.self/boot-game.txt differ from246. Embedded1228-byte
shader verified byte-for-byte against the qualified compiled candidate. No new
Vita3K gameplay test. Normal246 watcher324540 stopped before deployment; keepawake
134059 remains live. After boot confirmation, start launch247.sh and collect247.py
in material-greater-candidate. Require actual _axisblack_gt linked receipt in
pod/outdoor logs before attributing any gain. Pod may use NA programs; outdoors
is the important generic-alpha material workload. No GPU gain claimed yet.


Perf247 deployment complete: verified=true,boot_confirmed=true,slot1, expected
payload SHA. Normal campaign launch active session81350; collector74852.
Tag perf247-material-greater-20260926; scripts launch247.sh/collect247.py in
material-greater-candidate, outputs launch247-driver.log/collect247.log,
fast247-summary.json,fast247-pod.log,fast247-qualification.json,pod247-fast.png.
No shader FPS result yet. Inspect the qualified scene and look for actual
_axisblack_gt program linking (pod may mostly use NA); then collect outside.
Keepawake134059 still running. Current source HEAD before this note7704f54a.


### Perf247 outdoor improvement and cutscene evidence

Qualified pod247-fast.png frame6315: mean57.1039ms/17.5119FPS,p95 73.684,
p99 92.074,max116.858;11>100ms,0>200ms. No demonstrated pod gain vs246.
Qualified outdoor247.png frame9237 matches the route/view; transient blue/blocky
effects visible (existing effects issues remain tracked, do not claim every
pixel correct). Subsequent1200 intervals11280..12420 average50.0727ms/19.9710FPS,
p50 47.816,p95 62.465,p99 91.550,max290.317;305>50ms,8>100ms,1>200ms.
Previous246 outdoor56.0225ms/17.85FPS. Newaxisblack_gt program explicitly linked
against halo_vs_09, vs3 blend6; both shader and index changes remain stacked.
Promising improvement, still not sustained20FPS.

opening247.png frame5277 visibly shows the letterboxed pod cutscene. Adjacent
180 intervals ending5160/5220/5280 average70.7665ms/14.131FPS,p95 90.348,
p99 237.265,max271.921. This identifies one opening slice, not the whole canyon
flyover. In nearby coarse reports helper wall50.7–52.1ms, done->noticed8.8–20.5ms,
FA920 inclusive owner elapsed52.8–65.4ms/frame. Interleaved reports and nested
waits prevent treating these as CPU self or adding across threads, but update
path completion is a concrete next target beyond shader optimization.

Added XV_SCENE_PHASES=2 (owner/tick only;1 remains both,0 off), with relaxed atomic
initialization/read to avoid concurrent first-use races. Production scope fixture
passes host sanitizers and Pi for off/both/owner-only, interleaved thread identity,
nesting, and clock exclusion. Perf248 stage tick-phase-candidate/build-x87 is
compiling (session90727), diagnostic only. launch248.sh retains both optimizations,
replaces explicitFRAG_CENSUS=0 withSCENE_PHASES=2 to fit32 keys; actual xita.cfg has
noFRAG_CENSUS entry and its runtime default is0. No deployment yet.

Perf247 active input exercise live session12948, four forward/jump/fire/turn/back/
reload cycles. Collector35421 finished outdoor observation. Keepawake134059 lives.
Finish the input check, archive screenshot/log, then deploy248 for attribution,
not performance acceptance. Stop only confirmed247 watcher before restart.
