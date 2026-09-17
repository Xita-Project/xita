# Separate collision-query fusion generation

`XV_NATIVE_QUERY_FUSION=1` selects the qualified separate translation unit for
the static `172C95 → specialized 171F10 → 171F94` route. The repository default
is **0**. There is no runtime selector, environment override, counter or new
query scheduling. The existing transaction stays held. This wiring does not
change any of the seven retained optimization settings.

The generator installs only local, ignored `recomp/query_fusion.c` and the
guarded call/collection clone in local `recomp/code_028.c`. It never rewrites
`code_013.c` or `code_016.c`. The latter units retain generic query functions,
interior entries and overflow targets. Appending fusion to the large generic
unit was measured slower during qualification and is deliberately unsupported.

## Build and generation

Keep the existing retained build invocation and add
`XV_NATIVE_QUERY_FUSION=1`. It requires `RECOMP=1`, the `halo_ce_3925` profile,
and all four compiled prerequisites:

```
XV_NATIVE_BSP_SPHERE=1
XV_NATIVE_COLLISION_VERTICES=1
XV_NATIVE_SEGMENT_SPHERE=1
XV_NATIVE_COLLISION_TRAVERSAL=1
```

Existing startup defaults remain supplied by the retained build. They are not
inferred from these four feature flags or overridden by fusion. In particular,
typed traversal reads `xv_collision_traversal_mode`, vertices retain their
atomic enabled/scope admission, and segment sphere retains its atomic mode
check; all original decline paths remain. Compilation alone does not imply
those helpers are initially enabled. The cumulative build already supplies
their enabled startup defaults. The qualified combined oracle also covers
typed traversal initially OFF.

The ON build automatically invokes `tools/gen_native_query_fusion.py` using
`XBE`, `XBE_JSON` and `PYTHON` from the build. Those variables must identify
the owned image, its matching private manifest, and a Python environment with
the existing recompiler dependencies. For an explicit generation-only step,
use the same retained arguments with target `query-fusion-generate`, or:

```sh
python tools/gen_native_query_fusion.py \
  --xbe "$XBE" --manifest "$XBE_JSON" --recomp-dir recomp \
  --receipt build/recomp/query-fusion.generated.json
```

Inputs must already contain the retained generated units 013/016/028 and their
current hooks. Generation verifies the complete owned-image SHA, exact five
query and two caller bodies, closure targets, profiler inventory, context sinks,
and absence of live local flag-cache state. The production continuation count
is fixed at 32. Optimized Python is rejected. The receipt contains hashes and
the transformation contract, not guest bytes.

Generation is repeatable: it recognizes and validates its own exact previous
clone/callsite, reconstructs the generic caller, and regenerates the same
outputs. Unexpected partial or modified wrappers fail before publication.
Outputs are replaced only when content changes; the generation receipt is
published last. A missing generated source or receipt triggers regeneration.
Parallel builds of the two affected objects wait for this generation boundary.

Only `code_028.o` and `query_fusion.o` receive the new compile definition. A
tracked feature stamp rebuilds the caller and updates archive membership on
both OFF/ON transitions; an OFF build needs no image generation and omits the
query object. Changing generator inputs revalidates the owned outputs. Existing
compiler options are retained: O2, `-fno-strict-aliasing`, Thumb/Cortex-A9/NEON,
without adding fast-math, new contraction behavior or recursive-shell flags to
the fused function. A generated query object uses normal native-object `-MMD`
dependencies. No new flag is applied to either generic query object.

For a retained stage, copy the updated Makefile/runtime adapter and both
generator tools (`gen_native_query_fusion.py`, `prototype_collision_query.py`),
plus `tools/tests/collision_query_fusion.c`, which the generation receipt hashes.
Keep the retained headers, recompiler/profile modules, original generated units
and generic objects in place. The generator validates their compatibility;
there is no wholesale guest regeneration step. After generation/build, verify
the parent `code_013.o`/`code_016.o` hashes are unchanged and inspect the new
query/caller stack-use output when building with `-fstack-usage`.

## Evidence and limits

The imported tool/fixture net change comes from qualified agent commit
`15c32cbe878a5e4a238f037849ed3bf8b5e533a3`. Private evidence is under
`direct-cluster-query/native-query-interface-combined`: 79 observation cases,
84 hooked suffix-caller cases, 84 full-wrapper cases, all 12 query preempt
sites, forced overflow, typed-OFF, sanitizer cases, and actual retained-object
whole-query comparisons. This wiring reuses that oracle. New tests cover the
actual production generator/build graph rather than repeating those suites:

* `tools/test_query_fusion_generation.py`: exact identity with qualified
  separate-unit/caller outputs; repeated generation; failed image/scope/clone
  validation without output changes; Python-O refusal; ARM ON/OFF compilation,
  caller OFF machine-code identity, empty OFF query unit and prerequisite errors.
* `tools/test_query_fusion_build.py`: real Makefile/compiler/archive/dependency
  recipes on small C inputs; OFF/ON/OFF and no-op builds; missing-output and
  changed-generator recovery; unchanged generic object hashes/mtimes; strict
  booleans and compiled prerequisites. Only its owned-image generator is a
  small graph fixture; the first test exercises the real generator.

The qualified separate typed unit uses a 2152-byte frame plus a 24-byte
adapter; actual CE guest threads/workers request 512 KiB native SCE stacks.
This wiring adds no stack-headroom admission and claims no universal recursion
bound. Overflow retains the fused frame while calling a generic child. Full
actor/physics callbacks, firmware copy implementation cost, cache effects and
physical FPS remain outside the bounded oracle. The earlier instruction
reductions are not a hardware gain claim. Root owns final package review,
fresh-launch validation and any optional passive startup identification.

## Cumulative package integration

The full Vita package built from `47c7e7c` retains all seven preceding startup
selections and adds `XV_NATIVE_QUERY_FUSION=1`. The only changed existing object
is `code_028.o`; `query_fusion.o` is the only added object. Both generic query
source files and objects remain byte-identical to the installed seven-path
parent. The new unit's complete `.text` also matches the qualified separate
unit byte-for-byte. Actual stack records remain 2152 bytes plus the 24-byte
adapter; the specialized enclosing caller uses 96 bytes and generic 171F10
remains 80 bytes. The caller object contains the expected single adapter-call
relocation and literal caller identity.

Package verification finds only the runtime and boot record changed; all 1588
members and the installed launcher/assets contract are otherwise preserved.
Linked symbols retain both the generic entries and selected fused adapter, and
typed traversal's linked startup mode remains one. The candidate runtime is
`98393693e68be0032c5381cc172611a4db1361cf16d7dfeb7917136b9041fa99`.
These checks establish the compiled composition, not actual call counts or
physical performance. No getter was added to the qualified query unit; the
verified runtime hash identifies this startup-only selection.

Private build, object/code identity, caller relocation and package receipts
are under `direct-cluster-query/query-fusion-startup/`. Hardware acceptance is
separate from these completed integration checks.
