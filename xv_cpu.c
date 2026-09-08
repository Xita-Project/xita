/* System-wide application-core busy time, from changes in kernel idle clocks.
 * No extra worker thread and no per-thread enumeration. */
#include "xv_cpu.h"
#include "xv_log.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CPU_PERIOD_US UINT64_C(1000000)
#define CPU_UNKNOWN_PACKED UINT32_C(0x00FFFFFF)
static uint32_t g_cpu_usage = CPU_UNKNOWN_PACKED;
static int g_cpu_on = -1, g_cpu_have_previous, g_cpu_warned, g_cpu_polled;
static uint64_t g_cpu_last_poll, g_cpu_previous_time;
static SceKernelSystemInfo g_cpu_previous;
/* Only the serial guest path accesses these; startup thread reports do not. */
static int g_guest_thread_on = -1, g_guest_thread_polled;
static uint64_t g_guest_thread_last_poll;

void xv_cpu_log_thread(const char *role)
{
    const char *e = getenv("XV_THREADS");
    if (!e || !atoi(e)) return;
    SceUID id = sceKernelGetThreadId();
    SceKernelThreadInfo info;
    memset(&info, 0, sizeof info); info.size = sizeof info;
    int rc = sceKernelGetThreadInfo(id, &info);
    /* Successful-but-empty emulator responses are not measured thread data. */
    if (rc < 0 || !info.name[0] || !(info.currentCpuAffinityMask & SCE_KERNEL_CPU_MASK_USER_ALL)) {
        xv_logf("[cpu-thread] role=%s id=%08X core=%d info-unavailable rc=%08X affinity=%08X\n",
                role, (unsigned)id, sceKernelGetCpuId(), (unsigned)rc, (unsigned)info.currentCpuAffinityMask);
        return;
    }
    info.name[sizeof info.name - 1] = 0;
    xv_logf("[cpu-thread] role=%s id=%08X name=%s at=%.3f s core=%d affinity=%08X last=%d priority=%d status=%X run-clocks=%llu migrations=%d\n",
            role, (unsigned)id, info.name, sceKernelGetProcessTimeWide() / 1000000.0,
            sceKernelGetCpuId(), (unsigned)info.currentCpuAffinityMask,
            info.lastExecutedCpuId, info.currentPriority, (unsigned)info.status,
            (unsigned long long)info.runClocks, info.changeCpuCount);
}

void xv_cpu_guest_poll(void)
{
    if (g_guest_thread_on < 0) {
        const char *e = getenv("XV_THREADS"); g_guest_thread_on = e && atoi(e) != 0;
    }
    if (!g_guest_thread_on) return;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (g_guest_thread_polled && now >= g_guest_thread_last_poll && now - g_guest_thread_last_poll < CPU_PERIOD_US) return;
    g_guest_thread_last_poll = now; g_guest_thread_polled = 1;
    xv_cpu_log_thread("guest-present");
}

uint32_t xv_cpu_usage(void)
{
    return __atomic_load_n(&g_cpu_usage, __ATOMIC_ACQUIRE);
}

/* Firmware CPU affinity masks use bits 16..19. Accept physical low-bit masks
 * too; neither representation makes the reserved fourth core an app core. */
static unsigned cpu_mask(uint32_t mask)
{
    return ((mask >> 16) | mask) & 7u;
}

static uint32_t cpu_interval(const SceKernelSystemInfo *before,
                             const SceKernelSystemInfo *after, uint64_t elapsed)
{
    uint32_t packed = CPU_UNKNOWN_PACKED;
    unsigned active = cpu_mask(before->activeCpuMask) & cpu_mask(after->activeCpuMask);
    if (!elapsed) return packed;
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t old_idle = before->cpuInfo[i].idleClock, idle = after->cpuInfo[i].idleClock;
        if (!(active & (1u << i)) || idle < old_idle) continue;
        uint64_t idle_delta = idle - old_idle;
        /* The time and idle reads are separate syscalls. Allow 2 ms of boundary
         * skew, but don't turn an implausible/reset counter into a valid 0%. */
        if (idle_delta > elapsed && idle_delta - elapsed > 2000u) continue;
        double idle_fraction = (double)idle_delta / (double)elapsed;
        unsigned busy = idle_fraction >= 1.0 ? 0 : (unsigned)(100.0 * (1.0 - idle_fraction) + 0.5);
        packed = (packed & ~(255u << (8u * i))) | (busy << (8u * i));
    }
    return packed;
}

void xv_cpu_poll(uint64_t now_us)
{
    if (g_cpu_on < 0) {
        const char *e = getenv("XV_CPU"); g_cpu_on = e ? atoi(e) != 0 : 1;
        if (g_cpu_on)
            xv_logf("[cpu] per-core busy time enabled (kernel idle counters, ~1 s intervals, system-wide; core 3 reserved)\n");
    }
    if (!g_cpu_on) return;
    if (g_cpu_polled && now_us >= g_cpu_last_poll && now_us - g_cpu_last_poll < CPU_PERIOD_US) return;
    g_cpu_last_poll = now_us; g_cpu_polled = 1;
    SceKernelSystemInfo info;
    memset(&info, 0, sizeof info); info.size = sizeof info;
    int rc = sceKernelGetSystemInfo(&info);
    /* Vita3K currently stubs this call without populating the structure. A
     * successful return with an empty mask must show unknown, not 100% busy. */
    if (rc < 0 || !cpu_mask(info.activeCpuMask)) {
        __atomic_store_n(&g_cpu_usage, CPU_UNKNOWN_PACKED, __ATOMIC_RELEASE);
        g_cpu_have_previous = 0;
        if (!g_cpu_warned) {
            xv_logf("[cpu] counters unavailable: rc %08X active-mask %08X; C0/C1/C2=n/a\n", (unsigned)rc, (unsigned)info.activeCpuMask);
            g_cpu_warned = 1;
        }
        return;
    }
    g_cpu_warned = 0;
    uint32_t usage = CPU_UNKNOWN_PACKED;
    if (g_cpu_have_previous && now_us > g_cpu_previous_time) {
        uint64_t elapsed = now_us - g_cpu_previous_time;
        usage = cpu_interval(&g_cpu_previous, &info, elapsed);
        char label[3][8];
        for (unsigned i = 0; i < 3; ++i) {
            unsigned percent = (usage >> (i * 8)) & 255u;
            if (percent == XV_CPU_UNKNOWN) snprintf(label[i], sizeof label[i], "n/a");
            else snprintf(label[i], sizeof label[i], "%u%%", percent);
        }
        xv_logf("[cpu] at %.3f s window %.1f ms | C0 %s C1 %s C2 %s | active-mask %08X\n",
                now_us / 1000000.0, elapsed / 1000.0, label[0], label[1], label[2], (unsigned)info.activeCpuMask);
    }
    g_cpu_previous = info; g_cpu_previous_time = now_us; g_cpu_have_previous = 1;
    __atomic_store_n(&g_cpu_usage, usage, __ATOMIC_RELEASE);
}
