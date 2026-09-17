# Optional native collision vertex pass

This experiment replaces the first vertex-ring pass inside the full `86F50`
collision function, from `86F9B` through the branch to `8709A`. It combines
distance testing and ordered vertex deduplication into one native component.
Later edge and surface tests remain the original lifted instructions. The
component does not release the existing transaction, change publication, or
introduce concurrency. Both the build flag and runtime mode default OFF.

The physical nine-scope measurements identify the `88110 -> 87EA0 -> 87E10 ->
86F50` query path as a useful investigation target. Those timings are nested
inclusive samples, including world and object queries and calls outside a
sampled `171F10`. They do not measure this vertex pass separately. No frame-time
improvement follows from the instruction counts below.

## Preserved boundary

The hook admits once per ring at `86F9B`. It retains all original guest reads
and writes, including stack scratch, radius spills, XMM lanes, x87 stale slots,
status and final lazy flags. It scans the output list in its original order,
retains its signed count comparisons and 256-entry append limit, and performs
the original read/modify/write when appending. There is no dead-stack exemption
or memory comparison mask.

Integer registers and dedupe lazy flags/budget can stay in C locals between
original observation points. At each taken original backedge that exhausts
the budget, the helper publishes the exact context before `xv_preempt`, then
reloads it. Resumption goes to the already-selected target even when the
callback changes registers. Untaken backedges do not consume budget. All
context is published before continuing at original `8709A`; there is no child
call inside the replaced region.

The helper receives the containing generated function's captured `xram_` and
`xpt_` roots. Its ordinary accesses use those roots, including any changed
page-table entries after a callback. Existing x87 page-aware load/store helpers
retain their existing global-root behavior. No new cached guest pointers,
allocated buffers, shared scratch, or geometry lifetime assumptions are added.

On ARM, unmasked FPSCR exception enables (`0x00009f00`) decline admission. The
same check follows a real callback. If the callback enables exceptions, the
helper publishes no early output context: its already-published context resumes
original `86FBA` (ring) or `87050` (dedupe), at the exact committed target, and
the active scope retires. No instructions are replayed. The helper retains the
original arithmetic operation order rather than reassociating distance sums.

This retains the existing scheduler contract: guest observation and mutation
occur at explicit calls/yields. It does not authorize asynchronous access to
an executing guest context or make an existing data race safe.

## Control and integration

Build with `XV_NATIVE_COLLISION_VERTICES=1`. The runtime module starts OFF and
has these APIs:

| API | Contract |
| --- | --- |
| `xv_collision_vertices_init()` | Bind once to the actual native owner thread at a drained boundary; repeated calls require that same owner. |
| `xv_collision_vertices_available()` | Atomic read: owner initialization completed. |
| `xv_collision_vertices_enabled()` | Atomic read of current mode. |
| `xv_collision_vertices_control_ready()` | Nonfatal check: initialized, same native owner, and no active helper scope, including a suspended guest fiber. |
| `xv_collision_vertices_override(int)` | Drained bound owner only; positive enables, zero/negative disables. |
| `xv_collision_vertices_calls()` | Drained bound owner only; take/reset actual admitted ring count. |

Control checks call `xv_object_math_report_check()` when linked and reject an
active helper scope. Joining worker jobs alone is insufficient: the object
pass must have finished and cleared its owner as well as its count/running
state. The established drained Present path after the completed pass is the
intended boundary. The selector-44 integration should drain presentation and
initialize immediately before `xv_benchmark_step`, which checks availability
before applying optimizations. Once initialized, defer mode changes and counter
takes while `control_ready()` is false. Defer control boundaries outside timed
arms, or mark an interrupted trial noncomparable before deferred restoration;
continue normal measured-frame accounting. Blindly skipping measured-frame
steps would bias FPS. Do not yield between readiness and control. A different
guest fiber may reach Present while one retains a helper scope; finishing an
object pass does not rule that out. Restore the exact incoming mode on
completion and failure, deferring restoration too while a scope remains held.
Controller/main/client files are deliberately outside this patch.

The control module does not independently stop workers from starting: callers
must honor the existing drained scheduling boundary. The helper itself may run
on either owner or existing object workers under their original ownership and
locks. Mode/scope/counter accesses are atomic; there is no native thread lookup
inside the ring. Control changes while active fail rather than wait.

Selective generation, from this checkout with the owned image and manifest:

```sh
python tools/test_collision_vertices.py \
  --xbe /absolute/path/to/default.xbe \
  --manifest /absolute/path/to/game_manifest.json \
  --out /new/private/output-directory --emit-only
```

The tool verifies the whole image and function span, independently emits the
original, verifies compile-OFF identity, and writes
`f_00086F50-{original,candidate}-private.c` plus `oracle.json`. Install only the
candidate full-function fragment into existing `code_013.c`, including its
guarded header include, preserving the unit prologue and every other function.
Before replacement, compare the current full function against the emitted
original; investigate instrumentation differences instead of deleting them.
The independently emitted original matched the supplied private stage exactly.
Do not regenerate the whole unit or its root list from this isolated fixture.

Stage the header, control C file and gated build changes as well as the function
fragment. No core emitter/prototype change is required. Interior entries are
unchanged. Whole-image and 884-byte function-span hashes restrict the game
hook. Span drift declines the transformation and boundary drift rejects it.
The tool refuses optimized Python (`-O`). Original function bodies stay private.

| Identity | SHA-256 |
| --- | --- |
| Owned image | `4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae` |
| `86F50`, 884 original bytes | `dac5ac8da738ab412fd265fb824a2a6abe9cde4f6fef19fb1a873ba5aba73d34` |
| Original emitted function, no added newline | `b7a0c0e3abe305f1e43249dbf7126d10548888a081ab0d0dc67b240284b1f62f` |
| Candidate fragment file | `6b8109452cb775d36d321fc300657e262cd5a288dc13a6da424a9a9d4952fb41` |

## Qualification and cost

Host ASan/UBSan passed 2,048 independent owned-XBE region comparisons: complete
context, the complete 1 MiB fixture arena and its mappings, with 22,536 matching
yield observations. Yield observers hash the complete context and 72 bytes of
active guest stack; final memory comparison is unmasked. Cases cover ring
sizes 1/3/4/8/16/32, empty through full/over-capacity lists, first/last duplicate,
misses, nonfinite inputs, alignments, page crossings, source/stack aliases and
callbacks changing registers, stack, query pointer, count, vertex, mappings and
x87 state. A second ASan/UBSan run models FP enable changes and proves both
original continuation targets and initial decline. That model replaces only
the FP admission read; it does not emulate real hardware exception delivery.

Another 128 comparisons run the entire original and hooked `86F50`, with the
candidate ON/OFF and original later edge/surface paths. `B0CB0` is a recording
stub with both AL results, volatile register/x87 changes and its correct stack
cleanup. This proves integration at that call boundary, not the mathematics of
the original `B0CB0` child.

Actual Cortex-A9/Thumb ARM instruction execution passed 249 comparisons,
including 128 FP variants spanning all eight x87 stack tops, four ARM rounding
modes, FZ/DN/status bits and nonfinite payloads, plus nine runtime-OFF cases.
It compares full context, full arena and page table, yield observations,
native FPSCR, actual admission count and scope retirement. Firmware imports
are modeled; this is instruction execution, not physical cycle measurement.

Control ASan/UBSan and TSan passed default OFF, owner identity, pending math
work, nonfatal readiness while the same owner retains a scope (modeling a
suspended guest fiber), active-scope rejection, cleanup returns, two native callers with 20,000
exact admissions, take/reset and negative restore. The control stress tests
scope/counter concurrency; it does not invent shared guest-query ownership.

Representative ARM instruction counts, normal FP mode and budget 100,000:

| Ring vertices | Existing list | Original | ON candidate | Change |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 569 | 608 | +6.9% |
| 1 | 128, immediate duplicate | 531 | 607 | +14.3% |
| 3 | 0 | 1,720 | 1,691 | -1.7% |
| 3 | 16 | 4,942 | 2,745 | -44.5% |
| 3 | 128 | 27,454 | 9,801 | -64.3% |
| 8 | 0 | 5,770 | 4,766 | -17.4% |
| 8 | 128 | 74,384 | 26,316 | -64.6% |
| 32 | 0 | 48,526 | 26,834 | -44.7% |
| 32 | 256 | 562,532 | 186,800 | -66.8% |

These are complete-region counts including actual admission/retirement. Empty
and immediately satisfied one-vertex queries lose. Actual ring/list lengths
and the share of this pass in gameplay remain unmeasured.

Per-flag costs are distinct:

* Compile OFF: preprocessed full function is byte-identical; the control module
  is absent and the object imports no candidate symbols.
* Compile ON, runtime OFF: one atomic mode read and branch per ring; representative
  empty-list cases add 8/6/1 executed instructions for 1/3/8 vertices. Compiler
  layout also changes some path counts. Native stack/text growth applies even
  when runtime OFF.
* Runtime ON: entry mode read, FP read, scope CAS and admission increment;
  scope decrement on exit and FP recheck only after actual yields. There are
  no per-vertex counters, allocations or clocks. Control take/reset is outside
  the hot path. The 32-bit admitted count wraps naturally; drain reports often
  enough for the experiment.

The production-shaped standalone full-function ARM compile has 9,472 bytes
of text and 128 bytes static native stack OFF, versus 11,896 and 248 ON. The
control object adds 356 text bytes and 16 BSS bytes; its largest static stack
entry is 16 bytes. These are object measurements, not final linked application
sizes. The production-shaped compile includes the header after the generated
unit's captured-root `X_G` override.

Reproduce region/full-function tests with the generation command minus
`--emit-only`, adding `--sanitize`, `--sanitize --fp-model`, or `--arm`, each
with a fresh output directory. Control fixtures compile
`tools/tests/collision_vertices_control.c` together with
`recomp/kernel/xk_collision_vertices_control.c`, `-Irecomp`,
`-DXV_NATIVE_COLLISION_VERTICES`, `-pthread -lm`, and respectively
`-fsanitize=address,undefined` or `-fsanitize=thread` (use `-no-pie`).

The next gate is a same-camera, separately controlled OFF/ON/OFF physical
comparison with actual admitted counts and frame elapsed time, leaving the
object-collection candidate OFF. Require exact restoration and no ownership or
runtime errors. Synthetic savings alone do not select a default. No hardware,
authoritative source, stage or emulator was modified for this qualification.
