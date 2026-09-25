# Point-location prototype (17A8B0)

This candidate has an opt-in retained-shard hook and is installed in perf217 on the Vita. `xk_point_location.h`
reduces transient x87 context traffic while traversing the BSP point-location tree.
The retained implementation has no callees and one preemption backedge. The
prototype keeps stack writes, register/flag results and that scheduling boundary;
it reloads inputs after a yield. It preserves double arithmetic and association,
without contraction or fast-math.

The first prototype disagreed on a NaN payload in a dead x87 slot in case 378 on
both x86 and ARM. Exceptional results now use the context-based x87 operation
sequence. Do not remove this path merely because the leaf result agrees.

`tools/test_point_location.py` extracts a hash-pinned reference from a private
retained stage; generated code is never committed. The fixture compares the
whole context, 4 MiB guest arena, preemption count, context hashes at yields and
host floating-point exception flags. It changes point memory at yields, shuffles
mapped data pages, crosses float reads over page boundaries, varies tree depth,
x87 TOP and status, and exercises random float bit patterns including non-finite
values. Both leaf branches and the outside sentinel are covered.

Example (from the authoritative source checkout):

```sh
python3 tools/test_point_location.py ../helper-cache-candidate/build-x87 \
  --out ../point-location-candidate/host
```

Pass `--cc /path/to/arm-none-linux-gnueabihf-gcc` to build a static Cortex-A9 Thumb
fixture for execution on the Pi. This is supporting correctness evidence, not a
Vita speed measurement. The expanded `--snapshot` fixture uses a distinct active page table with stale
live mappings, tests all four rounding modes and point data overlapping the
saved-register stack area. Host and ARM each passed 4,096 cases. `--mutant side`
and `--mutant dead-slot` were both caught on the host. These tests do not cover
every possible guest-memory alias layout or concurrent runtime behavior.

On the Pi, the Cortex-A9 Thumb binary's synthetic 32-node finite traversal
(200,000 calls per implementation, no yields) measured 2,467.6 ns/call for the
retained body and 2,007.3 ns/call for the candidate. This single microbenchmark
excludes hook/verifier overhead and is not a hardware frame-time prediction.
Run the compiled fixture with `bench` to repeat it. In-game verification and
Vita measurements remain required before enabling the candidate.
`tools/patch_point_location.py` adds the wrapper only to the pinned retained
function and rejects drift or a second application. `XV_POINT_LOCATION=0` is the
default; 1 verifies and retains the guest result, 2 runs the native. Object-worker
calls and unaligned stacks decline to the guest. A mismatch disables the candidate.
The report uses session totals and is called through the existing weak report chain.

Verification saves/restores the eight bytes written by the prologue and the input
context/FP state. It defers preemption during replay, compares both results, then
applies the guest's backedge budget at return. This is a correctness diagnostic,
not representative scheduling or performance. Normal native mode retains each
backedge. The wrapper passed 4,096 cases on host and ARM; an injected wrong-side
mutation was detected while retaining the guest result. Full-game Pi verification passed 765,691 calls. Physical Vita verification in the
a30 lifepod passed 1,046,158 calls with zero mismatches or declines. These
checks compare this function, not the entire simulation. A cold launch in native
mode is underway; no whole-frame performance benefit has been established.
