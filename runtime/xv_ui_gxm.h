/*
 * xv_ui_gxm.h - hardware (GXM) renderer for the recompiled engine's immediate-mode UI path.
 *
 * The Stage-4 recompiled engine (recomp/kernel/xd3d.c) submits 2D screen-space quads through the
 * xd3d_r_* hooks running Halo's OWN vertex-shader program (declaration 0x1E13EC -> halo_vs_03.gxp).
 * This module overrides those hooks with real sceGxm draws:
 *
 *   - vertices are streamed zero-copy into a GPU-mapped ring (unified memory: the CPU writes, the
 *     GPU reads the same physical pages - no deep copy, no per-frame framebuffer duplication);
 *   - the already-compiled UI vertex program runs on the GPU with c[] bound straight from
 *     xd3d_state.vsc, so the earlier decode/translate/compile stages do the transform, not the CPU;
 *   - the combiner tint is one fragment uniform write per batch;
 *   - texture control words are built once and cached, and only re-bound when the state hash changes.
 *
 * Threading: the game fiber RECORDS batches (xv_ui_gxm_*), the render pump REPLAYS them inside its
 * GXM scene (xv_ui_gxm_replay).  GXM scenes are single-threaded, so recording and replay never touch
 * the context concurrently. Three recording slots are retired by GPU fragment
 * notifications; replay receives an explicit slot captured at publication.
 */
#pragma once

#include <stdint.h>
#include <psp2/gxm.h>
#include "recomp/kernel/xd3d.h"          /* xd3d_im_vtx, xd3d_state */

/* One-time setup: loads the UI vertex/fragment programs and the clear program, and allocates the
 * GPU-mapped vertex/index rings.  Call after xv_shader_init().  Returns 0 on success. */
int  xv_ui_gxm_init(void);
void xv_ui_gxm_shutdown(void);
int  xv_ui_gxm_ready(void);
#ifdef XV_DEPTH_STORE
/* Pump-only proof for replay_overlay/settings, excluding ordinary UI batches. */
int xv_ui_gxm_depth_tail_readonly(unsigned frame);
#endif
/* Apply explicit pre-launch sampler overrides to a copied descriptor. Defaults
 * preserve the game's filters and only existing mip chains can be sampled. */
void xv_ui_gxm_apply_texture_options(SceGxmTexture *texture);
/* Benchmark phase boundary only, after draining: -1 restores configuration. */
void xv_ui_gxm_rgba_layout_override(int enabled);
/* Recording-thread proof: pins an opaque decoded upload against in-place writes. */
int xv_ui_gxm_texture_opaque(const SceGxmTexture *texture);

/* --- recording (game fiber) --------------------------------------------------------------------- */
void xv_ui_gxm_clear(uint32_t argb);                       /* xd3d_r_clear: full-screen tint */
/* One immediate-mode QUADLIST batch: `n` vertices (multiple of 4), `tex_hdr` = guest
 * X_D3DPixelContainer, `tint` = combiner colour (NULL -> white). */
void xv_ui_gxm_quads(const xd3d_im_vtx *v, unsigned n, uint32_t tex_hdr, const float tint[4], unsigned prog, unsigned stage);
void xv_ui_gxm_frame_flip(void);                           /* xd3d_r_present: publish + swap buffers */
/* Native-resolution settings, inside the final scene and covered by its fence. */
void xv_ui_gxm_replay_settings(SceGxmContext *ctx, unsigned frame);

/* --- replay (render pump, inside sceGxmBeginScene/EndScene) -------------------------------------- */
void xv_ui_gxm_replay(SceGxmContext *ctx, unsigned width, unsigned height);

void xv_ui_gxm_frame_begin(void);
unsigned xv_ui_gxm_published_frame(void);
void xv_ui_gxm_replay_frame(SceGxmContext *, unsigned, unsigned, unsigned);
