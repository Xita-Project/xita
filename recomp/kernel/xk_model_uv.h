/* Optional exact common-UV reuse inside one selected CE model walk. */
#pragma once
#include "../xv_x86rt.h"
unsigned xk_model_uv_scope_begin(xctx *,const uint8_t *,const uint32_t *,const uint8_t *);
void xk_model_uv_scope_end(xctx *,unsigned);
int xk_model_uv_begin(xctx *,const uint8_t *,const uint32_t *,const uint8_t *,unsigned,unsigned *);
void xk_model_uv_end(xctx *,unsigned);
void xk_model_uv_report(unsigned);
