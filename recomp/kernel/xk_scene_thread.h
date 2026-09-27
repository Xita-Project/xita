/* Increment B (handoff 20260920 §32): the scene half (BCB30) runs on a helper Vita thread with a copy of
 * the guest context while the owner waits. The helper acts as the owner's guest fiber (same guest
 * stack, xk_cur, fiber semaphore), and host-side owner checks see the owner's id (xv_owner_thread_id). */
#pragma once
#include <stdint.h>
int  xv_scene_thread_run(void *context);    /* generated BCB30 entry hook: 1 = body ran on the helper, return */
void xv_scene_thread_report(unsigned frames);

int xv_scene_thread_owns_context(const void *context); /* active helper copy only */
uint32_t xv_scene_thread_context_generation(const void *context);

int xv_scene_thread_stack_bounds(const void *context,uint32_t *low,uint32_t *high);
