/*
 * xv_tpidr_probe.c - start-up probe (XV_TPIDR_PROBE=1, diagnostic): does the Vita
 * kernel keep the ARM user thread-ID registers per thread?
 *   TPIDRURW (CP15 c13,c0,2) is user read/write; TPIDRURO (c13,c0,3) user read-only.
 * If TPIDRURW survives sleeps, preemption and core migration per thread, generated
 * code can fetch a per-thread page-table pointer in one instruction, which is the
 * cheap way to give a render thread a frozen (copy-on-write) view of guest memory.
 * The probe writes the register only when every thread starts with it at zero (a
 * non-zero value would mean the system already uses it); it restores the original
 * value before each thread exits. Off by default the function is a no-op.
 */
#include "xv_log.h"
#if XV_TPIDR_PROBE && defined(__vita__)
#include <stdint.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

static inline uint32_t rd_urw(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c13, c0, 2" : "=r"(v)); return v; }
static inline void wr_urw(uint32_t v) { __asm__ volatile("mcr p15, 0, %0, c13, c0, 2" :: "r"(v) : "memory"); }
static inline uint32_t rd_uro(void) { uint32_t v; __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(v)); return v; }

typedef struct {
    unsigned index; int mask; uint32_t value;
    uint32_t urw_initial, uro_first, uro_last, first_foreign;
    int write_ok, writes_allowed;
    unsigned checks, mismatches, uro_changes, migrations;
    volatile int initial_read;
} Probe;

static volatile int go;              /* 0: report initial values only; 1: run the write test */
static void spin_us(unsigned us) { uint64_t t = sceKernelGetProcessTimeWide(); while (sceKernelGetProcessTimeWide() - t < us) {} }

static int probe_thread(SceSize args, void *argp)
{
    (void)args;
    Probe *p = *(Probe **)argp;
    p->urw_initial = rd_urw(); p->uro_first = p->uro_last = rd_uro(); p->initial_read = 1;
    while (!go) sceKernelDelayThread(200);
    if (go < 0) return 0;
    static const int cores[3] = { SCE_KERNEL_CPU_MASK_USER_0, SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_USER_2 };
    p->writes_allowed = 1;
    wr_urw(p->value); p->write_ok = rd_urw() == p->value;
    SceUID self = sceKernelGetThreadId();
    for (unsigned i = 0; i < 400; i++) {
        if (i & 1) sceKernelDelayThread(500); else spin_us(200);
        if (p->mask == SCE_KERNEL_CPU_MASK_USER_ALL && (i % 10) == 9)
            if (sceKernelChangeThreadCpuAffinityMask(self, cores[(i / 10) % 3]) >= 0) p->migrations++;
        uint32_t v = rd_urw(); p->checks++;
        if (v != p->value) { if (!p->mismatches) p->first_foreign = v; p->mismatches++; wr_urw(p->value); }
        uint32_t o = rd_uro(); if (o != p->uro_last) { p->uro_changes++; p->uro_last = o; }
    }
    wr_urw(p->urw_initial);
    return 0;
}

void xv_tpidr_probe(void)
{
    static Probe probes[4];
    static const int masks[4] = { SCE_KERNEL_CPU_MASK_USER_0, SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_USER_2, SCE_KERNEL_CPU_MASK_USER_ALL };
    SceUID th[4];
    uint32_t main_initial = rd_urw(), main_uro = rd_uro();
    go = 0;
    for (unsigned i = 0; i < 4; i++) {
        probes[i] = (Probe){ .index = i, .mask = masks[i], .value = 0xA5000000u | (i << 16) | 0x1234u };
        Probe *pp = &probes[i];
        th[i] = sceKernelCreateThread("xv_tpidr_probe", probe_thread, sceKernelGetThreadCurrentPriority(), 64 * 1024, 0, masks[i], NULL);
        if (th[i] < 0) { xv_logf("[tpidr] thread %u create failed %08x\n", i, (unsigned)th[i]); continue; }
        if (sceKernelStartThread(th[i], sizeof pp, &pp) < 0) { xv_logf("[tpidr] thread %u start failed\n", i); sceKernelDeleteThread(th[i]); th[i] = -1; }
    }
    for (unsigned i = 0; i < 4; i++) if (th[i] >= 0) while (!probes[i].initial_read) sceKernelDelayThread(200);
    int all_zero = main_initial == 0;
    for (unsigned i = 0; i < 4; i++) if (th[i] >= 0 && probes[i].urw_initial != 0) all_zero = 0;
    xv_logf("[tpidr] initial: main urw %08x uro %08x; threads urw %08x %08x %08x %08x uro %08x %08x %08x %08x; %s\n",
            (unsigned)main_initial, (unsigned)main_uro,
            (unsigned)probes[0].urw_initial, (unsigned)probes[1].urw_initial, (unsigned)probes[2].urw_initial, (unsigned)probes[3].urw_initial,
            (unsigned)probes[0].uro_first, (unsigned)probes[1].uro_first, (unsigned)probes[2].uro_first, (unsigned)probes[3].uro_first,
            all_zero ? "TPIDRURW unused, running write test" : "TPIDRURW in use, read-only report");
    uint32_t main_value = 0xA5FF1234u; int main_write_ok = 0, main_kept = 0;
    if (all_zero) { wr_urw(main_value); main_write_ok = rd_urw() == main_value; }
    go = all_zero ? 1 : -1;
    for (unsigned i = 0; i < 4; i++) if (th[i] >= 0) { sceKernelWaitThreadEnd(th[i], NULL, NULL); sceKernelDeleteThread(th[i]); }
    if (all_zero) { main_kept = rd_urw() == main_value; wr_urw(main_initial); }
    if (!all_zero) return;
    int ok = main_write_ok && main_kept;
    for (unsigned i = 0; i < 4; i++) {
        Probe *p = &probes[i];
        if (th[i] < 0 || !p->writes_allowed) { ok = 0; continue; }
        xv_logf("[tpidr] thread %u mask %08x: write_ok %d mismatches %u/%u first_foreign %08x uro %08x->%08x changes %u migrations %u\n",
                i, (unsigned)p->mask, p->write_ok, p->mismatches, p->checks, (unsigned)p->first_foreign,
                (unsigned)p->uro_first, (unsigned)p->uro_last, p->uro_changes, p->migrations);
        if (!p->write_ok || p->mismatches) ok = 0;
    }
    xv_logf("[tpidr] main: write_ok %d kept_across_probe %d\n", main_write_ok, main_kept);
    xv_logf("[tpidr] verdict: TPIDRURW per-thread preserved: %s\n", ok ? "YES" : "NO");
}
#else
void xv_tpidr_probe(void) {}
#endif
