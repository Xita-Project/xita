/* write_watch.c - host-only diagnostic: XV_WRITE_WATCH=<guest hex address>. While a scene is bound, the live
 * page holding that address and its shadow copy are made read-only; the first write to each from either half
 * faults, and the handler logs a host backtrace (the recompiled guest function names f_XXXXXXXX appear in it),
 * then unprotects the page for the rest of the frame. Names the tick-side and scene-side writers of a word the
 * render-view conflict census reported. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <execinfo.h>
#include <pthread.h>
#if defined(__x86_64__)
#define WW_PC(uc) ((void *)(uc)->uc_mcontext.gregs[REG_RIP])
#define WW_SP(uc) ((void *)(uc)->uc_mcontext.gregs[REG_RSP])
#elif defined(__arm__)
#define WW_PC(uc) ((void *)(uc)->uc_mcontext.arm_pc)
#define WW_SP(uc) ((void *)(uc)->uc_mcontext.arm_sp)
#elif defined(__aarch64__)
#define WW_PC(uc) ((void *)(uc)->uc_mcontext.pc)
#define WW_SP(uc) ((void *)(uc)->uc_mcontext.sp)
#else
#define WW_PC(uc) ((void *)0)
#define WW_SP(uc) ((void *)0)
#endif
extern uint8_t *g_xram; extern uint32_t *g_xpt;   /* the LIVE table: the thread running the body is bound to the render table */
uint8_t *xv_render_view_shadow_of_phys(uint32_t phys_page);   /* xk_render_view.c: NULL when the page has no slot */
static void ww_altstack(void) { static __thread int done; if (done) return; done = 1; stack_t ss; ss.ss_sp = malloc(1 << 16); ss.ss_size = 1 << 16; ss.ss_flags = 0; if (ss.ss_sp) sigaltstack(&ss, NULL); }
static uint32_t ww_addr; static uint8_t *ww_pages[2]; static const char *ww_names[2] = { "live", "shadow" }; static unsigned ww_hits[2], ww_frames;
static void ww_handler(int sig, siginfo_t *si, void *uc_)
{
    uint8_t *a = si->si_addr;
    for (int i = 0; i < 2; ++i) if (ww_pages[i] && a >= ww_pages[i] && a < ww_pages[i] + 4096) {
        mprotect(ww_pages[i], 4096, PROT_READ | PROT_WRITE);
        if (ww_hits[i]++ < 12) {
            void *bt[10]; int n = backtrace(bt, 10); char **sym = backtrace_symbols(bt, n);
            fprintf(stderr, "[write-watch] %s page: write to %08X+%X (frame-scene %u):", ww_names[i], ww_addr & ~0xFFFu, (unsigned)(a - ww_pages[i]), ww_frames);
            for (int k = 2; k < n && sym; ++k) { const char *s = strchr(sym[k], '('); fprintf(stderr, " %.*s", s ? (int)(strchr(s, '+') ? strchr(s, '+') - s - 1 : 24) : 20, s ? s + 1 : sym[k]); }
            { extern char __executable_start; fprintf(stderr, " | pcs:"); for (int k = 2; k < n; ++k) fprintf(stderr, " %lx", (unsigned long)((char *)bt[k] - &__executable_start)); }
            fprintf(stderr, "\n"); free(sym);   /* PIE: offsets from the load base; addr2line -f -e harness <offset> names them */
        }
        return;
    }
    { ucontext_t *uc = uc_; extern char __executable_start; fprintf(stderr, "[write-watch] foreign SIGSEGV at %p (pc offset %lx, sp %p): default action\n", (void *)a, (unsigned long)((char *)WW_PC(uc) - &__executable_start), WW_SP(uc)); }
    signal(sig, SIG_DFL);   /* not ours: re-raise with the default action */
}
void xv_write_watch_init(void) { if (getenv("XV_WRITE_WATCH")) ww_altstack(); }   /* harness main thread (the owner) */
void xv_write_watch_arm(void)   /* called by the render view at scene entry, on the thread running the body, after copy-in */
{
    ww_altstack();
    static int init; if (!init) { init = 1; const char *e = getenv("XV_WRITE_WATCH"); if (!e) return; ww_addr = (uint32_t)strtoul(e, NULL, 16);
        struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = ww_handler; sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK; sigaction(SIGSEGV, &sa, NULL);
        fprintf(stderr, "[write-watch] watching guest %08X (live page + shadow copy) from the next scene on\n", ww_addr); }
    if (!ww_addr) return;
    uint32_t off = g_xpt[ww_addr >> 12]; if (!off) return;
    ww_pages[0] = g_xram + (off & ~0xFFFu); ww_pages[1] = xv_render_view_shadow_of_phys(off >> 12); ww_frames++;
    { static int once; if (!once) { once = 1; fprintf(stderr, "[write-watch] armed: pte off %08X live %p (aligned %d) shadow %p\n", off, (void *)ww_pages[0], !((uintptr_t)ww_pages[0] & 0xFFF), (void *)ww_pages[1]); } }
    for (int i = 0; i < 2; ++i) if (ww_pages[i] && ((uintptr_t)ww_pages[i] & 0xFFF) == 0) mprotect(ww_pages[i], 4096, PROT_READ);
}
