# Halo CE performance work without Vita hardware — September 18

This pass adds a tested, opt-in sampler-cache experiment and fixes live texture
options being omitted from cache identity. It does **not** establish a Vita FPS
gain. Existing tester packages and device files were not changed.

## Findings that guide the work

The [controlled September 7 Blood Gulch comparison](hardware-20260907-cpu-comparison.md)
found no FPS gain from its CPU optimization bundle. That recorded view spent
substantial time waiting for graphics completion, so small CPU savings should
not be presented as a solution to the overall performance problem.

The same archived `xita.log` reports 18,060 sampler preparations in a 60-frame
window with unused texture stages skipped: 301 preparations per frame, versus
21,600 cache hits. The old cache remembers only the previous material per stage;
alternating materials repeatedly evict each other. These totals motivate the
experiment but do not tell us how many would fit a larger cache.

### Distant texture noise

The stronger code-level lead is `XV_BC_MIPS=0`: the default square compressed
texture path uploads only its selected top level. "Mip smoothing: Auto" cannot
sample levels that were never uploaded. This is a plausible contributor to
shimmer, not a confirmed diagnosis of the user's Blood Gulch view.

This default was retained after a suspected GPU fault in September 2's mipmapped
BC upload. It remains unchanged pending a controlled graphics comparison.

The earlier hypothesis that replacing `MIPMAP_LINEAR` with `LINEAR` necessarily
disables mipmaps was not established. [vitaGL's texture-state mapping](https://github.com/Rinnegatamante/vitaGL/blob/master/source/textures.c)
uses ordinary `LINEAR` plus mip-count and mip-filter controls for mipmapped
sampling. [Vita3K's Vulkan sampler implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/vulkan/texture.cpp)
also separates within-level filtering from between-level interpolation. No
speculative filter-enum change was made.

The new BC regression runs the real uploader on a synthetic 256x256 DXT1 chain:

| Configuration | Uploaded levels | Source bytes consumed |
| --- | ---: | ---: |
| Default | 1 | 32,768 |
| `XV_BC_MIPS=1` | 7, down to 4x4 | 43,688 |

It verifies each uploaded level's contents and invalidation range. Mutations to
unused 2x2/1x1 tails do not invalidate the upload; level 1 matters only when
uploaded. This checks CPU layout and cache accounting, not GPU stability.
Lightmap mip generation, texture size and compressed-texture defaults are unchanged.

## Changes

- Prepared samplers include the current effective filtering/mip-smoothing options
  in their identity. Live settings changes now rebuild the sampler even when the
  texture and game state remain identical. Toggling back can reuse an exact match.
- `XV_SAMPLER_CACHE_WAYS=4` enables four entries per stage, with the previous hit
  checked first and bounded round-robin replacement. Default remains one entry.
  `XV_SAMPLER_CACHE=0` still disables reuse. Capacity is fixed at startup.
- All paths resolve the live texture before consulting prepared sampler entries.
  Changed pixels, palettes, missing resources and render-target aliases retain
  the texture owner's validation. Cached descriptors are copies, with no added
  allocation, pixel upload or GPU-resource ownership.
- Added a repeatable synthetic host replay and benchmark. It compares descriptor
  output and checksums and measures setup counts separately from host time.

## Measured scope and tradeoffs

Seven alternating-order runs, one million binds per case, optimized host C with
GPU API calls represented by descriptor-writing stubs:

| Alternating materials | One-entry median ns/bind | Four-entry median ns/bind | Host time change |
| --- | ---: | ---: | ---: |
| 1 | 8.45 | 9.08 | 7.5% slower |
| 2 | 12.04 | 8.83 | 26.7% faster |
| 4 | 12.03 | 8.79 | 26.9% faster |
| 8 | 11.96 | 13.45 | 12.5% slower |
| 16 | 11.97 | 13.44 | 12.3% slower |

For four alternating materials, preparation count drops from 1,000,000 to 4,
while all 1,000,000 live texture resolutions remain. Workloads beyond capacity
still miss, with added lookup cost. This is why the expansion is **opt-in**.
These timings exclude actual driver/GPU work and are not gameplay or Vita FPS.

## Verification and reproduction

From the repository root:

```sh
make -C recomp/host test-textures test-cpu-prep test-texture-preparation test-sampler-replay
python3 tools/benchmark_samplers.py --runs 7 --json /tmp/xita-sampler-benchmark.json
make RECOMP=1 BUILD=local/ce-performance-20260918/build \
  local/ce-performance-20260918/build/runtime/xv_d3d.o \
  local/ce-performance-20260918/build/runtime/xv_ui_gxm.o
```

Host checks cover live options, descriptor identity, immutable captured copies,
aliases, missing resources, capacity overflow, mip contents and dirty ranges.
The existing differential texture-preparation tests pass for all 54,976 recordings
in each tested mode. The draw-texture sanitizer test passes outside the sandbox;
inside, LeakSanitizer cannot run under process tracing. Both changed runtime
translation units compile with the Vita SDK. Existing warnings remain.

Private logs, baseline copies and benchmark JSON are in
`local/ce-performance-20260918/`. No VPK was rebuilt or deployed in this pass.

## Next evidence to collect

1. Replay representative captured sampler identities, or measure one/four-entry
   cache modes in the same gameplay view. Do not enable the expansion by default
   based on the favorable two/four-material cases alone.
2. Compare compressed mipmaps off/on in an isolated emulator setup, including
   distant ground, grazing-angle walls, lightmaps and UI. Emulator success does
   not resolve the historical Vita GPU fault.
3. Once hardware is available, measure matched uncapped frame times and GPU
   stability with each change independently. Until then, keep the tester build
   and graphics defaults stable and continue profiling expensive rendering passes.
