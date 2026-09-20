#pragma once
#include "xk.h"
/* Captured arithmetic only; caller must resume before counters/publication. */
int xv_object_hierarchy_suspend(xctx *,int guard,uint32_t output,unsigned bytes);
void xv_object_hierarchy_resume(int token);
void xv_object_hierarchy_report(unsigned frames);

/* Caller keeps the outer guard through join; callback touches owned data only. */
int xv_object_hierarchy_offer(xctx *,int guard,void (*run)(void *),void *argument);
void xv_object_hierarchy_join(int token);
void xv_object_hierarchy_assist_report(unsigned frames);
