#pragma once
#include "../xv_x86rt.h"
/* Idle query requires independently verified guest-owner identity. */
int xv_object_jobs_native_idle(void);
int xv_constant_pack_prefix(xctx *c);
void xv_constant_pack_stats(unsigned out[7]);
