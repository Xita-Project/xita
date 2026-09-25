/* sampler.c - host-only in-process sampling profiler (no perf on this machine). XV_HOST_SAMPLE=<file>: a 1 ms
 * ITIMER_PROF timer; the handler buckets the interrupted PC (PIE-relative offset) per thread kind (the harness
 * main thread = the owner/tick, any other = helper/worker). Every 60 presented frames host_reports.c calls
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
#if defined(__x86_64__)
#define SPL_PC(uc) ((uintptr_t)(uc)->uc_mcontext.gregs[REG_RIP])
#elif defined(__arm__)
#define SPL_PC(uc) ((uintptr_t)(uc)->uc_mcontext.arm_pc)
#elif defined(__aarch64__)
#define SPL_PC(uc) ((uintptr_t)(uc)->uc_mcontext.pc)
#else
#define SPL_PC(uc) ((uintptr_t)0)
#endif
#define SPL_BUCKETS 16384
static struct { uint32_t off; uint32_t n; uint8_t kind; } spl[SPL_BUCKETS]; static unsigned spl_used, spl_drop, spl_total;
static pid_t spl_main_tid; static FILE *spl_out; static int spl_on;
static void spl_handler(int sig, siginfo_t *si, void *uc_)
{
    (void)sig; (void)si; extern char __executable_start;
    uintptr_t pc = SPL_PC((ucontext_t *)uc_); uint32_t off = (uint32_t)(pc - (uintptr_t)&__executable_start);
    uint8_t kind = (pid_t)syscall(SYS_gettid) == spl_main_tid ? 0 : 1;
    unsigned h = (off * 2654435761u ^ kind) & (SPL_BUCKETS - 1);
    for (unsigned i = 0; i < 64; ++i, h = (h + 1) & (SPL_BUCKETS - 1)) {
        if (spl[h].n == 0) { spl[h].off = off; spl[h].kind = kind; spl[h].n = 1; spl_used++; spl_total++; return; }
        if (spl[h].off == off && spl[h].kind == kind) { spl[h].n++; spl_total++; return; }
    }
    spl_drop++;
}
void xv_host_sample_start(void)
{
    const char *e = getenv("XV_HOST_SAMPLE"); if (!e) return;
    spl_out = fopen(e, "w"); if (!spl_out) return;
    spl_main_tid = (pid_t)syscall(SYS_gettid);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = spl_handler; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = { { 0, 1000 }, { 0, 1000 } }; setitimer(ITIMER_PROF, &it, NULL); spl_on = 1;
    fprintf(stderr, "[sampler] 1 ms ITIMER_PROF, PIE-relative offsets -> %s (kind 0 = owner thread, 1 = other)\n", e);
}
void xv_host_sample_dump(unsigned frame)
{
    if (!spl_on) return;
    struct itimerval off = { { 0, 0 }, { 0, 0 } }, on = { { 0, 1000 }, { 0, 1000 } }; setitimer(ITIMER_PROF, &off, NULL);
    fprintf(spl_out, "frame %u samples %u dropped %u\n", frame, spl_total, spl_drop);
    for (unsigned i = 0; i < SPL_BUCKETS; ++i) if (spl[i].n) { fprintf(spl_out, "%u %x %u\n", spl[i].kind, spl[i].off, spl[i].n); spl[i].n = 0; }
    fflush(spl_out); spl_used = spl_drop = spl_total = 0;
    setitimer(ITIMER_PROF, &on, NULL);
}
