/* Render view: a second guest page table whose mutable pages are per-frame shadow copies, so the scene
 * half reads a frozen world (handoff 20260920 §25-26). Increment A: single-threaded, the scene runs on
 * the owner bound to the render table and every shadow page is copied back afterwards. */
#pragma once
#include <stdint.h>
extern int xv_render_view_enabled;                 /* runtime switch (XV_RENDER_VIEW env, build default) */
void xv_render_view_configure(void);               /* once, after the arena exists and env is loaded */
void xv_render_view_present(unsigned frame);       /* per Present: schedules/advances learning passes */
void xv_render_view_enter(unsigned *scope, void *context);   /* scene entry (generated hook, BCB30) */
void xv_render_view_leave(unsigned *scope);        /* scene exit (cleanup attribute) */
void xv_render_view_report(unsigned frames);       /* periodic log line */
void xv_render_view_mirror(uint32_t vpage, uint32_t arena_off);  /* live table write -> render table */
void xv_render_view_fiber_switch(void);
void xv_render_view_watchdog(void);                /* remote thread: restore the live mapping if a scene is stuck */            /* guest scheduler leaving the scene's thread: publish + drop to live */
