#ifndef XV_SUBMIT_CPU_H
#define XV_SUBMIT_CPU_H
/* Pump-owned diagnostic; no synchronization or ownership changes. */
static int xv_submit_cpu_enabled;
static uint64_t xv_submit_cpu_begin_us, xv_submit_cpu_begin_run;
static int xv_submit_cpu_begin_valid;
static unsigned xv_submit_cpu_count, xv_submit_cpu_valid, xv_submit_cpu_failed;
static uint64_t xv_submit_cpu_wall, xv_submit_cpu_run;
static int xv_submit_cpu_read(uint64_t *run)
{
    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof ti); ti.size = sizeof ti;
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) < 0 || !ti.name[0]) return 0;
    *run = (uint64_t)ti.runClocks;
    return 1;
}
static void xv_submit_cpu_begin(void)
{
    if (!xv_submit_cpu_enabled) return;
    xv_submit_cpu_begin_us = sceKernelGetProcessTimeWide();
    xv_submit_cpu_begin_valid = xv_submit_cpu_read(&xv_submit_cpu_begin_run);
}
static void xv_submit_cpu_end(int failed)
{
    if (!xv_submit_cpu_enabled) return;
    uint64_t run = 0;
    int valid = xv_submit_cpu_read(&run);
    uint64_t now = sceKernelGetProcessTimeWide();
    xv_submit_cpu_failed += !!failed;
    if (valid && xv_submit_cpu_begin_valid && now >= xv_submit_cpu_begin_us &&
        run >= xv_submit_cpu_begin_run &&
        run - xv_submit_cpu_begin_run <= now - xv_submit_cpu_begin_us) {
        xv_submit_cpu_valid++;
        xv_submit_cpu_wall += now - xv_submit_cpu_begin_us;
        xv_submit_cpu_run += run - xv_submit_cpu_begin_run;
    }
    if (++xv_submit_cpu_count == 60) {
        XV_LOG("[submit-cpu] %u submissions valid %u failed %u; sum-us wall %llu cpu %llu remainder %llu; remainder includes waits/preemption and sampling overhead, not GPU service time\n",
            xv_submit_cpu_count, xv_submit_cpu_valid, xv_submit_cpu_failed,
            (unsigned long long)xv_submit_cpu_wall, (unsigned long long)xv_submit_cpu_run,
            (unsigned long long)(xv_submit_cpu_wall - xv_submit_cpu_run));
        xv_submit_cpu_count = xv_submit_cpu_valid = xv_submit_cpu_failed = 0;
        xv_submit_cpu_wall = xv_submit_cpu_run = 0;
    }
}
#endif
