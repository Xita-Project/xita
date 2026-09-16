#pragma once
#include "quad_draw.h"
#include "screen_draw.h"
const h2_quad_contract *h2_quad_gxm_contract(void);
const uint32_t *h2_quad_gxm_render(void *opaque, const h2_quad_request *request);
const h2_screen_contract *h2_screen_gxm_contract(void);
const uint32_t *h2_screen_gxm_render(void *opaque, const h2_screen_request *request);
#include "bc1_draw.h"
const h2_bc1_contract *h2_bc1_gxm_contract(void);
const uint32_t *h2_bc1_gxm_render(void *opaque, const h2_bc1_request *request);

#include "composition_draw.h"
const h2_composition_contract *h2_composition_gxm_contract(void);
const uint32_t *h2_composition_gxm_render(void *opaque, const h2_composition_request *request);

#include "threshold_draw.h"
const h2_threshold_contract *h2_threshold_gxm_contract(void);
const uint32_t *h2_threshold_gxm_render(void *opaque, const h2_threshold_request *request);

#include "blur_draw.h"
const h2_blur_contract *h2_blur_gxm_contract(void);
const uint32_t *h2_blur_gxm_render(void *opaque, const h2_blur_request *request);

#include "blend_draw.h"
const h2_blend_contract *h2_blend_gxm_contract(void);
const uint32_t *h2_blend_gxm_render(void *opaque, const h2_blend_request *request);

#include "luma_draw.h"
const h2_luma_contract *h2_luma_gxm_contract(void);
const uint32_t *h2_luma_gxm_render(void *opaque, const h2_luma_request *request);

#include "sprite_draw.h"
const h2_sprite_contract *h2_sprite_gxm_contract(void);
const uint32_t *h2_sprite_gxm_render(void *opaque,const h2_sprite_request *request);

/* Shared GXM state (quad_gxm.c) for sibling backends. kind: 0 mapped RW, 1 vertex USSE, 2 fragment USSE. */
struct SceGxmContext; struct SceGxmShaderPatcher;
int h2_gxm_ensure(void);
struct SceGxmContext *h2_gxm_context(void);
struct SceGxmShaderPatcher *h2_gxm_patcher(void);
void *h2_gxm_alloc(unsigned size, int kind, unsigned *offset);
