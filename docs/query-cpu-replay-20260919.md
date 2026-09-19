# Complete world-query replay prototype

CPU-state replay now joins the [memory capture prototype](query-capture-qualification-20260919.md).
This is private ARM qualification, not an enabled Vita optimization. There is
no new hardware FPS result, and perf19 remains the device build.

## What changed

`xk_query_cpu.{h,c}` records explicit scalar input/output fields, native FPSCR
words supplied by the caller, raw ST/XMM payloads, actual FP write masks, and
consumed backedge budget. It has no native FP instructions, allocation, TLS or
mutable global state. It does not save and restore an old complete `xctx`.

The optional `--cpu-state` capture variant instruments physical x87 reads and
writes through the emitted query and private helpers. Read-modify-write right
operands are evaluated before recording the destination write. Each of the
three pinned vertex intervals marks its nine sequentially written XMM lanes:
XMM0/1 and XMM2.low. Those intervals initialize operands from guest memory and
have no admitted intervening callback or trap. Other XMM lanes remain current
caller state. Masks are never inferred from changed before/after values.

The initial contract is deliberately exact for the eight integer registers,
lazy flag fields, DF, FSP, FSW, FCW, FS base and supplied native FPSCR. Initial
ST/XMM payloads are keys only if read before their first write. MM, scratch,
fiber, EIP hint and optional ID must remain unchanged during capture and are
preserved from the current caller during replay. Actual owner/fiber admission
remains the integration caller's responsibility.

Replay requires a positive current scheduling budget strictly greater than
the captured cost and subtracts that cost from the current budget. It does not
restore the old budget. Callback/unknown paths invalidate; native FP trap
enables, malformed FSP, budget refills, undeclared FP changes and changes to
untouched fields reject capture. Comparing net budgets cannot independently
detect a callback/refill, so explicit invalidation is mandatory.

The private combined wrapper validates both CPU and memory before any replay
store, then restores the recorded memory, CPU effects and native FPSCR. Caller
ownership must keep these inputs stable throughout validation and publication.

## Evidence

* Host tests and ASan/UBSan pass with EFLAGS.ID both off and on. Tests include
  same-value stores, raw NaN payloads, preserved fields, dependency rejection,
  budget boundaries, traps, callbacks and unchanged host rounding/exceptions.
* Fourteen actual ARM queries match original complete memory/context/FPSCR,
  then reproduce all three on replay. Original traced memory dependencies are
  covered and the written-byte footprints match exactly.
* 208 original-versus-capture comparisons pass across aliases, x87 stack
  positions, native FP modes and preemption; preempted records are abandoned.
* 140 warm comparisons first seed the cold ST/XMM slots with their eventual
  final values, deliberately hiding real writes from a difference-only scheme.
  Warm calls then supply different ST/XMM signaling-NaN payloads, MM/scratch/EIP
  hints and a larger budget. Complete replay matches fresh original execution.
* 420 combined replay rejections for budget, scalar and native FP mismatch
  preserve the complete arena, context and supplied native FP state.

No initial ST/XMM payload was consumed in those tested paths; the recorder
tracks such reads rather than assuming they are impossible elsewhere. Host
CPU-record size is 680 bytes, in addition to the bounded memory record.

## Cost and next hardware decision

| Synthetic case | Original query instructions | Query with both recorders | Complete replay instructions |
|---|---:|---:|---:|
| Split traversal |103,835|1,689,087|10,155|
| Winding |40,161|611,764|7,103|
| Edge |38,338|569,907|7,133|
| Negative traversal |1,215|18,739|3,486|

The replay wrapper includes guard entry/exit and CPU/memory checks. Counts still
exclude real hardware timing, firmware bulk-copy cost, cache lookup/admission,
cache behavior and scheduling. They do not establish a frame-time gain.

Heavy fixtures need roughly 17–21 subsequent successful replays to amortize
capture in this optimistic instruction model. Short searches already lose on
each replay. Capturing every query or every miss is therefore unsuitable.

Next, integrate a bounded selective adapter retaining the untouched original
path for ordinary calls. It must measure surviving exact matches under these
CPU/memory keys, avoid short searches, limit cold captures and suppress repeated
recapture after mutation. The earlier 54.5% input-only match census does not
prove this stricter cache will pay off. Only a fresh ordinary hardware campaign
run can establish its value; reject the cache if it adds cost without sufficient
surviving expensive repeats. Preserve the cumulative existing optimizations.

## Reproduction

```sh
python3 tools/test_query_cpu.py
SANITIZE=1 python3 tools/test_query_cpu.py
python3 tools/test_query_memory_capture.py
python3 tools/query_memory_capture.py --cpu-state \
  --recomp-dir /private/perf19/recomp --out /private/capture-cpu-generated
```

Private compiled-query evidence is in `../collision-query-cpu/qualification/`:
build commands/fixture, retained ELF, `check.py`, `check_variants.py`,
`check_warm.py`, result files and SHA256 receipt. Generated game bodies remain
outside the repository. Production wiring, lifetime admission and sustained
campaign validation are still pending.
