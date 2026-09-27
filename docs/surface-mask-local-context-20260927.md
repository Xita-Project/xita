# Visibility-mask local-context candidate

Experimental and not installed on Vita. Goal remains sustained 20 FPS on a30.

The preceding GPR-only formulation regressed sparse masks. This variant keeps
the routine's working context local so the compiler can optimize both integer
registers and lazy flag state. It retains all original instructions, branches,
guest memory operations and scheduler handoffs. At each handoff it publishes
integer registers, nine lazy-flag fields and preemption budget into the original
context, calls the original scheduler with that original pointer, then reloads
state. Exit publishes the same modified fields. Other fields are never copied
back over the caller's state. The phase scope still receives the original
context identity. No cached visibility answer or altered visibility decision.

`prepare_surface_mask_registers.py --local-context` rejects retained-body hash
or helper drift and emits game code only into a private directory.
`test_surface_mask_context.py` builds the generated reference/candidate with the
source-only fixture. The production runtime and compiler default are unchanged.

## Evidence

Private ../surface-mask-local-context/ and selective/ contain receipts.
Host ASan/UBSan and Cortex-A9 Thumb ARM/Pi each passed 768 full-context/memory
comparisons and 2,520 matching handoff states after expanded mutations. Cases
include empty/dense/sparse/mixed/partial masks, different handoff budgets,
mask page relocation, count changes, index/source-register changes, flag changes,
unrelated context-field changes and stack relocation at a handoff. Removing
publication before scheduling fails the handoff-budget assertion. This is bounded
synthetic coverage, not exhaustive aliases or real concurrent execution.

Selective-publication Pi microbenchmark, 128 entries, 50,000 calls per pattern:

| Mask | Reference ns/call | Candidate ns/call |
|---|---:|---:|
| empty | 213.6 | 198.5 |
| bit 0 | 4,927.1 | 4,538.9 |
| bit 31 | 4,781.6 | 4,524.4 |
| all set | 8,897.9 | 6,595.1 |
| alternating | 6,787.2 | 5,685.8 |
| bits 0 and 16 | 5,076.1 | 4,680.9 |

Supporting evidence only: not Vita timings or whole-frame savings. An earlier
full-context publication variant also improved these masks, but selective
publication avoids overwriting unrelated state. The final generator uses a
prefixed preemption macro to avoid changing subsequent emitted functions; its
host fixture passed again after that integration cleanup.

## Headless integration in progress

The private gameplay shard replaces only 53E90 in the retained point-location
harness. That original body is byte-identical to perf275's target. An audit
checks replacement reversal restores the whole original shard. The full ARM
harness built successfully. Other harness units are older than perf275; results
cannot establish current hardware frame times or validate rendering.

A 120-second Pi cores 0/1 run with the documented a30/two-input-burst sequence
has started, remote executable harness-codex-mask-context, log
runs/codex-mask-context-20260927.log. Command and result receipts live in the
private gameplay directory. Wait for the actual process result before restarting.
No Halo 2 resources or user saves were modified. Perf275 remains on the Vita.

Before promotion: inspect integration results and actual exercised paths, audit
context ownership/diagnostic admission, verify Vita-compiled output, then create
an isolated hardware candidate preserving all qualified optimizations and saves.
Neither a short headless run nor the microbenchmark proves a hardware gain.

The bounded Pi job completed with its planned timeout exit 124. Final telemetry
shows loaded=1, active=1 and director=1 at the established pod camera. The log
contains five constant-pool exhaustion lines, also a known issue in older
headless diagnostics; this prevents treating the run as rendered correctness
qualification. The bounded scan found no fatal/trap/mismatch/abandon/scope-overflow
lines. Absence of those messages is not equivalence or crash-free hardware proof.
No per-call target admission counter was added, so this is integration evidence
rather than a measured target call census. summary.json and run.log preserve the
actual observations. All Pi test jobs are now terminal. Next qualify the exact
Vita-generated body and diagnostic/context admission before packaging.

## Vita compiler and thread-mapping qualification

`tools/test_arm_surface_mask_context.py` and its source-only ARM fixture now
compile the reference and candidate using VitaSDK GCC O2 Thumb Cortex-A9.
The linked ARM instructions passed 192 full context, 8 MiB arena, page-table,
handoff-trace and FPSCR comparisons. The fixture executes its actual preemption
handler; only libc firmware copies are modeled. Private receipts are vita/ and
vita.log. This is instruction-level correctness evidence, not Vita FPS.

A second 192-case run (`--thread-mapping`, vita-thread/ and vita-thread.log)
uses `__vita__`, `XV_THREAD_PAGE_TABLE=1` and `XV_RENDER_VIEW=1`. The fixture
binds TPIDRURW to a table different from g_xpt, accesses image globals through
that thread table, and mutates mappings at handoff. All comparisons passed.
This covers the compiled memory-access mode, not a real kernel context switch
or concurrent scheduler execution.

Ownership review: xk_thread embeds its host context, and the scene helper owns
a static host context outside the guest arena. The candidate calls xv_preempt
with that original owner after publication, preserving scene-helper and object
worker identity checks. It reloads after the callback. The phase scope likewise
uses the original owner. Ordinary memory/flag operations have no context-aware
callback. Checked-address instrumentation can invoke diagnostic callbacks during
access, so it must retain the original routine rather than observe stale owner
registers. Contexts aliased into guest RAM are not an admitted runtime use case.

`tools/prepare_surface_mask_context_shard.py` stages a private copy of the
production shard, checks the exact qualified body (allowing its known phase
scope), and proves replacing the staged region with the original restores the
whole shard. The experiment requires `XV_SURFACE_MASK_CONTEXT=1`; default and
checked-address builds retain the original body. Its macros are undefined before
following functions. The first staged output is code_009_guarded.c with an audit
receipt beside it. No generated guest code is committed.

Hardware remains perf275; no candidate package has been deployed. Next build
this guarded shard with the retained production flags, audit the package against
perf275, then test normal a30 gameplay with the protected save and rollback.

## Isolated perf276 build staged

Private `../mask-context-hardware/` copies the retained perf275 build, preserving
its flags and runtime stack. Only recomp/code_009.c differs among translated
sources/headers; source-scope.json records that comparison. The private Makefile
adds XV_SURFACE_MASK_CONTEXT=1 only to code_009.o. Version is perf276, qualification
revision 104d3696. The compile command confirms O2 Cortex-A9 Thumb and the existing
thread-page-table/render-view modes. No default source-tree build policy changed.

The build was started once with build_x87.py; build-x87-result.json is written
only on process completion. audit-package.py requires success, exactly code_009.o
changed among recomp objects, and only game-a.self/boot-game.txt changed in the
final package. It retains perf275's other assets and updater contract. Do not
deploy until these checks pass. The launch/collector scripts assert perf276 and
preserve the existing protected-save settings. Hardware has not yet changed.

The perf276 build and package gates passed: code_009.o is the only changed
recomp object; final VPK differs only in game-a.self and boot-game.txt. The
53E90 symbol shrank from 0x788 to 0x696 bytes, which is not an FPS measurement.
Runtime size is 34,818,350 bytes, SHA-256
3801a9f4244cd6f0ee4ac9a46737ecaf19387345d9c843bdcafe95b235ef2c7e.
Package SHA-256 is
7e57569c4b3acf86cef7151fe61cf8dc860beb2fe595d52d3d2a91fc4826ebba.
Updater contract remains
775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.

The authorized update completed with verified=true and boot_confirmed=true,
slot 0. An independent status returned V0.2.0-perf.276 / 104d3696 at dashboard
(timing_frame=0). Perf275 occupies the other slot; retained perf273/275 VPKs
remain available. The batched a30 launch was started and renews the awake lease.
No frame-time result or gameplay correctness conclusion exists yet for perf276.

## Perf276 hardware result: no demonstrated overall gain

Both collectors completed. Screenshots confirm the saved lifepod, rifle
firing/reload, and the expected outdoor canyon after movement. All values below
are CPU Present intervals at the retained 360p settings, not GPU execution time.

| Interval | Samples | Mean ms | FPS | p95 ms | Max ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|---:|
| pod | 720 | 56.644 | 17.654 | 74.338 | 97.090 | 512 / 0 |
| fire | 64 | 82.425 | 12.132 | 116.562 | 126.139 | 63 / 7 |
| move | 81 | 67.090 | 14.905 | 94.064 | 274.426 | 81 / 1 |
| outside | 941 | 48.124 | 20.780 | 58.370 | 105.429 | 197 / 2 |

Perf275 preceding means were 57.113 / 78.240 / 64.310 / 48.469 ms.
The small pod/outdoor changes do not establish useful improvement; firing and
movement were slower. Separate ordinary runs are not identical workloads or a
statistically isolated regression test. No crash was observed in this short
sequence, but it does not qualify AI/audio/cutscene/checkpoint/15-minute stability.
The outdoor average above 20 FPS still hides 197 frames exceeding 50 ms.

Keep the candidate experimental, default off, with its correctness evidence.
A rollback to perf275 was requested; initial status during restart refused the
connection. Confirm actual boot/version before stating rollback completed.
Neither collector is running now. The next work must address the larger
firing/update/recording cost, not repeat this local-context trial without new
evidence. The separate ~274 ms movement hitch remains an I/O attribution lead,
not an explanation for the sustained firing cost.

Rollback is now confirmed by independent live status: perf275 / d0975e0b,
timing_frame=0 at dashboard. A fresh 3600-second keep-awake lease succeeded.
rollback-status.json preserves the receipt. No test jobs or held controls remain.
