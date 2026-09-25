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
