# Model-collision predicate candidate

The perf269 AR slow frames predominantly occur before final presentation.
Prior aligned Pi profiles identify object-vector collision (171AF0) and its
model-node child 1731D0 as recurring update work. The native BSP and matrix
callees already exist. The earlier 171AF0 shift/register experiments did not
show a benefit and are not repeated here.

A private 1731D0 candidate replaces only nine distinct branch-site predicates
(eleven emitted occurrences, including duplicated translated paths) with direct
comparisons of the operands just tested. Original lazy-flag writes remain,
including dormant CF/OF fields. Calls, memory accesses, node/permutation order,
nearest-hit result updates, stack effects and preemption sites remain intact.
This does not cache transforms, omit nodes or change collision quality. The
retail unsigned-byte permutation behavior remains; byte 255 is not treated as
NONE. See `halo-reference-object-vector.md` for the reference/retail distinction.

Private files: `../model-collision-predicates/`, including reference/candidate C,
site-count audit, exact ARM flags, object files and binary hashes. No generated
game body is committed or enabled in production.

Same-header Cortex-A9 Thumb O2 object text: reference 2,872 bytes; candidate
2,676 bytes (196 fewer). This is code-size evidence only, not a speedup.

`tools/test_model_collision_predicates.py` extracts both supplied private
bodies, rejects unexpected callees and builds the hand-written fixture in
`tools/tests/model_collision_predicates.c`. The fixture compares full context
(including all lazy flags), full 1 MiB arena, full context/argument observations
at every modeled callee, and preemption counts. It covers empty/negative node
counts, omitted regions, zero/negative BSP counts, permutation clamping,
multiple nodes, hit/miss results, all eight x87 TOP inputs and a nonidentity
page table. It does not model arbitrary aliasing, invalid region indices,
page-boundary records or actual native math/BSP implementation behavior.

Host O1 ASan/UBSan and Pi core 0 Cortex-A9 Thumb O2 both passed 4,096 cases,
20,716 candidate callee observations. The Pi transfer initially failed with a
transient route error, then completed successfully; no successful run was
restarted. The Pi had Chromium activity, so this run is correctness evidence,
not a timing result. No Halo 2 files or processes were modified.

Reproduce the host check:

```sh
python3 tools/test_model_collision_predicates.py \
  ../model-collision-predicates/reference.c \
  ../model-collision-predicates/candidate.c \
  --headers ../frame-slow-candidate/build-x87/recomp \
  --output ../model-collision-predicates/host-repro \
  --flags='-O1 -g -fsanitize=address,undefined'
```

For ARM use `--cc` with the ARM Linux compiler, `--build-only`, and
`--flags='-mcpu=cortex-a9 -mthumb -mfpu=neon -mfloat-abi=hard -static'`.
Run the resulting `test 4096` under `taskset -c 0` on the Pi.

Next gate: compare actual collision-path execution cost in the aligned Pi
harness, preserving the same native callees and observers. If useful, qualify
real callee integration and fingerprinted hook admission before a Vita build.
Do not claim that this small candidate will resolve the entire firing deficit.
Hardware remains perf269; no update or gameplay restart was performed here.
