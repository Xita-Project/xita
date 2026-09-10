# Wii Blam reference review

Reviewed September 9, 2026, from the user-supplied
`BLAM_WII_HALOCEA_XBOX360_09092026.zip` (6,381,311 bytes).
Archive SHA-256: `58ef78135c7c521a9b3d6ec32735662e32103b0830c5e0fac4be1f0bd76ca40b`.

This is useful engine-reference material. It contains recovered gameplay C,
Wii-specific host adapters, model/texture packers and explicit rendering policies.
It reads big-endian Halo caches and references Xbox 360-era addresses, unlike
Xita's original Xbox 3925 little-endian guest state. Matching names do not prove
matching offsets, ABI or behavior.

## Useful candidates for Xita

| Area | Observed implementation | Next Xita investigation |
| --- | --- | --- |
| Existing model LODs | `include/host_model_lod_policy.h` scales projected size to 75% for scenery and 80% for bipeds. `src/host_cpu_skinning.c:973` selects authored meshes, with a fallback for missing LODs at line 1328. These are selection thresholds, not polygon-reduction percentages. | Identify the original Xbox LOD selection and measure actual mesh changes. Keep near objects, weapons and vehicles correct. A resolution setting alone does not prove lower-detail meshes are selected. |
| Static visibility and material reuse | `src/host_bsp_batch_visibility.c:309` builds cluster/batch metadata. `src/host_render_scene.c:6652` rejects invisible batches conservatively and caches per-frame frustum results. Unknown/incomplete metadata retains drawing. The scene also retains parsed environment materials. | Find repeated visibility/material work in Xita and establish safe reuse boundaries. Preserve animated state, transparent ordering and fallback rendering to avoid renewed disappearing models. |
| Offline geometry preparation | `tools/wii_model_pack/wii_model_pack.py` builds a deterministic native archive; `include/host_renderer.h:28` describes prevalidated strip segments and compact vertex layouts. | Consider a Vita-specific cache for immutable geometry and validated indices. Map/version/settings fingerprints and draw ownership must remain correct. This could remove repeated parsing without changing geometry. |
| Offline texture preparation | `tools/wii_texture_pack/wii_texture_pack.py` emits native GX formats and mip records. | Evaluate Vita-native preprocessing for loading and first-use stalls. Wii GX bytes are not a GXM texture layout, and existing Xita caches mean steady-state FPS benefit must be measured. |
| Fog/sky diagnosis | `src/host_render_fog.c:735` builds atmospheric and planar fog from cluster, sky, camera and map data, with explicit finite/range checks. | Use it to identify which original-game values to inspect for the tester's black/gray a30 sky. It is not evidence that the same bug or fix applies to Xita. |

The first [model LOD and material preparation implementation](model-preparation-20260909.md)
now has a local native candidate and host/emulator checks. Physical gains are
pending; it is not in the published September 9 gameplay VPK.

Prioritize the confirmed [shader-capture flood](tester-issue-3-20260909.md), then
model LOD/state reuse based on a fixed hardware scene. The archive supports the
idea of doing less repeated engine work; it does not provide a measured Vita FPS
improvement or a ready-made multicore scheduler.

## Limits of this archive

The main build configuration and renderer backend implementation are absent.
Referenced dependencies such as `host_model_archive.h`, `host_texture_archive.h`
and Wii platform headers are missing. The only CMake file builds an audio helper.
Wii skinning headers declare Gekko paired-single routines; the implementation
needed to assess their speed is absent. A renderer comment and profiling API
refer to end-frame `GX_DrawDone`; do not infer that this port eliminates GPU waits.

No README, license or redistribution terms are included. The archive was extracted
outside the Xita repository for read-only inspection; its code/tools were not run
or copied into Xita. Author, upstream revision, completeness and reuse permission
remain unverified pending a project link. Xita's existing GPL license does not
establish permission for this separate archive. Any implementation should be
validated against our own supported Xbox executable and Vita rendering behavior.
