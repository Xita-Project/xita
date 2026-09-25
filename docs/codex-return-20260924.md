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
