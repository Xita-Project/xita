#pragma once
#include "xk.h"
/* Captured arithmetic only; caller must resume before counters/publication. */
int xv_object_hierarchy_suspend(xctx *,int guard,uint32_t output,unsigned bytes);
void xv_object_hierarchy_resume(int token);
