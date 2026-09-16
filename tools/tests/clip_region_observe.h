#pragma once
#include "xv_x86rt.h"
#include <stdlib.h>
extern volatile uint32_t xv_cur_fn;
extern int xv_watch_n;

void original_wrapper(xctx*),current_wrapper(xctx*),fused_wrapper(xctx*);
void f_0001D130(xctx*);
void xv_watch_leave(uint32_t,uint32_t,xctx*);
int xv_object_math_lock(void);
void xv_object_math_unlock(int*);
