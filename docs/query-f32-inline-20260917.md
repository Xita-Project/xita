# Query-only f32 primitive inlining

`XV_QUERY_F32_INLINE=1` selects the qualified copies of `x87_load_f32` and
`x87_store_f32` in the separate collision-query unit. The repository default is
**0**. It requires `XV_NATIVE_QUERY_FUSION=1` and inherits that feature's exact
Halo CE caller admission and compiled prerequisites. Solver fusion and all
other cumulative settings remain independently selected as before.

The only primitive changes are two `always_inline` attributes. The double/x87
precision model, float loads/stores, global guest-memory mapping, floating-point
options, original context publication and generic fallback remain unchanged.
No runtime selector or shared compiler definition is introduced.

## Generation and incremental builds

Use the retained build command with `XV_QUERY_F32_INLINE=1`. The existing
`query-fusion-generate` target invokes the same owned-image generator, adding
`--query-f32-inline 1`. It composes query, caller and optional solver generation
before publication, and publishes the generation receipt last.

The generator writes an ignored `recomp/query_f32_primitives/` directory with
exactly eight canonical headers:

* `xv_recomp_protos.h`, `xv_x86rt.h`, `xv_phase.h`
* `kernel/xk_object_jobs.h`, `kernel/xk_light_census.h`
* `kernel/xk_collision_vertices.h`, `kernel/xk_segment_sphere.h`,
  `kernel/xk_collision_traversal.h`

Seven headers are byte-identical copies. Only the two attributes differ in
`xv_x86rt.h`. Four includes in `query_fusion.c` select this private tree before
its captured-root macros are defined. All transitive x87 primitive includes
therefore retain one `#pragma once` file identity, and their memory operations
continue to bind the original global `g_xram`/`g_xpt` expressions.

The header inventory is explicit. Generation never scans or recursively copies
its output directory. It rejects unexpected, escaping or macro-based includes,
missing canonical inputs, changed helper declarations and changed global-root
expressions. Any expanded include closure requires a reviewed inventory update
in both the small transform module and Makefile. The tests compare those lists.

Make tracks all eight canonical inputs and every corresponding missing output.
A changed canonical header revalidates and refreshes its private copy. Missing
private headers force regeneration. A content-preserving write retains the
source timestamp. Repeating an unchanged build performs no compilation.

A dedicated `query-f32.config` stamp handles OFF/ON transitions. OFF restores
the exact original query source and machine code. Old ignored private headers
may remain on disk but are not included by that source. The current shared
query-generation stamp also recompiles the caller and enabled solver when it
changes; their bytes remain identical. Generic units are not affected by the
selector. A genuine shared-header or profiling change retains its existing
broader dependencies.

When updating a retained private build stage, copy the new Makefile,
`tools/gen_native_query_fusion.py` and `tools/query_f32_primitives.py` together.
The helper module is a generator dependency even while this option is OFF.
No new guest regeneration or original shared-header edit is needed.

## Validation and practical limits

The [prototype qualification](query-inline-primitives-20260917.md) found about
2.5–3.4% fewer whole-query ARM instructions on busy synthetic queries, with
three or six extra instructions on two trivial cases. The query grows by 220
bytes, and its local native frame shrinks from 2,152 to 2,136 bytes. This does
not establish a hardware frame-rate gain.

Production integration checks the exact qualified query source and initialized
object sections, restored OFF object, retained caller/solver/generic identities,
production stack report and preprocessed global memory binding. Its separate
build tests cover all eight missing outputs, canonical changes, profiling
transitions, no-op builds, prerequisite errors and strict boolean selectors.
The integration reuses the qualified semantic oracle when object identity
matches; it does not repeat unrelated gameplay tests.

Tests:

```sh
python tools/test_query_f32_headers.py
python tools/test_solver_fusion_build.py --output-dir "$PRIVATE_OUTPUT/graph"
python tools/test_query_fusion_build.py --output-dir "$PRIVATE_OUTPUT/query-graph"
python tools/test_query_f32_integration.py \
  --retained-build "$RETAINED_BUILD" --build-command "$BUILD_COMMAND_JSON" \
  --qualified-dir "$QUALIFIED_COST" --xbe "$XBE" --manifest "$MANIFEST" \
  --out "$PRIVATE_OUTPUT/production"
```

The last command needs the owned inputs, Vita compiler and existing Python
recompiler/ELF dependencies. It copies the retained stage to a new private
location and never accesses a device. No owned source or binary is committed.
The independent prototype review counted 160 normal-object and 117 forced
capacity-one observations; these are not a new claim of all 12 yield-site
coverage or a repeated whole `172BF0` caller test.
