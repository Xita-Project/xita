/* Optional exact replay of primary Halo CE 70110 fog arithmetic only. */
#pragma once
#include "../xv_x86rt.h"
int xk_model_fog_begin(xctx *, const uint8_t *, const uint32_t *, const uint8_t *, unsigned *);
void xk_model_fog_end(xctx *, unsigned);
/* Live presenting owner only; negative restores the process-start setting.
 * Every accepted configuration call invalidates previous and pending results. */
void xk_model_fog_override(xctx *, int);
void xk_model_fog_report(unsigned);
