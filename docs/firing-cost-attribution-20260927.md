# Firing cost follow-up

The perf276 local-context trial did not establish a whole-frame improvement and
was rolled back to independently confirmed perf275. This follow-up targets the
firing penalty rather than repeating that trial. Preserve the existing stack.

In the perf276 ordinary-gameplay log, reports near the firing window show scene
helper CPU rising from about 42 to 57 ms/frame, with corresponding scene wall
time about 46 to 60 ms. FA920 owner elapsed reports rise from about 49 to 66 ms.
These are concurrent/nested measurements and must not be added. Async report
proximity is coarse association, not exact per-frame causal attribution.
Recording drain waits grow from about 3.3 to 5.1 ms, with zero queue-full waits.
The observed firing penalty is therefore not explained by those waits alone.
The frame-slow records place nearly all the long interval before Present; this
still includes other CPU work/waits and does not independently rule out GPU
backpressure earlier in a frame.

A single diagnostic launch on perf275 is staged under ../firing-phase-hardware/:
XV_SCENE_PHASES=1 and XV_REC_WORKER_TIMING=1, omitting the frame-queue timing
override to stay within the 32-entry launch limit. Existing protected save,
360p, native hooks and worker settings remain. The phase timers already cover
owner and scene callees in this retained build. Their overhead makes this an
attribution run, not an FPS comparison against ordinary perf275/276 captures.
No new package or runtime policy was installed.

The batched launch is running. Once it completes, wait for loaded/active/director
telemetry, collect the pod interval and the normal firing/movement sequence,
review screenshots, and inspect nested parent/child timing deltas. Check scope
omission/overflow before selecting a native boundary. Missing rows from the
bounded top-cost report are not zero cost. Restore normal diagnostic settings
on the next normal launch. No new optimization is claimed yet.

## Completed diagnostic and next boundary

The pod and firing/movement collectors completed on perf275. Screenshots show
the intended pod, reduced ammunition after firing, and canyon after movement.
No scope-omission/overflow/fatal matches were found in the bounded log scan.
Instrumentation raised the pod mean to 73.626 ms and firing to 94.415 ms;
these are observer-affected numbers, not a regression in the normal build.

Representative report near frame 6300 (trigger bracket 6249–6306), compared
with the settled pod report:

| Nested scope | Pod ms/frame | Trigger ms/frame |
|---|---:|---:|
| FA920 owner total | 71.90 | 85.58 |
| 4B9D0 under biped update | 21.10 | 20.92 |
| 8DDF0 transforms | 13.41 | 13.52 |
| C0EA0 impact branch | not in selected top rows | 4.81 |
| Scene 5DBC0 | 50.32 | 58.35 |
| Model pass 5B760 | 19.34 | 21.65 |
| 59D80 pass | 8.99 | 10.21 |
| 59550 beneath 59D80 | 6.63 | 7.45 |
| Effects 5E270 | 3.11 | 5.43 |
| Recording worker elapsed busy | about 25.5 | 31.18 |

Nested/concurrent rows are not additive. Async logging, report boundaries,
selective output and timer overhead limit precise causal attribution. Batch
frame IDs exist internally, but plain phase lines do not expose each ID; use
nearby frame receipts as bounded association, not exact event matching. The
impact branch was already traced on Pi through C02F0 and effects/sound/collision
(impact-phase-diagnostic.md); no new wrapper-rewrite claim is justified.

59550 remains a substantial unpartitioned rendering path in these captures.
A private probe under ../render-59550-phase-pi/ adds 28 direct-call observer
pairs, including its two tail calls. Its original body matches the retained
perf275 target exactly. Removing observers and joining the two known split
call/return lines restores the complete original shard. The initial audit
rejected those line splits; the corrected audit explicitly checks both.
The ARM harness built, then a single 120-second Pi cores-0/1 run was started.
No competing harness was observed before launch. The surrounding headless
runtime is older, so results cannot establish current Vita FPS or rendering.
run-command.json and run-result.json retain the actual command and completion.
Do not restart while its process is live.

The Vita diagnostic session was ended via companion and dashboard relaunch
requested. Confirm status/lease in restored-dashboard.json before assuming the
restart finished. Normal launch must retain XV_SCENE_PHASES=0 and omit the
record-worker timing override. No save or production optimization was changed.

## Pi child result and private stack-translation candidate

The 120-second Pi job ended at its planned timeout (124), returning 31 reports
with 59550. The bounded fatal/mismatch/scope-omission scan found no matches.
Settled examples: 59550 inclusive 1.84–2.62 ms, remainder 0.68–0.96 ms;
597CB contributes 0.55–0.80 ms, followed by 59200 at 0.19–0.27 ms. These are
instrumented Pi numbers, not predicted Vita savings. The native 11B60 child is
already present. The result selects the vertex-building continuation 597CB for
a memory-access experiment rather than another replacement of that native leaf.

`tools/prepare_sprite_stack_candidate.py` prepares a private candidate from the
pinned installed body. It caches only the guest stack page translation for the
loop before the first pop: 48 float loads, six float stores, 28 word accesses.
All actual reads/writes and float conversion/rounding remain in place. The
page pointer refreshes after every actual scheduler handoff; crossing-page
stacks fall back to individual original translations. The post-loop callee and
unwind remain original. Checked-address builds take the original routine.

This is **unqualified, not deployed**, and changes no production policy.
Private ../sprite-stack-candidate/reference.c contains generated code, excluded
from commits. VitaSDK O2 Thumb compilation succeeded; original/candidate text
sizes are 0x1d22/0x1d1a. An eight-byte difference is not evidence of runtime gain.
Next construct whole-context/arena differential cases covering four-corner loop
branches, stack/output aliases, page boundaries, scheduler stack/mapping changes,
x87 TOP/status and the 5BA10 callee. Only after correctness qualification should
Pi timing or a private Vita build be considered. Avoid caching stack values or
changing double intermediates to float as a shortcut.

Vita dashboard restoration was independently confirmed on perf275; the latest
3600-second lease succeeded. No Pi or Vita capture job remains active.

## Stack candidate differential qualification

`tools/tests/sprite_stack.c` compares the entire xctx, 8 MiB arena, page table,
and ordered full-context callee/handoff observations. Expanded 2,048 cases cover
all eight x87 TOP values, four corner-loop entry indices, both output-format
branches, aligned/unaligned within-page and crossing-page stacks, stack/output
aliasing, handoff remapping/stack relocation/input edits, and modeled 5BA10
memory/context effects. The callee is modeled, not actual integration coverage.

The first 768-case host sanitizer and Pi runs passed. Code review then found
that the generic pointer fallback for float accesses bypassed the original
split-page helper. Existing cases had not demonstrated a mismatch from that
issue; it was corrected explicitly, without weakening checks. The expanded
candidate uses original x87_load/store_f32 when the complete stack span does
not fit one page. Word accesses retain their original unaligned semantics.

Corrected private split-safe/ host ASan/UBSan and Cortex-A9 Thumb Pi builds each
passed 2,048 cases, 1,536 matching handoffs and 1,024 matching modeled callees.
A mutant omitting stack-pointer refresh after handoff fails the full comparison.
These are bounded synthetic results, not proof of all aliases or concurrent
kernel behavior. The Pi fixture ran on cores 0/1 and is terminal. No build was
installed on Vita; perf275 remains with a renewed lease.

Next: VitaSDK-linked instruction qualification including per-thread mapping,
then a focused cost test. Real-callee integration and ordinary physical-Vita
frame-time checks are still required before promotion. The candidate is not a
proven speedup; generated test bodies and binaries remain private.

## ARM cost and Vita-linked qualification

The source fixture now supports SPRITE_BENCH=1: 50,000 calls per format with
identical stack/object reset and modeled callee overhead included in both sides.
The split-safe formulation improved ordinary one-page cases but cost ~4% more
on crossing-page cases. The generator now declines those at entry to the
original function; post-handoff access fallback remains for relocated stacks.

Refined Pi Cortex-A9 Thumb O2 results (ns/call, original/candidate):
case 0 2473.8/1815.0; case 32 1722.3/1361.7; crossing cases 192
2442.3/2440.0 and 224 1618.0/1616.6. Supporting microbenchmark only, not a
predicted whole-frame gain. Full 2,048-case correctness checks also passed.

The fixture setup is shared by sprite_stack.c and sprite_stack_arm.c.
`test_arm_sprite_stack.py --thread-mapping` compiled using VitaSDK and executed
2,048 Cortex-A9 instruction cases: context, arena, mapping, ordered handoff/callee
trace and FPSCR all matched. Actual test callee/preemption code executed; libc
firmware copies were modeled. A distinct live table and TPIDRURW-bound table
exercise render-view accesses. This is not Vita3K or physical frame-time evidence.

Private gameplay/ replaces only 597CB in the 59550 diagnostic shard. Its first
compile exposed declaration ordering: shard-local X_G uses xram_/xpt_ variables,
so SPRITE_REFRESH must follow their declarations. The generator was corrected
and the rebuilt integration-safe/ candidate is now compiling. Standalone test
headers do not reproduce every shard macro override; before promotion, qualify
that actual preamble and inspect full-game integration. No runtime settings,
Vita package, draw order or save data changed. Poll the existing build rather
than starting another. No integration gameplay run has started yet.

## Final shard qualification and perf277 candidate

The integration build and 120-second Pi run have now completed (planned timeout
124, cores 0/1). The bounded fatal/panic/scope-omission search found no matches;
this headless run does not establish rendering correctness. Private evidence:
`sprite-stack-candidate/gameplay/{run-result,phase-summary}.json` and `run.log`.

The final candidate with the actual generated shard preamble passed all 2,048
VitaSDK-linked instruction cases, including context, memory, mapping, handoff
trace and FPSCR. Within-page executed instruction counts were 3,169,920 original
versus 2,246,528 candidate; crossing-page fallback was 3,197,744 versus 3,210,032.
Modeled copy volume matched. These instruction counts are not physical timing.

Perf277 builds successfully from perf275 with the candidate opt-in enabled only
for code_010.o. Package audit confirms that is the sole changed recompiled
object, and game-a.self/boot-game.txt are the sole changed package entries.
Runtime SHA256: 7d8ba31adb1fea060d8d58084639817a64943e67edccef792204e0c242aec062.
The remote update has started; installation and ordinary hardware gameplay
results remain unverified. Retain perf275 as rollback and protect a30-perf211.
