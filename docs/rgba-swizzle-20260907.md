# Decoded RGBA swizzling — September 7, 2026

An isolated candidate now uploads supported power-of-two decoded textures using
GXM's native swizzled layout. Host correctness and private Vita3K rendering checks
pass. **Physical Vita performance is unmeasured.** The installed vertex-comparison
executable and the user's standard configuration have not been changed.

## Implementation

`xv_ui_gxm.c` previously uploaded ordinary decoded RGBA map/UI textures using
`sceGxmTextureInitLinear`, even when their dimensions support native swizzling.
Native compressed textures and cube maps already have separate swizzled paths.
The new option `XV_RGBA_SWIZZLED=1` enables the decoded path; its default is zero
until hardware validation. Render-target aliases return through their existing
path, and non-power-of-two decoded textures remain linear.

The conversion preserves the selected source mip, RGBA texels, UI coverage
conversion, existing box-filtered mip chain, sampler policy and opacity proof.
It does not generate mips for single-level lightmaps. The CPU decoder and mip
builder use a temporary cached buffer for each new swizzled upload; after the
worker joins, `xv_rgba_layout.h` writes texels sequentially into the mapped
uncached GPU pool and the existing GPU publication barrier runs. Unchanged
texture binds allocate and convert nothing. Scratch allocation failure falls
back to a cached linear version, without retrying allocation at every draw.

The layout uses Y in even interleaved bits, X in odd bits and the longer axis
above the common square. Unlike linear rows, swizzled RGBA mip rows have no
8-texel padding. This was checked against Vita3K's primary
[format decoder](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/texture/format.cpp)
and [texture-cache layout](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/texture/cache.cpp).
The implementation is independent; the host reference maps coordinates to
addresses, while production visits output addresses in sequence.

Each cache entry records its requested decoded layout. The benchmark can retain
both linear and swizzled variants; switching layouts never overwrites an upload
referenced by an earlier recorded frame. Source writes invalidate both variants,
and memory is reclaimed only through the existing drained texture-pool purge.
Normal rendering introduces no new GPU waits. Existing single-flight recovery,
three protected storage slots, vertex comparison and shaders are unchanged.

## Validation

`make -C recomp/host test-rgba-layout` exercises the actual upload/cache code,
not just the standalone converter. Normal and ASan/UBSan runs pass:

- 164 upload cases compare every texel at every mip, across square, rectangular,
  tiny and one-dimensional textures, with coverage and varying alpha.
- Additional full layout checks cover both 4096-by-1 orientations, both
  4096-by-8 orientations, 1024-by-512 and the complete 4096-square mip chain.
- Layout reuse, source changes, retained GPU bytes, opacity, palette updates,
  scratch/pool allocation failures, BC/cube and render-target exclusions pass.
- Production frame-acquisition checks verify that the phase hook runs after
  draining and leaves queue policy and both vertex overrides unchanged. The
  benchmark controller's completion/cancel/lost-view restoration checks pass.

The native SELF builds successfully. Private Vita3K checks use the user's saved
544p/256-texture settings and 20 FPS cap, with the new layout option enabled.
They cover the menu, Blood Gulch, firing, flashlight input, campaign loading,
skipping into the cryo bay, and turning the camera. The pre-skip screenshot
records the loading transition, not a full opening-cutscene validation.

| Same-view comparison | Linear before | Swizzled | Linear after |
| --- | ---: | ---: | ---: |
| Blood Gulch | 19.967 FPS | 19.961 FPS | 19.988 FPS |
| Campaign cryo bay | 13.703 FPS | 13.628 FPS | 13.546 FPS |

Each phase excludes 60 settling frames and measures 120 frames. Camera checks
pass. Blood Gulch reaches the cap; the campaign result is effectively neutral
against its before/after drift. There are no additional decodes in the campaign
measurement windows and no texture-pool purge during either comparison. These
software-rendered emulator timings do not measure SGX543 texture-cache behavior.

Static screenshot comparisons find zero changed pixels across all three layouts
in 27,200 mountain pixels, 17,100 ground pixels, 36,100 campaign wall pixels and
31,350 campaign floor pixels. Other sampled regions contain animated HUD, NPC
or display effects and differ even between the two linear captures; the full
frames are not asserted identical. Host tests establish exact uploaded samples.

Requested geometry ownership checks cover 262 and 513 draws, with zero changed
bytes before completion. The run logs no fence errors, upload failures, draw
storage drops, texture initialization failures, scratch allocation failures or
ordinary Finish calls. Maximum queue depth stays one; vertex upload high water
is 1,581/8,192 KiB and maximum recorded GXM draws/frame is 672. This is private
validation, not evidence that the previous hardware driving crash is fixed.

## Candidate and next measurement

Staging: `/tmp/xita-rgba-swizzle-build`.
Archive: `/home/birchwoodgod/xita-backups/2026-09-07-210443-rgba-swizzle/`.
Native SELF: 34,796,990 bytes, SHA-256
`d189f1a9a8a96743009b4d81c2d160590576733d28d8be67c1d6926607c6d0a1`.
Source manifest, patch, changed sources, binary, tests, screenshots, pixel
comparisons and raw logs are retained. The private emulator is stopped and its
prior executable/configuration restored. No Vita storage was accessed this turn.

This candidate derives from the preserved installed scalar/NEON comparison
source. Its L+R+Square action instead runs **linear/swizzled/linear** at fixed
resolution and restores the configuration afterward. The deferred-flare and
index-range candidates remain separate. Root's rendering source contains the
opt-in implementation, while root's existing flare comparison hook is preserved.

First collect the installed vertex-comparison hardware result when the user
returns. Then choose the next isolated hardware comparison from this layout
candidate and the deferred-flare candidate, retaining standard graphics settings.
The user's previous benchmark involved **no Warthog driving**. Physical 20 FPS
and driving stability remain unverified; neither is established by this change.
