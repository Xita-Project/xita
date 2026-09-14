/* Execute the actual monitor with controlled kernel counters and wall time. */
#include <assert.h>
#include <stdarg.h>
#include "../../runtime/xv_cpu.c"

static SceKernelSystemInfo fixture;
static int fixture_rc, queries, logs;
static char last_log[512];
static SceKernelThreadInfo thread_fixture;
static int thread_rc, thread_queries, current_core;
static uint64_t process_time;

SceUInt64 sceKernelGetProcessTimeWide(void) { return process_time; }
int sceKernelGetThreadId(void) { return 42; }
int sceKernelGetCpuId(void) { return current_core; }
int sceKernelGetThreadInfo(SceUID id, SceKernelThreadInfo *info)
{
    assert(id == 42 && info->size == sizeof *info);
    thread_queries++;
    if (thread_rc >= 0) *info = thread_fixture;
    return thread_rc;
}

int sceKernelGetSystemInfo(SceKernelSystemInfo *info)
{
    assert(info->size == sizeof *info);
    queries++;
    if (fixture_rc >= 0) *info = fixture;
    return fixture_rc;
}
void xv_logf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(last_log, sizeof last_log, fmt, ap); va_end(ap); logs++;
}
static void reset(void)
{
    unsetenv("XV_CPU");
    unsetenv("XV_THREADS");
    g_guest_thread_on = -1; g_guest_thread_polled = 0; g_guest_thread_last_poll = 0;
    memset(&thread_fixture, 0, sizeof thread_fixture);
    thread_rc = thread_queries = current_core = 0; process_time = 0;
    g_cpu_usage = CPU_UNKNOWN_PACKED; g_cpu_on = -1;
    g_cpu_have_previous = g_cpu_warned = g_cpu_polled = 0;
    g_cpu_last_poll = g_cpu_previous_time = 0;
    memset(&g_cpu_previous, 0, sizeof g_cpu_previous);
    memset(&fixture, 0, sizeof fixture);
    fixture.size = sizeof fixture; fixture.activeCpuMask = 0x70000;
    for (unsigned i = 0; i < 4; ++i) fixture.cpuInfo[i].idleClock = UINT64_C(0x100000000) + i;
    fixture_rc = queries = logs = 0; last_log[0] = 0;
}
static unsigned usage(unsigned core) { return (xv_cpu_usage() >> (8 * core)) & 255u; }
static void advance_idle(uint64_t c0, uint64_t c1, uint64_t c2)
{
    fixture.cpuInfo[0].idleClock += c0;
    fixture.cpuInfo[1].idleClock += c1;
    fixture.cpuInfo[2].idleClock += c2;
}
int main(void)
{
    reset();
    xv_cpu_poll(0); assert(queries == 1); assert(xv_cpu_usage() == CPU_UNKNOWN_PACKED);
    for (uint64_t t = 1; t < 1000000; t += 1000) xv_cpu_poll(t);
    assert(queries == 1); /* frame/poll rate must not become sampling rate */
    advance_idle(0, 250000, 1000000); xv_cpu_poll(1000000);
    assert(usage(0) == 100 && usage(1) == 75 && usage(2) == 0);
    assert(strstr(last_log, "C0 100% C1 75% C2 0%"));
    advance_idle(1000000, 500000, 750000); xv_cpu_poll(3000000);
    assert(usage(0) == 50 && usage(1) == 75 && usage(2) == 63); /* actual 2 s, not assumed 1 s */
    fixture.cpuInfo[0].idleClock = 0; /* one reset must not spoil the other cores */
    advance_idle(0, 0, 1000001); xv_cpu_poll(4000000);
    assert(usage(0) == XV_CPU_UNKNOWN && usage(1) == 100 && usage(2) == 0);
    advance_idle(1003000, 1000000, 500000); xv_cpu_poll(5000000);
    assert(usage(0) == XV_CPU_UNKNOWN && usage(1) == 0 && usage(2) == 50);
    fixture.activeCpuMask = 5; /* low-bit mask, inactive core 1 */
    advance_idle(500000, 0, 500000); xv_cpu_poll(6000000);
    assert(usage(0) == 50 && usage(1) == XV_CPU_UNKNOWN && usage(2) == 50);
    fixture.activeCpuMask = 7;
    advance_idle(0, 0, 0); xv_cpu_poll(7000000);
    assert(usage(1) == XV_CPU_UNKNOWN); /* newly visible counter needs a baseline */
    advance_idle(0, 0, 0); xv_cpu_poll(8000000); assert(usage(1) == 100);
    xv_cpu_poll(10); assert(xv_cpu_usage() == CPU_UNKNOWN_PACKED); /* clock moved backwards */
    advance_idle(1000000, 1000000, 1000000); xv_cpu_poll(1000010);
    assert(usage(0) == 0 && usage(1) == 0 && usage(2) == 0);

    reset(); xv_cpu_poll(1); fixture_rc = -1; xv_cpu_poll(1000001);
    assert(xv_cpu_usage() == CPU_UNKNOWN_PACKED && strstr(last_log, "unavailable"));
    int n = logs; xv_cpu_poll(2000001); assert(logs == n); /* unavailable warning is bounded */
    fixture_rc = 0; xv_cpu_poll(3000001); assert(xv_cpu_usage() == CPU_UNKNOWN_PACKED);
    advance_idle(500000, 500000, 500000); xv_cpu_poll(4000001); assert(usage(0) == 50);
    memset(&fixture, 0, sizeof fixture); fixture.size = sizeof fixture;
    xv_cpu_poll(5000001); assert(xv_cpu_usage() == CPU_UNKNOWN_PACKED); /* success/no data: emulator stub */
    fixture.activeCpuMask = 0x80000; xv_cpu_poll(6000001);
    assert(xv_cpu_usage() == CPU_UNKNOWN_PACKED); /* system core alone is not three busy app cores */

    reset(); setenv("XV_CPU", "0", 1); xv_cpu_poll(1); xv_cpu_poll(2000000);
    assert(queries == 0 && logs == 0 && xv_cpu_usage() == CPU_UNKNOWN_PACKED);

    reset(); xv_cpu_log_thread("disabled"); xv_cpu_guest_poll();
    assert(thread_queries == 0 && logs == 0);
    reset(); setenv("XV_THREADS", "1", 1);
    strcpy(thread_fixture.name, "xk_fiber");
    thread_fixture.currentCpuAffinityMask = 0x70000;
    thread_fixture.lastExecutedCpuId = current_core = 2;
    thread_fixture.runClocks = UINT64_C(0x100000001);
    xv_cpu_guest_poll();
    assert(thread_queries == 1 && strstr(last_log, "role=guest-present"));
    assert(strstr(last_log, "name=xk_fiber") && strstr(last_log, "core=2 affinity=00070000"));
    assert(strstr(last_log, "run-clocks=4294967297"));
    for (process_time = 1; process_time < 1000000; process_time += 1000) xv_cpu_guest_poll();
    assert(thread_queries == 1);
    process_time = 1000000; xv_cpu_guest_poll(); assert(thread_queries == 2);
    process_time = 0; xv_cpu_guest_poll(); assert(thread_queries == 3);
    /* Never label a thread according to its requested core or the guest role. */
    strcpy(thread_fixture.name, "xv_pump");
    thread_fixture.currentCpuAffinityMask = 0x20000; current_core = 1;
    xv_cpu_log_thread("render-pump");
    assert(strstr(last_log, "name=xv_pump") && strstr(last_log, "core=1 affinity=00020000"));
    thread_fixture.currentCpuAffinityMask=SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT;
    strcpy(thread_fixture.name,"xk_fiber");current_core=2;
    xv_cpu_log_thread("default-affinity");
    assert(strstr(last_log,"name=xk_fiber") && strstr(last_log,"core=2 affinity=00000000"));
    assert(!strstr(last_log,"unavailable"));
    thread_rc = -1; xv_cpu_log_thread("failed"); assert(strstr(last_log, "unavailable"));
    thread_rc = 0; memset(&thread_fixture, 0, sizeof thread_fixture);
    current_core=1;
    xv_cpu_log_thread("stub"); assert(strstr(last_log, "unavailable"));
    assert(strstr(last_log, "core=1")); /* core ID remains useful when ThreadInfo is empty */
    puts("PASS: per-core idle deltas, variable intervals, 64-bit counters, poll throttle, masks, reset/skew, API failure/recovery, stub rejection and disable");
    puts("PASS: opt-in thread identity, actual affinity/core, 64-bit raw runtime, guest sampling throttle and missing-data rejection");
    return 0;
}
