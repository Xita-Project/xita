/* sampler.c - host-only in-process sampling profiler (no perf on this machine). XV_HOST_SAMPLE=<file>: a 1 ms
 * ITIMER_PROF timer; the handler buckets the interrupted PC (PIE-relative offset) per thread kind (the harness
 * main thread = the owner/tick, 2 = the scene helper thread (XV_SCENE_THREAD), 1 = any other: workers). Every 60 presented frames host_reports.c calls
 * xv_host_sample_dump(): "tid-kind offset count" lines, reset. tools/host_profile.py resolves the offsets with
 * addr2line and aggregates by guest function (f_XXXXXXXX) and by thread kind. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#if defined(__x86_64__)
#define SPL_PC(uc) ((uintptr_t)(uc)->uc_mcontext.gregs[REG_RIP])
#elif defined(__arm__)
#define SPL_PC(uc) ((uintptr_t)(uc)->uc_mcontext.arm_pc)
#elif defined(__aarch64__)
#define SPL_PC(uc) ((uintptr_t)(uc)->uc_mcontext.pc)
#else
#define SPL_PC(uc) ((uintptr_t)0)
#endif
#if defined(__arm__)
#define SPL_LR(uc) ((uintptr_t)(uc)->uc_mcontext.arm_lr)
#elif defined(__aarch64__)
#define SPL_LR(uc) ((uintptr_t)(uc)->uc_mcontext.regs[30])
#else
#define SPL_LR(uc) ((uintptr_t)0)
#endif
#define SPL_BUCKETS 16384
static struct { uint32_t off, lr; uint32_t n; uint8_t kind; } spl[SPL_BUCKETS]; static unsigned spl_used, spl_drop, spl_total;
static pid_t spl_main_tid; static FILE *spl_out; static int spl_on, spl_lr;
/* perf_event (user-mode, works at perf_event_paranoid 2): XV_HOST_PERF=1 counts cycles/instructions/task-clock of the
 * owner and the scene helper and prints "[host-perf]" every 60 frames; XV_HOST_PERF_SAMPLE=<cycles> samples the scene
 * helper every <cycles> user cycles (signal to that thread; its ITIMER_PROF hits are then dropped). ITIMER_PROF and
 * thread CPU timers are tick-limited (HZ=250 on the Pi: 4 ms per sample at most); cycle overflows are not. */
enum { SPL_ROLES = 3 };
static struct { pid_t tid; int cycles, instr, clock, sample; uint64_t last[3]; int ev[4]; uint64_t ev_last[4]; } spl_role[SPL_ROLES];
/* XV_HOST_PERF_EVENTS=<raw hex>[,<raw hex>...] (up to 4, with XV_HOST_PERF=1): also count these raw PMU events per role,
 * "[host-perf-ev]" (Cortex-A72: 0x01 L1I refill, 0x03 L1D refill, 0x17 L2D refill, 0x10 branch mispredict).
 * XV_HOST_PERF_SAMPLE_EVENT=<raw hex>: sample the scene helper every XV_HOST_PERF_SAMPLE occurrences of that raw event
 * instead of every XV_HOST_PERF_SAMPLE cycles (where do the I-cache refills go). */
static uint64_t spl_ev_config[4]; static int spl_ev_n; static uint64_t spl_sample_event;
static int spl_perf_count, spl_perf_period, spl_perf_init;
static int spl_helper_sampling;
static void spl_handler(int sig, siginfo_t *si, void *uc_)
{
    (void)sig; extern char __executable_start;
    uintptr_t pc = SPL_PC((ucontext_t *)uc_); uint32_t off = (uint32_t)(pc - (uintptr_t)&__executable_start);
    uint32_t lr = spl_lr ? (uint32_t)(SPL_LR((ucontext_t *)uc_) - (uintptr_t)&__executable_start) : 0;
    extern int xv_scene_thread_on_helper(void) __attribute__((weak));   /* pthread_self() compare: signal-safe */
    pid_t tid = (pid_t)syscall(SYS_gettid);
    uint8_t kind = tid == spl_main_tid ? 0 : xv_scene_thread_on_helper && xv_scene_thread_on_helper() ? 2 : 1;
    int perf_hit = si->si_code == POLL_IN || si->si_code == POLL_HUP;
    if (perf_hit) {
        if (!spl_on) return;
        kind = 2;
        if (spl_role[2].sample >= 0) ioctl(spl_role[2].sample, PERF_EVENT_IOC_REFRESH, 1);
    } else if (kind == 2 && spl_helper_sampling) return;   /* the helper is sampled by its cycle counter */
    unsigned h = ((off ^ lr * 40503u) * 2654435761u ^ kind) & (SPL_BUCKETS - 1);
    for (unsigned i = 0; i < 64; ++i, h = (h + 1) & (SPL_BUCKETS - 1)) {
        if (spl[h].n == 0) { spl[h].off = off; spl[h].lr = lr; spl[h].kind = kind; spl[h].n = 1; spl_used++; spl_total++; return; }
        if (spl[h].off == off && spl[h].lr == lr && spl[h].kind == kind) { spl[h].n++; spl_total++; return; }
    }
    spl_drop++;
}
static int spl_perf_open(uint32_t type, uint64_t config, int group, uint64_t period)
{
    struct perf_event_attr at; memset(&at, 0, sizeof at);
    at.size = sizeof at; at.type = type; at.config = config; at.exclude_kernel = 1; at.exclude_hv = 1;
    if (period) { at.sample_period = period; at.wakeup_events = 1; at.disabled = 1; }
    return (int)syscall(SYS_perf_event_open, &at, 0, -1, group, 0);
}
static void spl_perf_configure(void)
{
    if (spl_perf_init) return;
    spl_perf_init = 1;
    const char *c = getenv("XV_HOST_PERF"); spl_perf_count = c && atoi(c);
    const char *p = getenv("XV_HOST_PERF_SAMPLE"); spl_perf_period = p ? atoi(p) : 0;
    const char *ev = getenv("XV_HOST_PERF_EVENTS");
    while (ev && *ev && spl_ev_n < 4) { char *end; spl_ev_config[spl_ev_n++] = strtoull(ev, &end, 16); ev = *end == ',' ? end + 1 : end; if (end == ev && *ev != ',') break; }
    const char *se = getenv("XV_HOST_PERF_SAMPLE_EVENT"); spl_sample_event = se ? strtoull(se, NULL, 16) : 0;
    for (unsigned r = 0; r < SPL_ROLES; r++) { spl_role[r].cycles = spl_role[r].instr = spl_role[r].clock = spl_role[r].sample = -1;
        for (int k = 0; k < 4; k++) spl_role[r].ev[k] = -1; }
}
/* On the thread itself: 0 = owner (harness main thread), 2 = scene helper (xk_scene_thread.c helper_main). */
void xv_host_thread_role(int role)
{
    spl_perf_configure();
    if (role < 0 || role >= SPL_ROLES) return;
    spl_role[role].tid = (pid_t)syscall(SYS_gettid);
    if (spl_perf_count) {
        spl_role[role].cycles = spl_perf_open(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1, 0);
        spl_role[role].instr = spl_perf_open(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1, 0);
        spl_role[role].clock = spl_perf_open(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, -1, 0);
        for (int k = 0; k < spl_ev_n; k++) spl_role[role].ev[k] = spl_perf_open(PERF_TYPE_RAW, spl_ev_config[k], -1, 0);
        fprintf(stderr, "[host-perf] role %d tid %d counters %d/%d/%d\n", role, (int)spl_role[role].tid,
            spl_role[role].cycles, spl_role[role].instr, spl_role[role].clock);
    }
    if (role == 2 && spl_perf_period > 0) {
        int fd = spl_sample_event ? spl_perf_open(PERF_TYPE_RAW, spl_sample_event, -1, (uint64_t)spl_perf_period) :
                                    spl_perf_open(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1, (uint64_t)spl_perf_period);
        if (fd >= 0) {
            struct f_owner_ex owner = { F_OWNER_TID, spl_role[role].tid };
            fcntl(fd, F_SETFL, O_ASYNC | O_NONBLOCK); fcntl(fd, F_SETSIG, SIGPROF); fcntl(fd, F_SETOWN_EX, &owner);
            spl_role[role].sample = fd; spl_helper_sampling = 1;
            ioctl(fd, PERF_EVENT_IOC_RESET, 0); ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
            fprintf(stderr, "[host-perf] scene helper sampled every %d user %s\n", spl_perf_period, spl_sample_event ? "raw events" : "cycles");
        } else fprintf(stderr, "[host-perf] helper sampling unavailable\n");
    }
}
static uint64_t spl_read(int fd) { uint64_t v = 0; if (fd >= 0 && read(fd, &v, sizeof v) != sizeof v) v = 0; return v; }
void xv_host_perf_report(unsigned frame, unsigned frames)
{
    extern unsigned xv_rec_ab_frames[2] __attribute__((weak));   /* cumulative XV_REC_AB phase frames (xd3d.c) */
    static unsigned last_ab[2];
    unsigned now0 = xv_rec_ab_frames ? xv_rec_ab_frames[0] : 0, now1 = xv_rec_ab_frames ? xv_rec_ab_frames[1] : 0;
    unsigned ab0 = now0 - last_ab[0], ab1 = now1 - last_ab[1];
    last_ab[0] = now0; last_ab[1] = now1;
    if (!spl_perf_count || !frames) return;
    for (unsigned r = 0; r < SPL_ROLES; r++) {
        if (spl_role[r].cycles < 0) continue;
        uint64_t now[3] = { spl_read(spl_role[r].cycles), spl_read(spl_role[r].instr), spl_read(spl_role[r].clock) }, d[3];
        for (int k = 0; k < 3; k++) { d[k] = now[k] - spl_role[r].last[k]; spl_role[r].last[k] = now[k]; }
        fprintf(stderr, "[host-perf] frame %u %s: %.3f Mcycles %.3f Minstr %.3f ms CPU per frame (%u frames) ab %u %u\n", frame,
            r == 0 ? "owner" : r == 1 ? "rec-worker" : "scene-helper", d[0] / 1e6 / frames, d[1] / 1e6 / frames, d[2] / 1e6 / frames, frames, ab0, ab1);
        if (spl_ev_n) {
            char line[256]; int n = snprintf(line, sizeof line, "[host-perf-ev] frame %u %s:", frame, r == 0 ? "owner" : r == 1 ? "rec-worker" : "scene-helper");
            for (int k = 0; k < spl_ev_n; k++) {
                uint64_t v = spl_read(spl_role[r].ev[k]), dv = v - spl_role[r].ev_last[k]; spl_role[r].ev_last[k] = v;
                n += snprintf(line + n, sizeof line - n, " 0x%llx %.1f k", (unsigned long long)spl_ev_config[k], dv / 1e3 / frames);
            }
            fprintf(stderr, "%s per frame\n", line);
        }
    }
}
void xv_host_sample_start(void)
{
    xv_host_thread_role(0);   /* the harness main thread: owner counters (XV_HOST_PERF) */
    struct sigaction pa; memset(&pa, 0, sizeof pa); pa.sa_sigaction = spl_handler; pa.sa_flags = SA_SIGINFO | SA_RESTART;
    if (spl_perf_period > 0) sigaction(SIGPROF, &pa, NULL);   /* perf-sampled helper, even without XV_HOST_SAMPLE */
    const char *e = getenv("XV_HOST_SAMPLE"); if (!e) return;
    spl_out = fopen(e, "w"); if (!spl_out) return;
    spl_main_tid = (pid_t)syscall(SYS_gettid);
    { const char *l = getenv("XV_HOST_SAMPLE_LR"); spl_lr = l && atoi(l); }
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = spl_handler; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = { { 0, 1000 }, { 0, 1000 } }; setitimer(ITIMER_PROF, &it, NULL); spl_on = 1;
    fprintf(stderr, "[sampler] 1 ms ITIMER_PROF, PIE-relative offsets -> %s (kind 0 = owner thread, 2 = scene helper, 1 = other)\n", e);
}
void xv_host_sample_dump(unsigned frame)
{
    if (!spl_on) return;
    struct itimerval off = { { 0, 0 }, { 0, 0 } }, on = { { 0, 1000 }, { 0, 1000 } }; setitimer(ITIMER_PROF, &off, NULL);
    extern unsigned xv_rec_ab_frames[2] __attribute__((weak));   /* cumulative XV_REC_AB phase frames (xd3d.c) */
    static unsigned last_ab[2];
    unsigned now0 = xv_rec_ab_frames ? xv_rec_ab_frames[0] : 0, now1 = xv_rec_ab_frames ? xv_rec_ab_frames[1] : 0;
    fprintf(spl_out, "frame %u samples %u dropped %u helper-cycles %d ab %u %u\n", frame, spl_total, spl_drop, spl_helper_sampling ? spl_perf_period : 0,
        now0 - last_ab[0], now1 - last_ab[1]);
    last_ab[0] = now0; last_ab[1] = now1;
    for (unsigned i = 0; i < SPL_BUCKETS; ++i) if (spl[i].n) {
        if (spl_lr) fprintf(spl_out, "%u %x %u %x\n", spl[i].kind, spl[i].off, spl[i].n, spl[i].lr);
        else fprintf(spl_out, "%u %x %u\n", spl[i].kind, spl[i].off, spl[i].n);
        spl[i].n = 0; }
    fflush(spl_out); spl_used = spl_drop = spl_total = 0;
    setitimer(ITIMER_PROF, &on, NULL);
}
