/* xv_log.c - one log sink for the Vita build: sceClibPrintf (visible in Vita3K / PrincessLog) plus
 * an append-only file at ux0:data/xboxvita/xboxvita.log so a run on real hardware leaves evidence. */
#ifdef __vita__
#include <stdarg.h>
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "xv_log.h"

static SceUID g_fd = -2;        /* -2 = not opened yet, -1 = open failed (stay console-only) */
static SceUID g_mtx = -1;

void xv_logf(const char *fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    sceClibPrintf("%s", buf);

    if (g_mtx < 0) g_mtx = sceKernelCreateMutex("xv_log", 0, 0, NULL);
    if (g_mtx >= 0) sceKernelLockMutex(g_mtx, 1, NULL);
    if (g_fd == -2) {
        sceIoMkdir("ux0:data", 0777);
        sceIoMkdir("ux0:data/xboxvita", 0777);
        /* keep the last three runs: .log -> .1.log -> .2.log -> .3.log (a hardware session is evidence) */
        sceIoRemove("ux0:data/xboxvita/xboxvita.3.log");
        sceIoRename("ux0:data/xboxvita/xboxvita.2.log", "ux0:data/xboxvita/xboxvita.3.log");
        sceIoRename("ux0:data/xboxvita/xboxvita.1.log", "ux0:data/xboxvita/xboxvita.2.log");
        sceIoRename("ux0:data/xboxvita/xboxvita.log",   "ux0:data/xboxvita/xboxvita.1.log");
        g_fd = sceIoOpen("ux0:data/xboxvita/xboxvita.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    }
    if (g_fd >= 0) sceIoWrite(g_fd, buf, (SceSize)n);
    if (g_mtx >= 0) sceKernelUnlockMutex(g_mtx, 1);
}

void xv_log_flush(void) { if (g_fd >= 0) sceIoSyncByFd(g_fd, 0); }
#endif
