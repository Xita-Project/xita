/* Worker pool for the software menu rasterizer (Vita). Large triangles are cut
 * into row bands: each worker rasterizes one band while the calling thread does
 * the last, then the caller waits for all of them. Rows never share pixels, so
 * colour/depth writes are disjoint; the combiner is pure per fragment. */
#include "menu_raster.h"
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

extern void xv_logf(const char *, ...);

enum { MAX_WORKERS = 3 };
typedef struct { menu_raster_band_fn fn; const void *job; int32_t y0, y1; } band_slot;
static volatile band_slot slots[MAX_WORKERS];
static SceUID go[MAX_WORKERS], done, threads[MAX_WORKERS];
static int indices[MAX_WORKERS], nworkers;

static int worker_main(SceSize args, void *argp)
{
    (void)args;
    int i = *(const int *)argp;
    for (;;) {
        sceKernelWaitSema(go[i], 1, NULL);
        band_slot s = { slots[i].fn, slots[i].job, slots[i].y0, slots[i].y1 };
        s.fn(s.job, s.y0, s.y1, 0);
        sceKernelSignalSema(done, 1);
    }
    return 0;
}

static void run_parallel(menu_raster_band_fn fn, const void *job, int32_t y0, int32_t y1)
{
    int parts = nworkers + 1, launched = 0;
    int32_t rows = y1 - y0 + 1, per = (rows + parts - 1) / parts, y = y0;
    for (int i = 0; i < nworkers && y <= y1; ++i) {
        int32_t e = y + per - 1;
        if (e > y1) e = y1;
        slots[i].fn = fn; slots[i].job = job; slots[i].y0 = y; slots[i].y1 = e;
        sceKernelSignalSema(go[i], 1);
        ++launched; y = e + 1;
    }
    if (y <= y1) fn(job, y, y1, 1);                   /* caller takes the last band */
    for (int i = 0; i < launched; ++i) sceKernelWaitSema(done, 1, NULL);
}

/* Create n worker threads (1..3) and install the parallel executor. 0 = inline. */
int menu_raster_pool_install(int n)
{
    if (n <= 0) { menu_raster_parallel = NULL; return 0; }
    if (n > MAX_WORKERS) n = MAX_WORKERS;
    done = sceKernelCreateSema("h2_menu_raster_done", 0, 0, MAX_WORKERS, NULL);
    if (done < 0) return -1;
    for (int i = 0; i < n; ++i) {
        char name[32];
        snprintf(name, sizeof name, "h2_menu_raster_%d", i);
        go[i] = sceKernelCreateSema(name, 0, 0, 1, NULL);
        if (go[i] < 0) return -1;
        indices[i] = i;
        threads[i] = sceKernelCreateThread(name, worker_main, 100, 64 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_ALL, NULL);
        if (threads[i] < 0 || sceKernelStartThread(threads[i], sizeof indices[i], &indices[i]) < 0) return -1;
    }
    nworkers = n;
    menu_raster_parallel = run_parallel;
    xv_logf("[h2/menu-render] raster worker pool: %d worker threads + caller\n", n);
    return n;
}
