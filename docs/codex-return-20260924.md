# Codex continuation — September 24

Read `codex-handoff-20260924.md` for Claude’s preceding work. Physical status confirms perf208 / 427df56+ still installed. Goal remains 20 FPS in a30; not achieved.

## Material native integration

Merged work/native-70110-20260924, preserving the effects Makefile/runtime integration and both report hooks. A separate retained candidate lives in `../material-native-candidate/build-x87`, copied from overlap-candidate/build-x87 (perf209). Installed the native using tools/install_native_70110.py; 72 tapped sites. Both build-command.json and make-vars.txt enable XV_NATIVE_70110=1; runtime default remains Off. Candidate version perf210. Build started; do not assume completion or installation.

Fresh Pi validation compiled from merged native source and the retained stage’s translated body, Cortex-A9 Thumb, guest -Os, native -O2, thread page table and render view. On Pi cores 0/1: 20,000 differential cases, 12,000 concurrent fast-mode runs, 4,000 concurrent verify-mode runs; zero mismatches. Includes 1,360 page-crossing-window cases. These are deterministic callee/HLE stand-ins, not full-game or Vita validation. Private logs: differential.log, threads-fast.log, threads-verify.log in the candidate directory. Pi artifacts isolated at ~/xita-codex-ce-20260924.

Before hardware mutations, read-only FTP capture saved 15 udata/tdata files (7,376,346 bytes), plus configuration copies, under candidate/save-backup-20260924; SHA256 manifest included. No saves were replaced and no new campaign launch has been issued.
