/* Optional exact common-UV reuse in selected CE model walks. */
#pragma once
#include "../xv_x86rt.h"
unsigned xk_model_uv_scope_begin(xctx *,const uint8_t *,const uint32_t *,const uint8_t *);
void xk_model_uv_scope_end(xctx *,unsigned);
int xk_model_uv_begin(xctx *,const uint8_t *,const uint32_t *,const uint8_t *,unsigned,unsigned *);
void xk_model_uv_end(xctx *,unsigned);
void xk_model_uv_report(unsigned);

#ifndef XV_MODEL_UV_CROSS_MODEL
#define XV_MODEL_UV_CROSS_MODEL 0
#endif
#if XV_MODEL_UV_CROSS_MODEL != 0 && XV_MODEL_UV_CROSS_MODEL != 1
#error XV_MODEL_UV_CROSS_MODEL must be 0 or 1
#endif
#if XV_MODEL_UV_CROSS_MODEL
#if !defined(XV_MODEL_UV) || !XV_MODEL_UV || !defined(XV_OWNER_PHASE)
#error XV_MODEL_UV_CROSS_MODEL requires XV_MODEL_UV and XV_OWNER_PHASE
#endif
/* Called at accepted outer SCENE and Present boundaries, never from a worker. */
void xk_model_uv_owner_boundary(void *);
#endif
