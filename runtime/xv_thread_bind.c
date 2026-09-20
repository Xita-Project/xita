/* xv_thread_bind.c - see xv_thread_bind.h. */
#define XV_THREAD_BIND_IMPL 1
#include <stdint.h>
#include <psp2/kernel/threadmgr.h>
#include "xv_log.h"
#undef sceKernelCreateThread              /* the force-included header maps it to the wrapper below */

extern uint32_t *g_xpt;
static inline void tpidrurw_write(uint32_t v) { __asm__ volatile("mcr p15, 0, %0, c13, c0, 2" :: "r"(v) : "memory"); }
static inline uint32_t tpidrurw_read(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c13, c0, 2" : "=r"(v)); return v; }

void xv_thread_bind_table(uint32_t *table) { tpidrurw_write((uint32_t)(uintptr_t)table); }
void xv_thread_bind_current(void) { xv_thread_bind_table(g_xpt); }

/* thid -> entry, filled before sceKernelStartThread can run the thread; newest entry wins on reuse. */
enum { SLOTS = 1024 };
static struct { SceUID thid; SceKernelThreadEntry entry; } slots[SLOTS];
static volatile unsigned next_slot;
static unsigned bound_threads, missed_lookups;

static int trampoline(SceSize args, void *argp)
{
    SceUID me = sceKernelGetThreadId();
    SceKernelThreadEntry entry = 0;
    unsigned n = next_slot;
    for (unsigned i = n; i-- > (n > SLOTS ? n - SLOTS : 0);)
        if (slots[i % SLOTS].thid == me) { entry = slots[i % SLOTS].entry; break; }
    if (!entry) { __atomic_add_fetch(&missed_lookups, 1, __ATOMIC_RELAXED); xv_logf("[thread-bind] no entry for thread %08x\n", (unsigned)me); return -1; }
    xv_thread_bind_current();
    __atomic_add_fetch(&bound_threads, 1, __ATOMIC_RELAXED);
    return entry(args, argp);
}

SceUID xv_thread_bind_create(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
                             SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option)
{
    SceUID t = sceKernelCreateThread(name, trampoline, initPriority, stackSize, attr, cpuAffinityMask, option);
    if (t < 0) return t;
    unsigned i = __atomic_fetch_add(&next_slot, 1, __ATOMIC_RELAXED) % SLOTS;
    slots[i].entry = entry;
    __atomic_store_n(&slots[i].thid, t, __ATOMIC_RELEASE);
    return t;
}

unsigned xv_thread_bind_count(void) { return bound_threads; }
int xv_thread_bind_is_bound(void) { return tpidrurw_read() != 0; }
