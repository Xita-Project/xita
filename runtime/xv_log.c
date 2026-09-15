/* xv_log.c - one log sink for the Vita build: sceClibPrintf (visible in Vita3K / PrincessLog) plus
 * an append-only file at ux0:data/xita/xita.log so a run on real hardware leaves evidence. */
#ifdef __vita__
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "xv_log.h"

static SceUID g_fd = -2;        /* -2 = not opened yet, -1 = open failed (stay console-only) */
static SceUID g_mtx = -1;
/* Used only while the presenting thread formats a periodic report. No worker,
 * deferred I/O or per-frame allocation: end writes the complete report before
 * gameplay resumes. Foreign-thread errors retain the ordinary immediate sink. */
#define XV_LOG_REPORT_BYTES 32768u
static char g_report[XV_LOG_REPORT_BYTES];
static unsigned g_report_used;
static SceUID g_report_owner;
static void log_write_immediate(const char *buf, unsigned n);

int xv_log_report_begin(void)
{
    SceUID expected=0, current=sceKernelGetThreadId();
    if (current<=0 || !__atomic_compare_exchange_n(&g_report_owner,&expected,current,
            0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
    g_report_used=0;
    return 1;
}

static int report_is_owner(void)
{
    SceUID owner=__atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE);
    return owner>0 && owner==sceKernelGetThreadId();
}

static void report_flush(void)
{
    if (g_report_used) log_write_immediate(g_report,g_report_used);
    g_report_used=0;
}

void xv_log_report_end(void)
{
    if (!report_is_owner()) return;
    report_flush();
    __atomic_store_n(&g_report_owner,0,__ATOMIC_RELEASE);
}

void xv_logf(const char *fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    xv_log_write(buf, (unsigned)n);
}

void xv_log_write(const char *buf, unsigned n)
{
    if (!buf || !n) return;
    if (report_is_owner()) {
        if (n>XV_LOG_REPORT_BYTES-g_report_used) report_flush();
        if (n<=XV_LOG_REPORT_BYTES) {
            memcpy(g_report+g_report_used,buf,n);
            g_report_used+=n;
        } else log_write_immediate(buf,n);
        return;
    }
    log_write_immediate(buf,n);
}

static void log_write_immediate(const char *buf, unsigned n)
{
    /* Keep individual console messages within the existing formatting limit.
     * The file receives the complete batch under one mutex acquisition. */
    for (unsigned at = 0; at < n;) {
        unsigned chunk = n - at;
        if (chunk > 511) chunk = 511;
        /* Emulator consoles prefix each call. Preserve complete lines where
         * possible so their prefixes cannot split a phase record in half. */
        for (unsigned i = chunk; i; --i)
            if (buf[at + i - 1] == '\n') { chunk = i; break; }
        sceClibPrintf("%.*s", (int)chunk, buf + at);
        at += chunk;
    }

    if (g_mtx < 0) g_mtx = sceKernelCreateMutex("xv_log", 0, 0, NULL);
    if (g_mtx >= 0) sceKernelLockMutex(g_mtx, 1, NULL);
    if (g_fd == -2) {
        sceIoMkdir("ux0:data", 0777);
        {   /* one-time migration from the pre-rebrand layout: ux0:data/xboxvita -> ux0:data/xita.  Game files,
             * saves and settings move with the directory; xboxvita.cfg becomes xita.cfg.  Nothing is deleted. */
            SceIoStat st_;
            if (sceIoGetstat("ux0:data/xita", &st_) < 0 && sceIoGetstat("ux0:data/xboxvita", &st_) >= 0 &&
                sceIoRename("ux0:data/xboxvita", "ux0:data/xita") >= 0)
                sceIoRename("ux0:data/xita/xboxvita.cfg", "ux0:data/xita/xita.cfg");
        }
        sceIoMkdir("ux0:data/xita", 0777);
        /* keep the last three runs: .log -> .1.log -> .2.log -> .3.log (a hardware session is evidence) */
        sceIoRemove("ux0:data/xita/xita.3.log");
        sceIoRename("ux0:data/xita/xita.2.log", "ux0:data/xita/xita.3.log");
        sceIoRename("ux0:data/xita/xita.1.log", "ux0:data/xita/xita.2.log");
        sceIoRename("ux0:data/xita/xita.log",   "ux0:data/xita/xita.1.log");
        g_fd = sceIoOpen("ux0:data/xita/xita.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    }
    if (g_fd >= 0) {
        unsigned written = 0;
        while (written < n) {
            SceSSize r = sceIoWrite(g_fd, buf + written, (SceSize)(n - written));
            if (r <= 0 || (unsigned)r > n - written) break;
            written += (unsigned)r;
        }
    }
    if (g_mtx >= 0) sceKernelUnlockMutex(g_mtx, 1);
}

void xv_log_flush(void)
{
    if (report_is_owner()) report_flush();
    if (g_fd >= 0) sceIoSyncByFd(g_fd, 0);
}
#endif
