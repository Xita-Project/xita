# Retained render preparation startup options — September 17

Two existing rendering options can now be selected for a fresh process without
editing its environment file. Both repository defaults remain **0**:

```
XV_TEXTURE_STATE_CACHE_DEFAULT=1 XV_DEPTH_PREPARE_DEFAULT=1
```

Append these Make variables to the current cumulative build command, retaining
all its other flags. They do not introduce a new cache, shader heuristic, quality
setting or benchmark. The existing paths are documented in
[texture-state-cache-20260914.md](texture-state-cache-20260914.md) and
[depth-preparation-20260915.md](depth-preparation-20260915.md).

An explicit `XV_TEXTURE_STATE_CACHE` or `XV_DEPTH_PREPARE` environment value wins
over its corresponding build default. Existing `atoi` semantics are unchanged:
`0`, empty and nonnumeric values disable; nonzero values, including negative
values, enable. The ordinary `env.txt` then `xita.cfg` handoff remains unchanged.
Runtime comparison override `-1` restores the configured value. This integration
runs no comparison and introduces no live control.

Depth preparation still requires no development shader override and enabled
alpha specialization/depth-only shader selection. It starts with no proofs;
only an original successful replay can publish an eligible linked program.
Already recorded commands retain their captured choice even if an existing
control later changes. Descriptor reuse retains full-descriptor equality,
resolver/feedback checks, error invalidation and clear/UI/render-target range
boundaries. No query, depth-store, packed vertex, GPU completion or resource
ownership rule changes.

After dashboard configuration and before either game/pump worker starts, one
startup call initializes and reports the effective policies:

```
[render-preparation] process-start texture-cache 1 depth-prepare 1 available 1; ...
```

This proves mode selection, not actual cache hits or prepared draw counts. The
existing `[depth-prepare]` periodic report distinguishes work removal. Texture
binding counters remain part of optional render profiling; no new per-draw
logging or clock read was added.

## Build and focused validation

Each default is checked as boolean by Make and C. Only `runtime/xv_d3d.o`
receives either `-D` flag and depends on its content-preserving startup stamp:
`build/texture-state-startup.config` and `build/depth-prepare-startup.config`.
Changing either default rebuilds D3D and the normal final linked output, without
recompiling generated guest code, its archive, main, shaders, UI or uploaders.
The initial source/header integration still rebuilds ordinary header consumers.

`tools/test_render_preparation_startup.py --output-dir <new-directory>` runs:

- 90 fresh ASan/UBSan processes using the production configuration, recording,
  replay, resolver and descriptor-cache bodies. Macro absent/0/1 combinations,
  explicit environment choices, incompatible shader settings and override
  restoration are checked independently. Cold proof misses, published proof
  admission, exact command retention with packed streams/query/depth/stencil
  fields, texture reuse across a zero-sampler draw, and active feedback rejection
  are exercised with host GXM stubs.
- Eight builds through the real Makefile and real host compiler/archive tool,
  with small fixture C sources replacing expensive assets. Default transitions
  `00→10→11→01→00`, repeated modes and invalid values verify dependency ownership.

The existing production descriptor fixture passes 120,000 binding comparisons,
including all 128 descriptor bits, failures and invalidation. Existing resolver,
depth recording/publication/replay, RTT/UI/clear ordering and focused depth-store
startup/query-notification tests pass. Two previously stale fixture boundaries
were repaired: texture configuration extraction no longer captures the unrelated
fragment-uniform function, and the RTT stub declares the notification type and
const EndScene parameters used by the current production replay.

The combined fixture's GXM descriptor accessors and shader linking are mocked;
its explicit cache reset does not simulate GPU execution. The retained RTT
fixture independently exercises real scene/range ordering with mocked draws;
the depth-store fixture exercises the actual query fence on an admitted
continuation. These checks are not a single complete GPU-rendering oracle.
Actual ARM compilation of main and D3D with both defaults 0/1 and current
packed/query-prefix/depth-store/async interfaces also passes. No owned generated
shader or guest bytes are included in this change.

## Trial limits

Earlier live comparisons were mixed for texture reuse and inconclusive for depth
preparation. Neither option was excluded by a recorded correctness failure.
Those results do not establish a gain in the current cumulative native-standard
build, and their timing deltas must not be added. This change prepares a bounded
fresh-launch cumulative gameplay trial: campaign combat/occlusion/flare visuals,
menus and Blood Gulch driving still require physical review. No hardware,
emulator, performance benchmark or deployment was run for this integration.
