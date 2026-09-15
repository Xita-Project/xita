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
