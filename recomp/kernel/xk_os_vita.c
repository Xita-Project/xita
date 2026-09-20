/* xk_os_vita.c - PS Vita implementation of xk_os.h: sceIo files, kernel clocks, sceCtrl pad,
 * cooperative fibers with a Thumb-2 context switch (same approach as the runtime's xk_sched). */
#ifdef __vita__
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>

#include "xk_os.h"
#include "xk_read_retry.h"
#include "../../runtime/xv_cpu.h"
#include "../../runtime/xv_benchmark.h"
#include "../../runtime/xv_settings.h"
#include "../../runtime/xv_remote.h"
#include "xk.h"                 /* X_M32/X_M8, xk_file_in_ui_map (pad context) */

void xv_logf(const char *fmt, ...);        /* app-side sink (xv_log.c): console + ux0:data/xita/xita.log */
void xv_log_write(const char *text, unsigned length);
void xk_os_log_batch(const char *text, unsigned length)
{
    xv_log_write(text, length);
}
void xk_os_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    xv_logf("[xk] %s", buf);
}

/* ---- files ------------------------------------------------------------------------------ */
/* Halo pre-sizes its cache files (cache000/001.map = 291 MB, 003-005 = 49 MB) with SetEndOfFile and
 * then only ever touches the prefix it actually writes.  exFAT has no sparse files, so honouring that
 * physically would zero-fill ~770 MB of card.  Instead the LOGICAL size lives here (per path, so it
 * survives close/reopen within a run): reads past the physical end return zeros up to the logical
 * size, and a write beyond the physical end extends the file for real (sceIo zero-fills the gap). */
/* The game holds a lot of handles at once (6 cache maps, the tag cache, profile/playlist files x2 each,
 * directory objects...).  SceIofilemgr's per-process descriptor table is small - on hardware the 23rd
 * playlist's blam.lst failed to open and Halo then wrote through a -1 pointer.  So a guest handle owns
 * only path+flags; the sceIo descriptor is acquired on demand and the least recently used one is
 * evicted once XK_FD_CACHE are live.  All I/O is positional (pread/pwrite), so reopening is invisible. */
struct xk_file {
    SceUID fd; int flags; int lsize_slot; char path[128]; struct xk_file *lru_prev, *lru_next;
    int profile;
    uint64_t read_bytes, write_bytes, read_us, write_us;
    uint32_t reads, writes, reported_mb;
};
struct xk_dir  { SceUID d; char path[512]; };

#define XK_FD_CACHE 16
static xk_file *g_lru_head, *g_lru_tail;
static int g_fds_live;

static void lru_unlink(xk_file *f)
{
    if (f->lru_prev) f->lru_prev->lru_next = f->lru_next; else if (g_lru_head == f) g_lru_head = f->lru_next;
    if (f->lru_next) f->lru_next->lru_prev = f->lru_prev; else if (g_lru_tail == f) g_lru_tail = f->lru_prev;
    f->lru_prev = f->lru_next = NULL;
}
static void lru_push_front(xk_file *f)
{
    f->lru_next = g_lru_head; f->lru_prev = NULL;
    if (g_lru_head) g_lru_head->lru_prev = f; g_lru_head = f;
    if (!g_lru_tail) g_lru_tail = f;
}
static void fd_release(xk_file *f)
{
    if (f->fd < 0) return;
    sceIoClose(f->fd); f->fd = -1; g_fds_live--;
    lru_unlink(f);
}
/* Descriptor for f, (re)opening if it was evicted.  Returns <0 on failure. */
static SceUID fd_acquire(xk_file *f)
{
    if (f->fd >= 0) { lru_unlink(f); lru_push_front(f); return f->fd; }
    while (g_fds_live >= XK_FD_CACHE && g_lru_tail) fd_release(g_lru_tail);
    f->fd = sceIoOpen(f->path, f->flags, 0777);
    if (f->fd < 0) { xk_os_log("reopen %s failed %08X\n", f->path, (unsigned)f->fd); return f->fd; }
    g_fds_live++; lru_push_front(f);
    return f->fd;
}

#define LSIZE_SLOTS 32
static struct { char path[128]; int64_t size; int used; } g_lsize[LSIZE_SLOTS];

static int lsize_find(const char *path, int create)
{
    int free_slot = -1;
    for (int i = 0; i < LSIZE_SLOTS; ++i) {
        if (g_lsize[i].used) { if (strcmp(g_lsize[i].path, path) == 0) return i; }
        else if (free_slot < 0) free_slot = i;
    }
    if (!create || free_slot < 0) return -1;
    g_lsize[free_slot].used = 1; g_lsize[free_slot].size = -1;
    snprintf(g_lsize[free_slot].path, sizeof g_lsize[free_slot].path, "%s", path);
    return free_slot;
}
static int64_t phys_size(SceUID fd)
{
    SceOff cur = sceIoLseek(fd, 0, SCE_SEEK_CUR); SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, cur, SCE_SEEK_SET); return end;
}
static int64_t logical_size(xk_file *f)
{
    if (f->lsize_slot >= 0 && g_lsize[f->lsize_slot].size >= 0) return g_lsize[f->lsize_slot].size;
    return -1;
}

xk_file *xk_os_open(const char *path, int write, int create, int truncate, int *err_is_missing)
{
    int flags = write ? SCE_O_RDWR : SCE_O_RDONLY;
    if (create) flags |= SCE_O_CREAT;
    if (truncate) flags |= SCE_O_TRUNC;
    while (g_fds_live >= XK_FD_CACHE && g_lru_tail) fd_release(g_lru_tail);
    SceUID fd = sceIoOpen(path, flags, 0777);
    if (fd < 0) {
        xk_os_log("open %s (flags %X) failed %08X, %d fds live\n", path, flags, (unsigned)fd, g_fds_live);
        if (err_is_missing) *err_is_missing = 1; return NULL;
    }
    xk_file *f = calloc(1, sizeof *f); f->fd = fd;
    f->flags = flags & ~(SCE_O_CREAT | SCE_O_TRUNC);        /* a later reopen must not recreate/clobber */
    g_fds_live++; lru_push_front(f);
    snprintf(f->path, sizeof f->path, "%s", path);
    { static int profile = -1;
      if (profile < 0) { const char *e = getenv("XV_LOAD_PROFILE"); profile = e ? atoi(e) != 0 : 1; }
      size_t len = strlen(path);
      f->profile = profile && len > 4 && !strcmp(path + len - 4, ".map"); }
    f->lsize_slot = lsize_find(path, 0);
    if (truncate && f->lsize_slot >= 0) g_lsize[f->lsize_slot].size = -1;
    return f;
}
static void map_io_report(xk_file *f, int closing)
{
    uint32_t mb = (uint32_t)((f->read_bytes + f->write_bytes) >> 20);
    if (!mb || (!closing && mb < f->reported_mb + 32)) return;
    xk_os_log("[load-io] %s%s: read %llu KB / %u calls / %llu ms; write %llu KB / %u calls / %llu ms\n",
              f->path, closing ? " (close)" : "", (unsigned long long)(f->read_bytes >> 10), f->reads,
              (unsigned long long)(f->read_us / 1000), (unsigned long long)(f->write_bytes >> 10),
              f->writes, (unsigned long long)(f->write_us / 1000));
    f->reported_mb = mb;
}
void xk_os_close(xk_file *f) { if (f) { if (f->profile) map_io_report(f, 1); fd_release(f); free(f); } }
static int64_t file_read_once(void *handle, uint64_t pos, void *buf, uint32_t n)
{
    return sceIoPread(*(SceUID *)handle, buf, n, (SceOff)pos);
}
int64_t xk_os_read(xk_file *f, uint64_t pos, void *buf, uint32_t n)
{
    uint64_t started = f->profile ? sceKernelGetProcessTimeWide() : 0;
    SceUID fd = fd_acquire(f); if (fd < 0) return -1;
    unsigned retries;
    int64_t r = xk_read_retry(&fd, pos, buf, n, file_read_once, &retries);
    if (retries && r == n)
        xk_os_log("[read-retry] %s @%llu: completed %u bytes with %u staged reads\n",
                  f->path, (unsigned long long)pos, n, retries);
    if (f->profile) {
        f->read_us += sceKernelGetProcessTimeWide() - started; f->reads++;
        if (r > 0) f->read_bytes += (uint64_t)r;
        map_io_report(f, 0);
    }
    int64_t ls = logical_size(f);
    if (ls < 0) return r;
    if (r < 0) r = 0;
    if ((uint64_t)r < n && pos + (uint64_t)r < (uint64_t)ls) {                 /* zero-fill the pre-sized tail */
        uint64_t want = (uint64_t)ls - pos; if (want > n) want = n;
        memset((uint8_t *)buf + r, 0, (size_t)(want - (uint64_t)r));
        r = (int64_t)want;
    }
    return r;
}
int64_t xk_os_write(xk_file *f, uint64_t pos, const void *buf, uint32_t n)
{
    uint64_t started = f->profile ? sceKernelGetProcessTimeWide() : 0;
    SceUID fd = fd_acquire(f); if (fd < 0) return -1;
    int64_t r = sceIoPwrite(fd, buf, n, (SceOff)pos);
    if (f->profile) {
        f->write_us += sceKernelGetProcessTimeWide() - started; f->writes++;
        if (r > 0) f->write_bytes += (uint64_t)r;
        map_io_report(f, 0);
    }
    if (r > 0 && f->lsize_slot >= 0 && g_lsize[f->lsize_slot].size >= 0 && (int64_t)(pos + (uint64_t)r) > g_lsize[f->lsize_slot].size)
        g_lsize[f->lsize_slot].size = (int64_t)(pos + (uint64_t)r);
    return r;
}
int64_t xk_os_size(xk_file *f)
{
    int64_t ls = logical_size(f);
    if (ls >= 0) return ls;
    SceUID fd = fd_acquire(f); return fd < 0 ? 0 : phys_size(fd);
}
int xk_os_truncate(xk_file *f, uint64_t size)
{
    if (size > INT64_MAX) return -1;
    /* Checkpoint files must retain their reserved length after a restart. Halo
     * reinitializes any checkpoint file whose length is not 0x380000, even though its payload is only 0x345000.
     * Extending with a final zero byte preserves the existing prefix and lets
     * sceIo zero-fill the gap. Shrinking retains the existing logical behavior. */
    size_t path_len = strlen(f->path);
    int checkpoint = path_len >= 13 && !strcmp(f->path + path_len - 13, "/savegame.bin");
    if (checkpoint) {
        SceUID fd = fd_acquire(f); if (fd < 0) return -1;
        int64_t physical = phys_size(fd); if (physical < 0) return -1;
        if (size >= (uint64_t)physical) {
            uint8_t zero = 0;
            if (size > (uint64_t)physical && sceIoPwrite(fd, &zero, 1, (SceOff)(size - 1)) != 1)
                return -1;
            /* An earlier logical shrink may already have a shared size entry. */
            if (f->lsize_slot >= 0) g_lsize[f->lsize_slot].size = (int64_t)size;
            return 0;
        }
    }
    if (f->lsize_slot < 0) {
        f->lsize_slot = lsize_find(f->path, 1);
        if (f->lsize_slot < 0) { xk_os_log("truncate: logical-size table full for %s\n", f->path); return -1; }
    }
    g_lsize[f->lsize_slot].size = (int64_t)size;
    return 0;
}
int xk_os_flush(xk_file *f) { if (f->fd >= 0) sceIoSyncByFd(f->fd, 0); return 0; }
int xk_os_stat(const char *path, int *is_dir, uint64_t *size, uint64_t *mtime_100ns)
{
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return -1;
    if (is_dir) *is_dir = SCE_S_ISDIR(st.st_mode);
    if (size) *size = (uint64_t)st.st_size;
    if (mtime_100ns) { SceUInt64 t = 0; sceRtcGetTick((SceDateTime *)&st.st_mtime, (SceRtcTick *)&t); *mtime_100ns = t * 10; }
    return 0;
}
int xk_os_unlink(const char *path) { int s = lsize_find(path, 0); if (s >= 0) g_lsize[s].used = 0; return sceIoRemove(path) < 0 ? -1 : 0; }
int xk_os_mkdir(const char *path) { int r = sceIoMkdir(path, 0777); return (r < 0 && r != (int)0x80010011) ? -1 : 0; }   /* EEXIST ok */
int xk_os_rmdir(const char *path) { return sceIoRmdir(path) < 0 ? -1 : 0; }
int xk_os_rename(const char *from, const char *to) { return sceIoRename(from, to) < 0 ? -1 : 0; }
xk_dir *xk_os_opendir(const char *path)
{
    SceUID d = sceIoDopen(path);
    if (d < 0) return NULL;
    xk_dir *x = calloc(1, sizeof *x); x->d = d; snprintf(x->path, sizeof x->path, "%s", path); return x;
}
int xk_os_readdir(xk_dir *d, char *name, unsigned cap, int *is_dir, uint64_t *size)
{
    SceIoDirent e; memset(&e, 0, sizeof e);
    if (sceIoDread(d->d, &e) <= 0) return 0;
    snprintf(name, cap, "%s", e.d_name);
    *is_dir = SCE_S_ISDIR(e.d_stat.st_mode); *size = (uint64_t)e.d_stat.st_size;
    return 1;
}
void xk_os_closedir(xk_dir *d) { if (d) { sceIoDclose(d->d); free(d); } }
int xk_os_freespace(const char *path, uint64_t *free_bytes, uint64_t *total_bytes)
{
    (void)path;
    *free_bytes = 1ull << 30; *total_bytes = 4ull << 30;    /* generous defaults; ux0 querying needs SceAppMgr */
    return 0;
}

/* ---- time -------------------------------------------------------------------------------- */
uint64_t xk_os_time_100ns(void)
{
    SceRtcTick t; sceRtcGetCurrentTick(&t);                 /* microseconds since 0001-01-01 */
    return (t.tick - 50491123200000000ull) * 10;            /* 0001 -> 1601 offset in us */
}
uint64_t xk_os_monotonic_us(void) { return sceKernelGetProcessTimeWide(); }
void xk_os_sleep_us(uint64_t us) { sceKernelDelayThread(us > 0xFFFFFFFFull ? 0xFFFFFFFFu : (SceUInt)us); }

static int g_scheduler_event=-2;
int xk_os_scheduler_prepare(void)
{
    /* Called only by serialized guest execution. Publish after creation. */
    int previous=__atomic_load_n(&g_scheduler_event,__ATOMIC_ACQUIRE);
    if (previous!=-2) return previous>=0;
    int id=sceKernelCreateEventFlag("xk_completion",0,0,NULL);
    __atomic_store_n(&g_scheduler_event,id,__ATOMIC_RELEASE);
    xk_os_log("scheduler completion notifications: %s\n",id>=0?"on":"timed fallback");
    return id>=0;
}
void xk_os_scheduler_notify(void)
{
    int id=__atomic_load_n(&g_scheduler_event,__ATOMIC_ACQUIRE);
    if (id>=0) sceKernelSetEventFlag(id,1);
}
void xk_os_scheduler_wait(uint64_t us)
{
    int id=__atomic_load_n(&g_scheduler_event,__ATOMIC_ACQUIRE);
    if (id<0) { xk_os_sleep_us(us); return; }
    SceUInt timeout=us>UINT32_MAX?UINT32_MAX:(SceUInt)us;
    unsigned bits;
    int rc=sceKernelWaitEventFlag(id,1,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,&bits,&timeout);
    if (rc<0 && timeout) xk_os_sleep_us(timeout>1000?1000:timeout);
}

/* ---- fibers -------------------------------------------------------------------------------- */
/* Guest threads used to run on hand-rolled fibers (memalign'd stacks + a Thumb-2 sp swap).  Real
 * hardware killed the process on the first sceIoWrite issued from such a stack (core dump: thread
 * stopped in the SceLibKernel write stub with sp outside the thread's registered stack, stop reason
 * 0x10006) - the kernel only trusts stack-resident syscall buffers inside the stack it allocated.
 * So each "fiber" is now a real SCE thread with a kernel-owned stack, and switching is a baton
 * handoff through per-thread semaphores: exactly one thread runs at a time, same cooperative
 * semantics as before, and syscalls always see a legitimate stack. */
struct xk_fiber { SceUID thid; SceUID wake; void (*entry)(void *); void *arg; int dead; };
static xk_fiber g_main_fiber = { -1, -1, NULL, NULL, 0 };
static xk_fiber *g_current = &g_main_fiber;

static void fiber_park(xk_fiber *f)
{
    sceKernelWaitSema(f->wake, 1, NULL);
    if (f->dead) sceKernelExitThread(0);                    /* destroyed while parked: unwind the thread */
}

static int fiber_thread(SceSize args, void *argp)
{
    (void)args;
    xk_fiber *f = *(xk_fiber **)argp;
    fiber_park(f);                                          /* wait for the scheduler's first switch */
    xv_cpu_log_thread("guest-fiber");
    f->entry(f->arg);
    xk_os_log("fiber entry returned - switching to scheduler\n");
    xk_os_fiber_switch(&g_main_fiber);
    return 0;
}

xk_fiber *xk_os_fiber_create(void (*entry)(void *), void *arg, size_t host_stack)
{
    if (g_main_fiber.wake < 0) g_main_fiber.wake = sceKernelCreateSema("xk_main", 0, 0, 1, NULL);
    xk_fiber *f = calloc(1, sizeof *f);
    f->entry = entry; f->arg = arg;
    f->wake = sceKernelCreateSema("xk_fiber", 0, 0, 1, NULL);
    f->thid = sceKernelCreateThread("xk_fiber", fiber_thread, sceKernelGetThreadCurrentPriority(),
                                    (SceSize)host_stack, 0, 0, NULL);
    if (f->thid < 0) { xk_os_log("fiber: sceKernelCreateThread failed %08X\n", (unsigned)f->thid); }
    sceKernelStartThread(f->thid, sizeof f, &f);
    return f;
}
void xk_os_fiber_switch(xk_fiber *to)
{
    xk_fiber *from = g_current;
    if (from == to) return;
    { extern void xv_render_view_fiber_switch(void) __attribute__((weak));   /* a scene on the render table drops back to live before another guest thread runs */
      if (xv_render_view_fiber_switch) xv_render_view_fiber_switch(); }
    g_current = to;
    sceKernelSignalSema(to->wake, 1);
    fiber_park(from);
}
xk_fiber *xk_os_fiber_current(void) { return g_current; }
xk_fiber *xk_os_fiber_main(void) { return &g_main_fiber; }
void xk_os_fiber_destroy(xk_fiber *f)
{
    if (!f || f == &g_main_fiber) return;
    f->dead = 1;
    sceKernelSignalSema(f->wake, 1);                        /* wakes it inside fiber_park -> exits */
    sceKernelWaitThreadEnd(f->thid, NULL, NULL);
    sceKernelDeleteThread(f->thid);
    sceKernelDeleteSema(f->wake);
    free(f);
}

/* ---- input: sceCtrl -> Xbox gamepad ---------------------------------------------------------- */
/* Xbox wButtons: DPAD_UP 1, DOWN 2, LEFT 4, RIGHT 8, START 0x10, BACK 0x20, LTHUMB 0x40, RTHUMB 0x80.
 * Analog buttons[8]: A, B, X, Y, BLACK, WHITE, LTRIGGER, RTRIGGER (0..255). */
enum { PAD_BLACK = 1, PAD_WHITE = 2, PAD_L3 = 4, PAD_R3 = 8 };
static struct { int touch, rear_touch, swap, deadzone, sens, curve, invert; } g_pad_cfg;

static unsigned pad_extra_token(const char *token)
{
    if (!strcmp(token, "black")) return PAD_BLACK;
    if (!strcmp(token, "white")) return PAD_WHITE;
    if (!strcmp(token, "l3")) return PAD_L3;
    if (!strcmp(token, "r3")) return PAD_R3;
    return 0;
}

static int pad_setting(const char *key, int def, int lo, int hi)
{
    const char *e = getenv(key);
    int v = e ? atoi(e) : def;
    return v < lo ? lo : v > hi ? hi : v;
}

static int16_t pad_axis(int v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
}

static void pad_stick(unsigned raw_x, unsigned raw_y, int deadzone, int sens,
                      int curve, int invert, int16_t *out_x, int16_t *out_y)
{
    int x = pad_axis(((int)raw_x - 128) * 256);
    int y = pad_axis(-(((int)raw_y - 128) * 256));
    /* Preserve the old integer mapping exactly, including its asymmetric endpoints.
     * Curve 1 is also linear: the previous Vita mapping had no acceleration curve. */
    if (deadzone || curve == 2 || sens != 100) {
        float fx = x, fy = y;
        float radius = sqrtf(fx * fx + fy * fy);
        float dz = 32767.0f * deadzone / 100.0f;
        if (radius <= dz) { x = y = 0; }
        else {
            float magnitude = fminf(radius / 32767.0f, 1.0f);
            float scale = 1.0f;
            if (deadzone) {
                magnitude = (magnitude - deadzone / 100.0f) / (1.0f - deadzone / 100.0f);
                scale = magnitude * 32767.0f / radius;
            }
            if (curve == 2) scale *= magnitude;
            scale *= sens / 100.0f;
            x = pad_axis((int)(fx * scale)); y = pad_axis((int)(fy * scale));
        }
    }
    *out_x = x; *out_y = invert ? -y : y;
}

static void pad_init(void)
{
    static int init;
    if (init) return;
    init = 1;
    g_pad_cfg.touch = pad_setting("XV_TOUCH", 1, 0, 1);
    g_pad_cfg.rear_touch = pad_setting("XV_REAR_TOUCH", 0, 0, 1);
    g_pad_cfg.swap = pad_setting("XV_TOUCH_SWAP", 0, 0, 1);
    g_pad_cfg.deadzone = pad_setting("XV_DEADZONE", 0, 0, 99);
    g_pad_cfg.sens = pad_setting("XV_LOOK_SENS", 100, 0, 400);
    g_pad_cfg.curve = pad_setting("XV_LOOK_CURVE", 0, 0, 2);
    g_pad_cfg.invert = pad_setting("XV_INVERT_Y", 0, 0, 1);
    /* Exhaustive default-path regression check, once at the first poll after cfg load. */
    int ok = 1;
    for (int v = 0; v < 256; ++v) {
        int16_t x, y;
        pad_stick(v, v, 0, 100, 0, 0, &x, &y);
        int old_x = (v - 128) * 256, old_y = -(v - 128) * 256;
        if (old_x < -32767) old_x = -32767;
        if (old_y > 32767) old_y = 32767;
        if (x != old_x || y != old_y) ok = 0;
    }
    xv_logf("[xk] pad: XV_TOUCH=%d XV_REAR_TOUCH=%d XV_TOUCH_SWAP=%d XV_DEADZONE=%d XV_LOOK_SENS=%d XV_LOOK_CURVE=%d XV_INVERT_Y=%d default-axis-check=%s\n",
            g_pad_cfg.touch, g_pad_cfg.rear_touch, g_pad_cfg.swap, g_pad_cfg.deadzone, g_pad_cfg.sens,
            g_pad_cfg.curve, g_pad_cfg.invert, ok ? "PASS" : "FAIL");
}

static unsigned pad_touch(void)
{
    static int init, ready[2];
    unsigned extras = 0;
    if (!g_pad_cfg.touch) return 0;
    if (!init) {
        init = 1;
        ready[SCE_TOUCH_PORT_FRONT] = sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START) >= 0;
        if (g_pad_cfg.rear_touch)
            ready[SCE_TOUCH_PORT_BACK] = sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START) >= 0;
    }
    for (unsigned port = SCE_TOUCH_PORT_FRONT; port <= SCE_TOUCH_PORT_BACK; ++port) {
        SceTouchData t = {0};
        if (!ready[port] || sceTouchPeek(port, &t, 1) <= 0) continue;
        for (unsigned i = 0; i < t.reportNum && i < SCE_TOUCH_MAX_REPORT; ++i) {
            int x = t.report[i].x, y = t.report[i].y;
            if (x < 0 || x >= 1920 || y < 0 || y >= 1088) continue;
            if (port == SCE_TOUCH_PORT_BACK)
                extras |= ((x < 960) ^ g_pad_cfg.swap) ? PAD_BLACK : PAD_WHITE;
            else if (y >= 888) {
                if (x < 300) extras |= PAD_L3;
                if (x >= 1620) extras |= PAD_R3;
            }
        }
    }
    return extras;
}

/* Optional automatic trace annotation; never controls screenshot capture. */
#include "xk_diag_poll.h"
void xk_os_pad_poll(xk_os_pad *p)
{
    xv_diag_poll_shot();
    /* DIAGNOSTIC (XV_ERR_LOG=1): log Halo's pending UI-error codes when they change.  2E4028 = network
     * error slot (code 6 -> "A networking error has occurred"); 2E4030 = saved-game error slot. */
    { static int on = -1; if (on < 0) { const char *e = getenv("XV_ERR_LOG"); on = e ? atoi(e) : 0; }
      if (on) { extern uint32_t xv_guest_r16(uint32_t); static uint32_t last = 0xEEEEEEEEu;
        uint32_t a = xv_guest_r16(0x2E4028u), b = xv_guest_r16(0x2E4030u), cur = a | (b << 16);
        if (cur != last) { xv_logf("[err] pending net(2E4028)=%04X saved(2E4030)=%04X\n", a, b); last = cur; } } }
    pad_init();
    unsigned extras = pad_touch();
    SceCtrlData d; memset(&d, 0, sizeof d);
    d.lx = d.ly = d.rx = d.ry = 128;                                  /* centred if no pad answers */
    { static int mode_set; if (!mode_set) { mode_set = 1; sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);   /* real firmware defaults to DIGITAL: sticks read 128 forever */
        xv_logf("[xk] pad: analog sampling mode set\n"); } }
    sceCtrlPeekBufferPositive(0, &d, 1);
    xv_remote_pad(&d.buttons, &d.lx, &d.ly, &d.rx, &d.ry);
    memset(p, 0, sizeof *p); p->connected = 1;
    uint16_t b = 0;
    /* Scripted input for emulator runs: ux0:data/xita/pad.txt holds "frame:input[*hold]" items (frame =
     * game frame / Present count; hold in frames, default 4).  input = up/down/left/right/start/back/a/b/x/y/
     * l/r/black/white/l3/r3, or a stick: lup/ldown/lleft/lright/rup/rdown/rleft/rright (full deflection).  Absent on a real
     * card, so hardware always sees the physical pad. */
    {
        static int init; static struct { unsigned at, hold; char btn[8]; } ev[512]; static int nev; static unsigned polls;
        if (!init) {
            init = 1; SceUID fd = sceIoOpen("ux0:data/xita/pad.txt", SCE_O_RDONLY, 0);
            if (fd >= 0) { static char txt[16384]; int n = sceIoRead(fd, txt, sizeof txt - 1); sceIoClose(fd); if (n < 0) n = 0; txt[n] = 0;
                const char *e = txt; while (*e && nev < 512) { unsigned at, hold = 4; char bn[8]; int k; if (sscanf(e, "%u:%7[a-z0-9]%n", &at, bn, &k) < 2) break; e += k;
                    if (*e == '*') { int k2; if (sscanf(e, "*%u%n", &hold, &k2) >= 1) e += k2; }
                    ev[nev].at = at; ev[nev].hold = hold; snprintf(ev[nev].btn, 8, "%s", bn); nev++; while (*e == ',' || *e == '\n' || *e == ' ') e++; }
                xv_logf("[xk] pad script: %d events\n", nev); }
        }
        /* index by game frame (Present count), not by poll: the game polls the pad more than once per frame */
        extern unsigned xd3d_frame(void) __attribute__((weak));
        { extern unsigned xd3d_pad_frame(void); unsigned gf = xd3d_pad_frame(); polls = gf ? gf : (xd3d_frame ? xd3d_frame() : polls + 1); }
        for (int i = 0; i < nev; ++i) if (polls >= ev[i].at && polls < ev[i].at + ev[i].hold) {
            const char *bn = ev[i].btn;
            extras |= pad_extra_token(bn);
            if (!strcmp(bn, "up")) d.buttons |= SCE_CTRL_UP; else if (!strcmp(bn, "down")) d.buttons |= SCE_CTRL_DOWN;
            else if (!strcmp(bn, "left")) d.buttons |= SCE_CTRL_LEFT; else if (!strcmp(bn, "right")) d.buttons |= SCE_CTRL_RIGHT;
            else if (!strcmp(bn, "start")) d.buttons |= SCE_CTRL_START; else if (!strcmp(bn, "back")) d.buttons |= SCE_CTRL_SELECT;
            else if (!strcmp(bn, "a")) d.buttons |= SCE_CTRL_CROSS; else if (!strcmp(bn, "b")) d.buttons |= SCE_CTRL_CIRCLE;
            else if (!strcmp(bn, "x")) d.buttons |= SCE_CTRL_SQUARE; else if (!strcmp(bn, "y")) d.buttons |= SCE_CTRL_TRIANGLE;
            else if (!strcmp(bn, "l")) d.buttons |= SCE_CTRL_LTRIGGER; else if (!strcmp(bn, "r")) d.buttons |= SCE_CTRL_RTRIGGER;
            else if (!strcmp(bn, "lup")) d.ly = 0; else if (!strcmp(bn, "ldown")) d.ly = 255;
            else if (!strcmp(bn, "lleft")) d.lx = 0; else if (!strcmp(bn, "lright")) d.lx = 255;
            else if (!strcmp(bn, "rup")) d.ry = 0; else if (!strcmp(bn, "rdown")) d.ry = 255;
            else if (!strcmp(bn, "rleft")) d.rx = 0; else if (!strcmp(bn, "rright")) d.rx = 255;
            else if (!strcmp(bn, "force")) p->force_start = 1;                                  /* force MP start (XV_FORCE_START) */
            else if (bn[0] == 'p' && bn[1] == '2') {                                         /* virtual player 2 (XV_PAD2=1) */
                const char *q = bn + 2;
                if (!strcmp(q, "up")) p->p2_buttons |= 1; else if (!strcmp(q, "down")) p->p2_buttons |= 2;
                else if (!strcmp(q, "left")) p->p2_buttons |= 4; else if (!strcmp(q, "right")) p->p2_buttons |= 8;
                else if (!strcmp(q, "start")) p->p2_buttons |= 0x10; else if (!strcmp(q, "back")) p->p2_buttons |= 0x20;
                else if (!strcmp(q, "a")) p->p2_analog[0] = 255; else if (!strcmp(q, "b")) p->p2_analog[1] = 255;
                else if (!strcmp(q, "x")) p->p2_analog[2] = 255; else if (!strcmp(q, "y")) p->p2_analog[3] = 255;
            }
        }
        /* Pad recorder / raw replay.  XV_PAD_REC=1 (env.txt or xita.cfg) appends one "frame lx ly rx ry buttons [black white l3 r3]"
         * line to ux0:data/xita/pad_rec.txt every time the pad state changes (frame = Present count, the same
         * index pad.txt uses).  Copy that file to pad_play.txt and the state is fed back in at the same frames, each
         * line holding until the next one - so a play session recorded on the emulator (or on the Vita, from the
         * card) can be re-run unattended.  Timing is per rendered frame, so a replay only lines up on a build that
         * renders at about the same rate as the recording. */
        {
            static int rec = -1; static SceUID rfd = -1; static SceCtrlData last; static char rbuf[4096]; static int rlen; static unsigned rflush, last_extras;
            static char *play; static int plen, ppos, pinit; static unsigned pnext; static SceCtrlData pstate; static int pactive; static unsigned pextras;
            if (!pinit) { pinit = 1; SceUID fd = sceIoOpen("ux0:data/xita/pad_play.txt", SCE_O_RDONLY, 0);
                if (fd >= 0) { SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END); sceIoLseek(fd, 0, SCE_SEEK_SET);
                    play = malloc((size_t)sz + 1); plen = play ? sceIoRead(fd, play, (SceSize)sz) : 0; if (plen < 0) plen = 0; if (play) play[plen] = 0; sceIoClose(fd);
                    pnext = play && sscanf(play, "%u", &pnext) == 1 ? pnext : 0xFFFFFFFFu;
                    xv_logf("[xk] pad replay: %d bytes, first frame %u\n", plen, pnext); } }
            if (play) {
                while (polls >= pnext && ppos < plen) {                      /* consume every line due by now */
                    unsigned f, lx, ly, rx, ry, bt; int k;
                    if (sscanf(play + ppos, "%u %u %u %u %u %x%n", &f, &lx, &ly, &rx, &ry, &bt, &k) < 6) { ppos = plen; pnext = 0xFFFFFFFFu; break; }
                    ppos += k; pextras = 0;
                    /* Optional names only on this line; six-column recordings still mean no extras. */
                    while (ppos < plen && play[ppos] != '\n' && play[ppos] != '\r') {
                        if (play[ppos] == ' ' || play[ppos] == '\t') { ppos++; continue; }
                        char token[8]; unsigned n = 0;
                        while (ppos < plen && play[ppos] != ' ' && play[ppos] != '\t' &&
                               play[ppos] != '\n' && play[ppos] != '\r') {
                            if (n < sizeof token - 1) token[n++] = play[ppos];
                            ppos++;
                        }
                        token[n] = 0; pextras |= pad_extra_token(token);
                    }
                    while (ppos < plen && (play[ppos] == '\n' || play[ppos] == '\r' || play[ppos] == ' ')) ppos++;
                    pstate.lx = lx; pstate.ly = ly; pstate.rx = rx; pstate.ry = ry; pstate.buttons = bt; pactive = 1;
                    pnext = (ppos < plen && sscanf(play + ppos, "%u", &f) == 1) ? f : 0xFFFFFFFFu;
                }
                if (pactive) { extras |= pextras; d.buttons |= pstate.buttons;                  /* the physical pad still adds on top */
                    if (pstate.lx < 64 || pstate.lx > 192 || pstate.ly < 64 || pstate.ly > 192) { d.lx = pstate.lx; d.ly = pstate.ly; }
                    if (pstate.rx < 64 || pstate.rx > 192 || pstate.ry < 64 || pstate.ry > 192) { d.rx = pstate.rx; d.ry = pstate.ry; } }
            }
            if (rec < 0) { const char *e = getenv("XV_PAD_REC"); rec = e ? atoi(e) : 0;
                if (rec) { rfd = sceIoOpen("ux0:data/xita/pad_rec.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777); xv_logf("[xk] pad record: fd %d\n", (int)rfd); memset(&last, 0xFF, sizeof last); } }
            if (rfd >= 0) {
                if (d.lx != last.lx || d.ly != last.ly || d.rx != last.rx || d.ry != last.ry || d.buttons != last.buttons || extras != last_extras) {
                    last = d; last_extras = extras;
                    rlen += snprintf(rbuf + rlen, sizeof rbuf - rlen, "%u %u %u %u %u %x%s%s%s%s\n",
                                     polls, d.lx, d.ly, d.rx, d.ry, d.buttons,
                                     extras & PAD_BLACK ? " black" : "", extras & PAD_WHITE ? " white" : "",
                                     extras & PAD_L3 ? " l3" : "", extras & PAD_R3 ? " r3" : ""); }
                if (rlen > (int)sizeof rbuf - 96 || (rlen && polls - rflush > 120)) { sceIoWrite(rfd, rbuf, rlen); rlen = 0; rflush = polls; }
            }
        }
    }
    /* SELECT+CIRCLE opens Xita's graphics panel. Its controls never reach Halo;
     * no guest pause state is patched. Halo can be paused with START first. */
    { extern int xv_settings_input(uint32_t,uint64_t) __attribute__((weak));
      if(xv_settings_input) {
          uint32_t input=0;
          const unsigned masks[]={SCE_CTRL_UP,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE};
          for(unsigned i=0;i<6;i++)if(d.buttons&masks[i])input|=1u<<i;
          if(d.ly<80)input|=XV_DASH_UP;
          if(d.ly>176)input|=XV_DASH_DOWN;
          if(d.lx<80)input|=XV_DASH_LEFT;
          if(d.lx>176)input|=XV_DASH_RIGHT;
          if((d.buttons&(SCE_CTRL_SELECT|SCE_CTRL_CIRCLE))==(SCE_CTRL_SELECT|SCE_CTRL_CIRCLE))input=XV_SETTINGS_TOGGLE;
          if(xv_settings_input(input,sceKernelGetProcessTimeWide())) {
              xv_benchmark_remote_poll(0);
              memset(p,0,sizeof(*p));p->connected=1;return;
          }
      }
    }
    /* Auto-advance past the attract screen to the main menu: pulse Start, then A, on a slow cycle
     * (Vita3K keyboard mapping is unreliable to script; real pad input still works and overrides). */
    /* SELECT+START held ~1 s toggles the debug overlay; the pair is swallowed while held so the game does
     * not see a pause/back. */
    { static unsigned both; extern int g_xv_overlay_on;
      if ((d.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) == (SCE_CTRL_START | SCE_CTRL_SELECT)) {
          if (++both == 45) g_xv_overlay_on = g_xv_overlay_on > 0 ? 0 : 1;
          d.buttons &= ~(SCE_CTRL_START | SCE_CTRL_SELECT);
      } else both = 0; }
    /* L+R+Select compares resolution; L+R+Square compares the current optimization candidate.
     * Either chord cancels an active test. Menu input remains
     * available when no test is running; a test's ordinary input is neutral. */
    { static int held;
      unsigned chord=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SELECT;
      unsigned cpu_chord=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SQUARE;
      uint32_t gg=X_M32(0x2F8CA0u);
      int control=!xk_file_in_ui_map && gg && X_M8(gg) && X_M8(gg+1) && !X_M8(gg+2) && !X_M32(0x2E4000u);
      xv_benchmark_remote_poll(control);
      int cpu_pressed=(d.buttons&cpu_chord)==cpu_chord;
      int pressed=(d.buttons&chord)==chord || cpu_pressed;
      if(pressed&&!held&&(control||xv_benchmark_active())) {
          if(cpu_pressed)xv_benchmark_compare_toggle();else xv_benchmark_toggle();
      }
      held=pressed;
      if(xv_benchmark_active()) {memset(p,0,sizeof *p);p->connected=1;return;}
      if(pressed&&control)d.buttons&=~(chord|cpu_chord);
    }
    /* Hardware chord for the virtual second pad (XV_PAD2=1 in xita.cfg): with L+R held, START / X / O /
     * D-pad go to player 2 instead of player 1 - enough to join a split-screen lobby and pick a profile. */
    if ((d.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) == (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
        if (d.buttons & SCE_CTRL_TRIANGLE) { p->force_start = 1; d.buttons &= ~SCE_CTRL_TRIANGLE; }   /* L+R+Triangle: force the MP match start (XV_FORCE_START) */
        if (d.buttons & SCE_CTRL_UP) p->p2_buttons |= 1;      if (d.buttons & SCE_CTRL_DOWN) p->p2_buttons |= 2;
        if (d.buttons & SCE_CTRL_LEFT) p->p2_buttons |= 4;    if (d.buttons & SCE_CTRL_RIGHT) p->p2_buttons |= 8;
        if (d.buttons & SCE_CTRL_START) p->p2_buttons |= 0x10; if (d.buttons & SCE_CTRL_SELECT) p->p2_buttons |= 0x20;
        if (d.buttons & SCE_CTRL_CROSS) p->p2_analog[0] = 255; if (d.buttons & SCE_CTRL_CIRCLE) p->p2_analog[1] = 255;
        d.buttons &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT |
                       SCE_CTRL_START | SCE_CTRL_SELECT | SCE_CTRL_CROSS | SCE_CTRL_CIRCLE);
    }
    if (d.buttons & SCE_CTRL_UP) b |= 1;
    if (d.buttons & SCE_CTRL_DOWN) b |= 2;
    if (d.buttons & SCE_CTRL_LEFT) b |= 4;
    if (d.buttons & SCE_CTRL_RIGHT) b |= 8;
    if (d.buttons & SCE_CTRL_START) b |= 0x10;
    if (d.buttons & SCE_CTRL_SELECT) b |= 0x20;
    /* The Vita has no Black/White buttons and no stick clicks.  Halo's default layout never reads the
     * D-pad in gameplay (menus do), so the D-pad doubles as those four: the D-pad bits stay set for the
     * UI and the Xbox-only inputs ride along - the game ignores whichever set is irrelevant.
     *   down  -> left stick click  (crouch)        up    -> right stick click (zoom)
     *   right -> White             (flashlight)    left  -> Black            (switch grenade) */
    /* ...but only while the player is in control: Halo's in-game UI reads the extra bits as a cancel (a
     * D-pad press closed the pause menu), so with a UI screen up - main menu (ui.map) or the pause menu
     * (game_globals.paused_by_ui) - the D-pad stays a plain D-pad. Multiplayer
     * does not pause simulation, so also check player 0's current UI root,
     * managed by Halo 3925's D0B20/D1540 screen lifecycle. */
    {
        uint32_t gg = X_M32(0x2F8CA0u);
        uint32_t menu = X_M32(0x2E4000u);
        int in_control = !xk_file_in_ui_map && !menu && gg && !X_M8(gg + 2);
        { static unsigned n, m; int defl = (d.lx < 64 || d.lx > 192 || d.ly < 64 || d.ly > 192 || d.rx < 64 || d.rx > 192 || d.ry < 64 || d.ry > 192);
          if ((n++ % 600) == 0 || (defl && (m++ % 30) == 0)) xv_logf("[xk] pad raw lx %u ly %u rx %u ry %u buttons %08X | ui_map %d paused %d menu %08X -> extras %d\n", d.lx, d.ly, d.rx, d.ry, d.buttons, xk_file_in_ui_map, gg ? X_M8(gg + 2) : -1, menu, in_control); }
        if (in_control) {
            b &= ~0xFu;                                              /* in-game the D-pad bits are NOT passed: the game does move the player with them */
            if (d.buttons & SCE_CTRL_DOWN) b |= 0x40;                /* LTHUMB */
            if (d.buttons & SCE_CTRL_UP) b |= 0x80;                  /* RTHUMB */
            p->analog[4] = (d.buttons & SCE_CTRL_LEFT) ? 255 : 0;   /* BLACK */
            p->analog[5] = (d.buttons & SCE_CTRL_RIGHT) ? 255 : 0;  /* WHITE */
        }
    }
    if (extras & PAD_L3) b |= 0x40;
    if (extras & PAD_R3) b |= 0x80;
    if (extras & PAD_BLACK) p->analog[4] = 255;
    if (extras & PAD_WHITE) p->analog[5] = 255;
    p->buttons = b;
    p->analog[0] = (d.buttons & SCE_CTRL_CROSS) ? 255 : 0;      /* A */
    p->analog[1] = (d.buttons & SCE_CTRL_CIRCLE) ? 255 : 0;     /* B */
    p->analog[2] = (d.buttons & SCE_CTRL_SQUARE) ? 255 : 0;     /* X */
    p->analog[3] = (d.buttons & SCE_CTRL_TRIANGLE) ? 255 : 0;   /* Y */
    p->analog[6] = (d.buttons & SCE_CTRL_LTRIGGER) ? 255 : 0;   /* left trigger */
    p->analog[7] = (d.buttons & SCE_CTRL_RTRIGGER) ? 255 : 0;   /* right trigger */
    pad_stick(d.lx, d.ly, g_pad_cfg.deadzone, 100, 0, 0, &p->lx, &p->ly);
    pad_stick(d.rx, d.ry, g_pad_cfg.deadzone, g_pad_cfg.sens,
              g_pad_cfg.curve, g_pad_cfg.invert, &p->rx, &p->ry);
}

/* ---- audio sink: sceAudioOut main port, one grain per write (blocking = pacing) ------------------ */
#include <psp2/audioout.h>
#include "xk_audio.h"
static int g_audio_port = -1;
static SceUID g_audio_mutex = -1;
int xk_os_audio_open(int rate, int grain)
{
    g_audio_mutex = sceKernelCreateMutex("xv_audio", 0, 0, NULL);
    g_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, grain, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (g_audio_port < 0) { xv_logf("[xk] audio port open failed 0x%08X\n", g_audio_port); return -1; }
    xv_logf("[xk] audio port %d open: %d Hz stereo, grain %d\n", g_audio_port, rate, grain);
    int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
    sceAudioOutSetVolume(g_audio_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    return 0;
}
void xk_os_audio_write(const int16_t *stereo, int frames)
{
    (void)frames;
    if (g_audio_port >= 0) sceAudioOutOutput(g_audio_port, stereo);      /* blocks until the previous grain drained */
    else sceKernelDelayThread(23000);
}
/* sceKernelStartThread copies the argument block, so each thread gets its own entry pointer - a shared
 * static here made the profiler and the mixer race for the same slot (two mixers = buzzing audio). */
static int audio_thread_main(SceSize args, void *argp)
{
    (void)args;
    void (*fn)(void *) = *(void (**)(void *))argp;
    xv_cpu_log_thread("audio-or-profiler-worker");
    fn(NULL);
    return 0;
}
int xk_os_audio_thread_start(void (*fn)(void *), void *arg)
{
    (void)arg; static unsigned nth; char name[16]; snprintf(name, sizeof name, "xv_worker%u", nth++);
    SceUID t = sceKernelCreateThread(name, audio_thread_main, 64, 64 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_ALL, NULL);
    if (t < 0) { xv_logf("[xk] thread %s create failed 0x%08X\n", name, t); return -1; }
    void (*fnv)(void *) = fn;
    int r = sceKernelStartThread(t, sizeof fnv, &fnv);
    xv_logf("[xk] audio thread %s (0x%08X)\n", r < 0 ? "START FAILED" : "running", r);
    return r < 0 ? -1 : 0;
}
void xk_os_audio_mutex_lock(void)   { if (g_audio_mutex >= 0) sceKernelLockMutex(g_audio_mutex, 1, NULL); }
void xk_os_audio_mutex_unlock(void) { if (g_audio_mutex >= 0) sceKernelUnlockMutex(g_audio_mutex, 1); }
#endif /* __vita__ */
