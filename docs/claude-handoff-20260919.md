# Claude Code handoff — 2026-09-19

User paused Codex because usage is nearly exhausted. Resume only under the user's direction. No build, deployment, launch or observer job remains running from this handoff. Remote controls were released by the completed observer. Do not interpret this pause as completion of the performance goal.

## Workspace and priorities

Use `/home/birchwoodgod/xita-backups/2026-09-18-unified-games/source`, branch `work/2026-09-18-packet-followup`, origin `https://github.com/Xita-Project/xita.git`. The old `/home/birchwoodgod/github/xboxvita` checkout is dirty and is NOT authoritative. Keep the repository private.

Full goal: Halo CE at 20 FPS in heavy gameplay, fix rendering bugs, and get Halo 2 rendering correctly. Halo 2 is currently parked to prioritize CE performance. Do not claim completion from a stationary room or menu result.

User preferences: retain cumulative optimizations; no automated off/on/off FPS comparisons; fully restart after updates; use ordinary gameplay/logs. Local correctness/differential/instruction tests are allowed. No Vita3K performance validation. Use the known button sequence, not a screenshot after every button. Remote controls, builds and updates have been authorized, but this handoff itself does not request another immediate update.

## Installed hardware and actual result

Vita endpoint: 192.168.0.205:8080. Installed **0.2.0-perf.48 / 6796094**, verified/restarted/boot-confirmed in slot 0. Runtime SHA-256 `602ac16bcb5a9189e76eaaa9f90d0fb299e2b7f5ad0b236846a6fc2d6e94b5b3`, 32,216,482 bytes. Private evidence is in `../hierarchy-assist-hardware/`: deploy.log, installed-status.json, package-check.json, gameplay.log, gameplay-status.json, loaded-check.png. Observer completed successfully; loaded/active both 1. Screenshot was captured but not visually inspected in this final handoff.

Final ordinary campaign windows: 12.5, 12.6, 12.9 FPS. Last object batch: 1,705,071 us / 60 = 28.42 ms/frame; draw-HLE 11.1 ms/frame. Last hierarchy assistance windows: 829 offers / 29 helped, 810 / 19, 835 / 19. Thus the new path DOES execute on hardware, but only roughly 2–3.5% of offered ranges were helped in these windows. No demonstrated FPS gain. Short observation only, not long-term stability qualification. Do not treat CPU lane work sums as additive wall time.

## Changes and reasoning

Read `docs/query-tail-followup-20260919.md` for detailed evidence and boundaries.

Perf47 showed every hierarchy overlap attempt rejected because output matrices are shared object memory. Inspection traced collision readers using those matrices both during a query and afterward. Simply dropping the privacy gate or unlocking around a query would not preserve the transaction.

New `XV_HIERARCHY_ASSIST=1` (default off; enabled in perf48) retains the shared math lock. The owner captures inputs, offers half the independent local-node transforms, computes the other half, then joins. A worker failing to acquire the math lock can claim the pure task. If nobody claims it, the publisher finishes locally. Ordered parent composition and guest publication remain with the lock owner. The helper adopts the publisher's FP mode and restores its own FP state; status flags are merged before composition. Numerical decline still restores original publisher status.

Files: `recomp/kernel/xk_hierarchy.c`, `xk_object_jobs.c`, `xk_hierarchy_runtime.h`, new `xk_captured_task.h`, and Makefile flag/config stamp. Runtime implementation commit 6796094. Earlier c00c989 split independent local transforms from dependent composition. 5bee7d3 added ARM handoff checks. 152f4a4 recorded verified deployment. This handoff commit records gameplay evidence.

## Validation already completed

- Shared-output whole-hierarchy production-worker tests: 40 configurations x 600 callbacks, ASan/UBSan AND TSan passed. Different rounding modes, preexisting FP status, success and numeric failure, copied-context fallback, no/one/two workers, disabled fast path.
- Host sanitized run: 9,600 offers, 237 helper completions. Not a hardware throughput prediction.
- Standalone captured-task test: ASan/UBSan and TSan passed no-helper fallback, claimed task cannot retire early, exact-once execution and 20,000 raced reuses.
- ARM instruction suite with `--assist`: 2,308 fixtures, 1,284 candidate handoffs. Distinct simulated helper FP context; not a thread scheduler or FPS test.
- Full Vita package build succeeded. Only game-a.self and boot-game.txt changed; package names preserved. Both assistance functions and callback are linked.
- Native hierarchy static stack: 9,128 bytes plus called frames; workers have 512 KiB native stacks. Not whole-thread peak proof.
- Existing GCC warning remains for inlined park_worker/service_state index bounds. Do not describe compilation as warning-free.

Logs: `../hierarchy-assist-shared-fp-workers.log`, `../hierarchy-assist-shared-tsan.log`, `../hierarchy-assist-arm/result.json`. Tests: `tools/test_object_private_math.py` with OBJECT_HIERARCHY_TEST_BUILD=1 and OBJECT_HIERARCHY_SHARED_TEST=1; standalone `tools/tests/captured_task.c`; `tools/test_arm_model_hierarchy.py --assist`.

## Next useful work

Investigate why helper uptake is low before assuming more core utilization will improve FPS. Offers are short and waiters may be inside a 50-us bounded mutex wait when a task appears. Determine whether notification/polling or coarser captured batches can expose useful overlap without increasing overhead or delaying owner-service parking. Do not remove bounded-wait/service safeguards blindly. Assistance does not parallelize parent composition or the entire object-update transaction.

Keep the current cumulative hardware build unless evidence warrants a change. Remaining hotspots include ~28 ms object batches and ~11–12 ms draw translation; GPU flare-query waits vary substantially by view. Do not add overlapping timings together or confuse a waiting caller with the lock holder. Shared-state snapshot safety remains unresolved for broader async simulation. Halo 2 work is parked, not finished.

## Private tools and artifacts

Remote config: `/home/birchwoodgod/xita-backups/vita-remote/3357-9AA2/remote-client.json`. Contains credentials: do not print, copy into Git, or expose contents.

SDK: `/home/birchwoodgod/vitasdk`.
ARM-test Python: `/home/birchwoodgod/xita-backups/2026-09-12-222123-phase-followup/arm-test-venv/bin/python`.
Recompiler Python: `/home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/venv/bin/python`.
Owned XBE/manifest: `../constant-pack-hardware/build/haloce/default.xbe` and `../constant-pack-hardware/build/local/halo_ce_3925/game_manifest.json`. Never commit game content/generated instruction bodies.

Build stage: `../hierarchy-assist-hardware/build.py`, build-command.json and retained build/. Stage version.json is perf48; general source version is intentionally different. Clone a stage for the next build; do not overwrite evidence.

Remote CLI: `python3 tools/vita_remote.py --config PRIVATE_CONFIG status`; update uses `update PATH_TO_VPK --apply`. Poll the same running handle through upload/verification/reboot; never restart a transfer just because an observation times out.

Launch helper: `../vita-campaign-sequence.py --expected-version VERSION --out NEW_DIRECTORY`. Observer template: `../hierarchy-assist-hardware/wait-gameplay.py`, update expected version and use a fresh evidence directory. Require a fresh loaded/active state after at least 120 seconds. Log output files are exclusively created: do not reuse destinations. No benchmark commands needed.
