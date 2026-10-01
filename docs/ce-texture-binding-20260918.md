# Halo CE texture-binding reuse — September 18

The next offline performance candidate skips identical texture bindings during
mesh replay. It is separate from the [sampler-preparation cache](ce-performance-20260918.md):
that cache runs while recording draws; this one avoids repeated
`sceGxmSetFragmentTexture` calls on the render thread.

`XV_TEXTURE_BIND_CACHE=1` enables it. The default remains **off**, pending a
representative emulator/hardware comparison. No tester package or device files
were changed. This is a CPU/driver submission optimization, not reduced shader
work or an established FPS improvement.

## Behavior and boundaries

The cache holds copies of complete texture descriptors indexed by the actual
linked shader's GPU unit (0–15), rather than the original Xbox stage. Exact
identity includes resource address, filtering, mip, format and addressing bits.
Changing shader sampler assignments therefore cannot reuse the wrong unit.

Texture resolution, previous-frame substitution, cube-face conversion, fallback
selection and active render-target feedback rejection all run before cache reuse.
A failed driver bind invalidates that entry and rejects the draw; the next attempt
retries. Out-of-range texture units reject the draw before calling the driver.
The prior code ignored the driver's texture-bind return value.

The cache is local to each uninterrupted mesh replay range. UI and scene/target
transitions start a new range; clears invalidate its entries explicitly. There
is no global cache spanning contexts or frames, no sorting, and no change to
draw order, pixels, shader arithmetic or texture ownership.

With `XV_RENDER_PROFILE=1`, each existing reporting window now includes:

```text
[texture-bind] mesh FIRST..LAST: N submitted / M reused (mesh replay only)
```

Counts include attempted binds even when a driver call fails. They exclude UI
texture binds. Counters work with reuse disabled too, reset at the reporting
boundary, ignore calls outside a profiled frame, and add no per-texture timer.
Use existing submission/frame metrics alongside these counts; fewer calls are
not an equivalent percentage reduction in frame time.

## Local evidence

The production texture-resolution function is extracted and run under
AddressSanitizer and UndefinedBehaviorSanitizer. A 4,096-draw synthetic sequence
is submitted through both paths, maintaining independent simulated driver state.
Every resulting descriptor matches. Calls fall from **15,872 to 1,842** (88.4%
fewer). This repeated-material workload demonstrates reuse; it is not a
captured Blood Gulch frame or a hardware benchmark.

Cases include descriptor changes, unused stages, remapping to GPU unit 15,
multiple stages sharing one unit, previous-frame substitutions, cube faces,
fallbacks, simulated external state at range boundaries, feedback rejection on
would-be hits, failed binds/retry, and invalid units. Disabled-cache submission
counts are also checked.

The render-profile suite verifies the new counts, window resets and disabled/
missing-clock behavior. Existing draw-state, clear-boundary, index-copy,
constant-identity and shader-link-cache regressions pass. Both changed runtime
translation units compile with the Vita SDK; existing unrelated warnings remain.
No emulator visual comparison or Vita performance result is claimed.

## Reproduce

```sh
python3 tools/test_draw_textures.py
make -C recomp/host test-render-profile test-draw-prep
make RECOMP=1 BUILD=local/ce-texture-bind-20260918/build \
  local/ce-texture-bind-20260918/build/runtime/xv_d3d.o \
  local/ce-texture-bind-20260918/build/runtime/xv_render_profile.o
```

LeakSanitizer needs execution outside a process-tracing sandbox. Private baseline
copies, native objects and test logs are in `local/ce-texture-bind-20260918/`.

Next compare this switch alone with the earlier sampler-preparation experiment
left at its default. Exercise menu-to-game transitions, sky, terrain, cutouts,
weapons, active camouflage and render-target effects. Then measure matched
uncapped frame/submission times on Vita before considering a default change.
