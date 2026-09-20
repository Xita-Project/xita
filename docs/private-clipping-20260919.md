# Private-stack clipping candidate

The initial perf.40 wait attribution to `clip_region_native` was incorrect;
see the corrected attribution below.
The optional `XV_CLIP_PRIVATE=1` path shortens the B71C0 guard when all mutable
inputs, outputs, arguments and scratch storage belong to the calling worker.
Ordinary builds default to zero. The cumulative perf.41 trial is now installed; gameplay qualification follows.

The original guard is acquired, shared counters are updated, and the original
stack probe runs first. Admission then checks the exact worker context and
outermost guard, unchanged private stack mappings, count/capacity bounds, no
optional output pointers, disjoint control/plane/output spans, canonical image
constants, forward string direction and adequate instruction budget. Identical
input/output remains valid because B71C0 uses its original stack temporary.
Other aliases, shared spans, nested transactions, hold instrumentation and
uncertain cases retain the original guard. Arithmetic, output order and guest
state are unchanged. The fused region's completion counters remain atomic.

At most 64 input vertices and capacity 64 are admitted. Capacity checks precede
output appends; exceptional arithmetic does not remove the bounded loop count.
The 65,536 remaining-backedge requirement keeps the admitted region away from
the worker instruction-budget stop. No owner-service call occurs between release
and the inner clip return; the next original lock boundary can park the worker.

## Current validation

- `tools/test_clip_private_admission.py`: 118 ASan/UBSan cases covering mappings,
  boundaries, aliases, guard depth, worker context, constants and controls.
  Admission preserves guest context and memory, including on rejection.
- `tools/test_clip_private_execution.py`: 512 ASan/UBSan owned-image cases
  compare the original wrapper, existing locked fused wrapper and candidate.
  Complete guest context/memory match; 1,623 inner clips actually release.
  Host floating-point exception flags match locked versus released fusion.
  Cases include in-place, shared-input, shared-output and enclosing-lock paths.
- The changed worker backend and generated region compile with VitaSDK for
  Cortex-A9/Thumb/NEON.

The initial execution harness incorrectly entered the fused interior directly;
its failures were not accepted. It now uses the production first-positive-test
wrapper hook. Host exception flags are compared against the existing fused
implementation, not used to assert equivalence of host and ARM FP behavior.

These fixtures are local correctness tests, not hardware FPS comparisons.
They do not establish actual parallel scheduling correctness or hardware gains.
The follow-up execution checks below qualify this candidate for a cumulative
hardware trial; hardware performance and long-session stability remain unproven.
Private generated code and receipts are in `../clip-private-execution/`.

The candidate is intentionally bounded rather than removing the shared guard
from all clipping. On hardware the release counters must show how much work
qualifies; they cannot by themselves establish a frame-time improvement.


## Follow-up execution validation

The `--workers` fixture uses the actual pthread worker pool, semaphore wakeups,
shared mutex, private stack mappings, owner-service parking and job completion.
Both workers enter concurrently and each executes 32 polygon regions; one asks
for owner service during the pass. Complete final guest contexts and memory
match the locked implementation, both workers retire with balanced guards,
and each releases 32 inner clips. ASan/UBSan report no failures. This validates
this exercised schedule, not all interleavings or Vita scheduler behavior.

`tools/test_clip_private_arm.py` executes the Vita-compiled original, locked
fusion and private fusion in a Cortex-A9 instruction model. All 64 cases match
complete guest context, memory and FPSCR; 222 clips release. Cases cover four
rounding modes, private/in-place/shared inputs and outputs, and selected
exceptional floating-point inputs. Imported memory-copy operations are modeled;
this is neither a GPU emulator test nor a hardware performance measurement.

Evidence: `../clip-private-workers.log`, `../clip-private-arm.log`, and
`../clip-private-execution/arm-result.json`. The upcoming cumulative build will
retain all perf.40 selections and add `XV_CLIP_PRIVATE=1`.


## Cumulative hardware deployment

Perf.41 / `1e249ea` built with all perf.40 selections plus
`XV_CLIP_PRIVATE=1`. Only `game-a.self` and `boot-game.txt` differ in the VPK.
The linked executable contains `xv_object_clip_release`. Runtime size is
32,208,546 bytes; SHA-256:
`173ddca818a9e9e4b3909c1af10033ef138e3bfb8443294a4b35153d5c4c50eb`.

The updater verified the bytes, requested a restart, and boot-confirmed slot 1.
Remote status and dashboard both show perf.41 / `1e249ea`. The ordinary campaign
launch sequence is underway. This confirms installation, not gameplay performance
or stability. No automated off/on/off FPS test was run. Receipts and subsequent
campaign captures are under `../clip-private-hardware/`.


## Corrected wait attribution

Perf.41 reached the saved campaign, visually confirmed in `loaded-followup.png`.
Later 60-frame windows report 12.9 and 12.7 FPS. Both private-clipping lanes
report zero candidates and releases: this run does **not** exercise the new
release path or demonstrate a benefit. The linked hook and build flag are
present. Do not infer which admission condition failed from these counters.
The copied CPU context in the fused query path is one possible restriction,
not an established cause of rejection in this scene.

The original reason for targeting clipping was a symbolization error. Vita
loads the executable at a shifted address. Perf.41 logs the math-lock anchor at
`810D9AB5`; its matching ELF places it at `8107AAB4`. Clearing the Thumb bit gives
a load slide of `0x5F000`. The earlier perf.40 anchor used a different slide,
`0x35000`. Direct addr2line on unadjusted runtime PCs returned misleading names.

`tools/symbolize_object_waits.py` now derives the slide from the log anchor,
subtracts it and resolves the call's final halfword against sized ELF text
symbols. It retains separate report windows, handles changed anchors between
runs, refuses missing/unaligned anchors, and leaves addresses outside known
functions unresolved. Supply the ELF corresponding to the logged executable.
Its regression fixture covers those cases, including an LR at a symbol boundary.

The last captured perf.41 report resolves the largest waits as:

| Caller | Lane 0 wait | Lane 1 wait |
| --- | ---: | ---: |
| `xv_math_quaternion_matrix` | 276,919 us | 303,737 us |
| guest `4C980` | 111,153 us | 120,339 us |
| `xv_math_matrix_multiply` | 87,376 us | 112,487 us |

These are per-report, overlapping lane totals, not independently recoverable
frame time. The corrected receipt is `symbolized-waits.json` beside the captures.

The existing private-quaternion bypass is compiled but runtime-disabled in this
build. Its earlier isolated test did not demonstrate a whole-frame gain and
moved some contention to other helpers. The next cumulative experiment should
consider that already-qualified path together with the current optimizations,
using normal gameplay after restart, rather than repeat an automated FPS
comparison. Keep tracking whole batch/frame time and correctness; reducing one
lock's wait is insufficient evidence of a gain.
