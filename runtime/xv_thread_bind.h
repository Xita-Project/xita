/*
 * xv_thread_bind.h - force-included (Makefile XV_THREAD_PAGE_TABLE=1) into every Vita translation unit:
 * routes sceKernelCreateThread through a trampoline that binds the new thread's TPIDRURW to the live
 * guest page table before its entry runs, so X_PT (recomp/xv_x86rt.h) is valid on every runtime thread.
 * Threads the runtime does not create (GXM display callback) never touch guest memory.
 */
#pragma once
#if defined(__vita__) && !defined(XV_THREAD_BIND_IMPL)
#include <stdint.h>
#include <psp2/kernel/threadmgr.h>
SceUID xv_thread_bind_create(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
                             SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option);
void xv_thread_bind_current(void);           /* bind the calling thread to the live table */
void xv_thread_bind_table(uint32_t *table);  /* bind the calling thread to a specific table (render view) */
#define sceKernelCreateThread xv_thread_bind_create
#endif
