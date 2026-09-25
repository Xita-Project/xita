# Point-location prototype (17A8B0)

This is an unhooked candidate, not an installed optimization. `xk_point_location.h`
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
No runtime hook or default is changed.
