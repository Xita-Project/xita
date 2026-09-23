/* psp2_host.c - Linux stand-ins for the Vita SDK calls the Halo 2 runtime makes, so games/halo2_5849
 * (boot.c, the host-channel NV2A consumer, the pass/GXM backends, the audio worker) runs unchanged as a
 * headless Linux process: x86 for fast iteration, 32-bit ARM (the Raspberry Pi bench) as a CPU proxy.
 * Built by tools/h2_host_build.py, which compiles the stage's own units against the vitasdk headers.
 *
 * kernel   memblocks = mmap; threads = pthreads (named); semaphores/mutexes = pthread mutex + condvar;
 *          process/system time = CLOCK_MONOTONIC.
 * io       sceIo* = POSIX, paths verbatim. The run directory holds a "ux0:data" directory and "app0:<file>" links
 *          (tools/h2_host_run.sh), so ux0:data/... and app0:<file> resolve relative to it, fopen() included.
 * display  a 60 Hz vblank counted from the monotonic clock. SetFrameBuf counts flips: the pad-script clock,
 *          a [host] progress line every 60 flips, sampler windows (XV_HOST_SAMPLE), XV_HOST_SHOT=<n> writes
 *          every n-th flip as shot-<flip>.ppm, XV_HOST_STATUS=<file> gets "<flip> <seconds>" every 10 flips.
 * ctrl     XV_PAD="<flip>:<button>[*<hold flips>],..." (hold default 30); "t<seconds>:<button>[*<hold s>]"
 *          for wall-clock events; XV_PAD_FILE=<file> holds whatever buttons the file names (re-read every
 *          100 ms), so a driver script can react to the log. Buttons: up down left right start back a b x y
 *          l r and stick directions lup ldown lleft lright rup rdown rleft rright.
 * audio    a port consumes one buffer per len/freq of wall time; Output blocks while a buffer is queued
 *          ahead of the playing one (the hardware queue); submits onto an already drained port are counted.
 * GXM      no GPU. Calls succeed; programs are the real .gxp files and their parameter tables are parsed,
 *          so FindParameterByName/GetResourceIndex/GetArraySize answer exactly; uniform writes land in
 *          scratch buffers; scene-end notifications are written at once. Rendered surfaces keep whatever
 *          the CPU put there (no pixels are produced), so readbacks see cleared/uploaded data only.
 * Timing   host numbers are not Vita numbers; the Pi has no GPU cost at all. Use it for structure,
 *          counters, hangs, races and the CPU split. */
#define _GNU_SOURCE
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/error.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern void xv_logf(const char *fmt, ...);
extern void xv_host_sample_start(void);
extern void xv_host_sample_dump(unsigned frame);

/* ---- time ------------------------------------------------------------------------------------------ */
static uint64_t g_start_us;
static uint64_t mono_us(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static void sleep_until_us(uint64_t deadline)
{
    struct timespec ts = { (time_t)(deadline / 1000000u), (long)(deadline % 1000000u) * 1000 };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
}
static void deadline_ts(struct timespec *ts, uint64_t us_from_now)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    uint64_t ns = (uint64_t)ts->tv_nsec + us_from_now * 1000u;
    ts->tv_sec += (time_t)(ns / 1000000000u); ts->tv_nsec = (long)(ns % 1000000000u);
}
SceUInt64 sceKernelGetProcessTimeWide(void) { return mono_us() - g_start_us; }
SceInt64 sceKernelGetSystemTimeWide(void) { return (SceInt64)mono_us(); }
int sceKernelDelayThread(SceUInt delay)
{
    if (!delay) { sched_yield(); return 0; }
    sleep_until_us(mono_us() + delay);
    return 0;
}

/* ---- kernel objects -------------------------------------------------------------------------------- */
enum { K_FREE, K_MEMBLOCK, K_THREAD, K_SEMA, K_MUTEX };
typedef struct {
    int kind;
    union {
        struct { void *base; size_t size; } mem;
        struct { SceKernelThreadEntry entry; pthread_t th; int started, joined, status; void *args; SceSize arglen; size_t stack; char name[32]; } thr;
        struct { pthread_mutex_t m; pthread_cond_t c; int count, max; } sema;
        struct { pthread_mutex_t m; pthread_cond_t c; pthread_t owner; int count, recursive; } mutex;
    } u;
} kobj;
enum { MAX_OBJ = 1024, UID_BASE = 0x10000 };
static kobj g_obj[MAX_OBJ];
static pthread_mutex_t g_obj_lock = PTHREAD_MUTEX_INITIALIZER;
static SceUID obj_new(int kind, kobj **out)
{
    pthread_mutex_lock(&g_obj_lock);
    for (int i = 1; i < MAX_OBJ; ++i) if (g_obj[i].kind == K_FREE) {
        memset(&g_obj[i], 0, sizeof g_obj[i]); g_obj[i].kind = kind; *out = &g_obj[i];
        pthread_mutex_unlock(&g_obj_lock);
        return UID_BASE + i;
    }
    pthread_mutex_unlock(&g_obj_lock);
    return (SceUID)SCE_KERNEL_ERROR_NO_MEMORY;
}
static kobj *obj_get(SceUID id, int kind)
{
    int i = id - UID_BASE;
    return i > 0 && i < MAX_OBJ && g_obj[i].kind == kind ? &g_obj[i] : NULL;
}
static void obj_free(kobj *o) { pthread_mutex_lock(&g_obj_lock); o->kind = K_FREE; pthread_mutex_unlock(&g_obj_lock); }
static void cond_init_monotonic(pthread_cond_t *c)
{
    pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(c, &a); pthread_condattr_destroy(&a);
}

SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
    (void)type; (void)opt;
    size_t bytes = ((size_t)size + 4095u) & ~(size_t)4095u;
    void *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED) { xv_logf("[host] memblock %s (%u bytes) failed\n", name ? name : "?", (unsigned)size); return (SceUID)SCE_KERNEL_ERROR_NO_MEMORY; }
    kobj *o; SceUID id = obj_new(K_MEMBLOCK, &o);
    if (id < 0) { munmap(base, bytes); return id; }
    o->u.mem.base = base; o->u.mem.size = bytes;
    return id;
}
int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
    kobj *o = obj_get(uid, K_MEMBLOCK);
    if (!o || !base) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    *base = o->u.mem.base; return 0;
}
int sceKernelFreeMemBlock(SceUID uid)
{
    kobj *o = obj_get(uid, K_MEMBLOCK);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    munmap(o->u.mem.base, o->u.mem.size); obj_free(o); return 0;
}

static void *thread_main(void *arg)
{
    kobj *o = arg;
    char name[16]; snprintf(name, sizeof name, "%s", o->u.thr.name);
    pthread_setname_np(pthread_self(), name);
    o->u.thr.status = o->u.thr.entry(o->u.thr.arglen, o->u.thr.args);
    return NULL;
}
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
                             SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option)
{
    (void)initPriority; (void)attr; (void)cpuAffinityMask; (void)option;
    if (!entry) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    kobj *o; SceUID id = obj_new(K_THREAD, &o);
    if (id < 0) return id;
    o->u.thr.entry = entry; o->u.thr.stack = stackSize < 256 * 1024 ? 256 * 1024 : stackSize;
    snprintf(o->u.thr.name, sizeof o->u.thr.name, "%s", name ? name : "sce");
    return id;
}
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp)
{
    kobj *o = obj_get(thid, K_THREAD);
    if (!o || o->u.thr.started) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    if (arglen && argp) { o->u.thr.args = malloc(arglen); memcpy(o->u.thr.args, argp, arglen); }   /* the Vita copies args onto the new stack */
    o->u.thr.arglen = arglen;
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, o->u.thr.stack);
    int rc = pthread_create(&o->u.thr.th, &a, thread_main, o);
    pthread_attr_destroy(&a);
    if (rc) return SCE_KERNEL_ERROR_NO_MEMORY;
    o->u.thr.started = 1;
    return 0;
}
int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout)
{
    (void)timeout;
    kobj *o = obj_get(thid, K_THREAD);
    if (!o || !o->u.thr.started) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    if (!o->u.thr.joined) { pthread_join(o->u.thr.th, NULL); o->u.thr.joined = 1; }
    if (stat) *stat = o->u.thr.status;
    return 0;
}
int sceKernelDeleteThread(SceUID thid)
{
    kobj *o = obj_get(thid, K_THREAD);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    if (o->u.thr.started && !o->u.thr.joined) pthread_detach(o->u.thr.th);
    free(o->u.thr.args); obj_free(o); return 0;
}

SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, SceKernelSemaOptParam *option)
{
    (void)name; (void)attr; (void)option;
    kobj *o; SceUID id = obj_new(K_SEMA, &o);
    if (id < 0) return id;
    pthread_mutex_init(&o->u.sema.m, NULL); cond_init_monotonic(&o->u.sema.c);
    o->u.sema.count = initVal; o->u.sema.max = maxVal;
    return id;
}
int sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout)
{
    kobj *o = obj_get(semaid, K_SEMA);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    struct timespec until; if (timeout) deadline_ts(&until, *timeout);
    pthread_mutex_lock(&o->u.sema.m);
    while (o->u.sema.count < signal) {
        if (!timeout) pthread_cond_wait(&o->u.sema.c, &o->u.sema.m);
        else if (pthread_cond_timedwait(&o->u.sema.c, &o->u.sema.m, &until) == ETIMEDOUT) {
            pthread_mutex_unlock(&o->u.sema.m); *timeout = 0; return SCE_KERNEL_ERROR_WAIT_TIMEOUT;
        }
    }
    o->u.sema.count -= signal;
    pthread_mutex_unlock(&o->u.sema.m);
    return 0;
}
int sceKernelSignalSema(SceUID semaid, int signal)
{
    kobj *o = obj_get(semaid, K_SEMA);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&o->u.sema.m);
    if (o->u.sema.max > 0 && o->u.sema.count + signal > o->u.sema.max) { pthread_mutex_unlock(&o->u.sema.m); return SCE_KERNEL_ERROR_SEMA_OVF; }
    o->u.sema.count += signal;
    pthread_cond_broadcast(&o->u.sema.c);
    pthread_mutex_unlock(&o->u.sema.m);
    return 0;
}
int sceKernelDeleteSema(SceUID semaid)
{
    kobj *o = obj_get(semaid, K_SEMA);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    obj_free(o); return 0;
}

SceUID sceKernelCreateMutex(const char *name, SceUInt attr, int initCount, SceKernelMutexOptParam *option)
{
    (void)name; (void)option;
    kobj *o; SceUID id = obj_new(K_MUTEX, &o);
    if (id < 0) return id;
    pthread_mutex_init(&o->u.mutex.m, NULL); cond_init_monotonic(&o->u.mutex.c);
    o->u.mutex.recursive = (attr & SCE_KERNEL_MUTEX_ATTR_RECURSIVE) != 0;
    if (initCount > 0) { o->u.mutex.owner = pthread_self(); o->u.mutex.count = initCount; }
    return id;
}
int sceKernelLockMutex(SceUID mutexid, int lockCount, unsigned int *timeout)
{
    kobj *o = obj_get(mutexid, K_MUTEX);
    if (!o || lockCount <= 0) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    struct timespec until; if (timeout) deadline_ts(&until, *timeout);
    pthread_t self = pthread_self();
    pthread_mutex_lock(&o->u.mutex.m);
    if (o->u.mutex.count > 0 && pthread_equal(o->u.mutex.owner, self)) {
        if (!o->u.mutex.recursive) { pthread_mutex_unlock(&o->u.mutex.m); return SCE_KERNEL_ERROR_MUTEX_RECURSIVE; }
        o->u.mutex.count += lockCount; pthread_mutex_unlock(&o->u.mutex.m); return 0;
    }
    while (o->u.mutex.count > 0) {
        if (!timeout) pthread_cond_wait(&o->u.mutex.c, &o->u.mutex.m);
        else if (pthread_cond_timedwait(&o->u.mutex.c, &o->u.mutex.m, &until) == ETIMEDOUT) {
            pthread_mutex_unlock(&o->u.mutex.m); *timeout = 0; return SCE_KERNEL_ERROR_WAIT_TIMEOUT;
        }
    }
    o->u.mutex.owner = self; o->u.mutex.count = lockCount;
    pthread_mutex_unlock(&o->u.mutex.m);
    return 0;
}
int sceKernelUnlockMutex(SceUID mutexid, int unlockCount)
{
    kobj *o = obj_get(mutexid, K_MUTEX);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&o->u.mutex.m);
    if (o->u.mutex.count < unlockCount || !pthread_equal(o->u.mutex.owner, pthread_self())) {
        pthread_mutex_unlock(&o->u.mutex.m); return SCE_KERNEL_ERROR_MUTEX_UNLOCK_UDF;
    }
    o->u.mutex.count -= unlockCount;
    if (!o->u.mutex.count) pthread_cond_broadcast(&o->u.mutex.c);
    pthread_mutex_unlock(&o->u.mutex.m);
    return 0;
}
int sceKernelDeleteMutex(SceUID mutexid)
{
    kobj *o = obj_get(mutexid, K_MUTEX);
    if (!o) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    obj_free(o); return 0;
}

/* ---- console, files, process ---------------------------------------------------------------------- */
static int g_console;
int sceClibPrintf(const char *fmt, ...)
{
    if (!g_console) return 0;
    va_list ap; va_start(ap, fmt); int n = vfprintf(stderr, fmt, ap); va_end(ap);
    return n;
}
SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
    (void)mode;
    int acc = flags & SCE_O_RDWR, f = acc == SCE_O_RDWR ? O_RDWR : acc == SCE_O_WRONLY ? O_WRONLY : O_RDONLY;
    if (flags & SCE_O_CREAT) f |= O_CREAT;
    if (flags & SCE_O_TRUNC) f |= O_TRUNC;
    if (flags & SCE_O_APPEND) f |= O_APPEND;
    if (flags & SCE_O_EXCL) f |= O_EXCL;
    int fd = open(file, f | O_CLOEXEC, 0644);
    return fd < 0 ? (SceUID)0x80010002 : fd;
}
SceSSize sceIoWrite(SceUID fd, const void *buf, SceSize nbyte) { return write(fd, buf, nbyte); }
int sceIoSyncByFd(SceUID fd, int flag) { (void)flag; if (getenv("XV_HOST_FSYNC")) fdatasync(fd); return 0; }   /* write() already reached the page cache; an SD card fsync per flush is slow */
int sceIoMkdir(const char *dir, SceMode mode)
{
    (void)mode;
    if (mkdir(dir, 0755) == 0) return 0;
    return errno == EEXIST ? (int)0x80010011 : (int)0x80010002;
}

static unsigned g_flips;
static uint64_t g_audio_empty, g_gxm_draws, g_gxm_scenes, g_audio_buffers;
static void host_summary(void)
{
    static int done; if (done) return; done = 1;
    double s = (mono_us() - g_start_us) / 1e6;
    xv_logf("[host] exit after %.1f s: flips=%u (%.2f/s) gxm scenes=%llu draws=%llu audio buffers=%llu empty_submits=%llu\n",
            s, g_flips, s > 0 ? g_flips / s : 0.0, (unsigned long long)g_gxm_scenes, (unsigned long long)g_gxm_draws,
            (unsigned long long)g_audio_buffers, (unsigned long long)g_audio_empty);
    fprintf(stderr, "[host] exit after %.1f s, %u flips\n", s, g_flips);
}
int sceKernelExitProcess(int res)
{
    xv_logf("[host] sceKernelExitProcess(%d)\n", res);
    host_summary();
    fflush(NULL);
    _exit(res);
}

/* ---- display --------------------------------------------------------------------------------------- */
/* 60 Hz: vcount k starts at k * 50000 / 3 us after process start. */
int sceDisplayGetVcount(void) { return (int)((mono_us() - g_start_us) * 3u / 50000u); }
int sceDisplayWaitVblankStart(void)
{
    uint64_t now = mono_us() - g_start_us;
    sleep_until_us(g_start_us + (now * 3u / 50000u + 1u) * 50000u / 3u);
    return 0;
}
static unsigned g_shot_every;
static const char *g_status_path;
static void write_shot(const SceDisplayFrameBuf *f, unsigned flip)
{
    char path[64]; snprintf(path, sizeof path, "shot-%06u.ppm", flip);
    FILE *out = fopen(path, "wb");
    if (!out) return;
    fprintf(out, "P6\n%u %u\n255\n", f->width, f->height);
    const uint8_t *px = f->base;
    uint8_t *row = malloc((size_t)f->width * 3);
    for (unsigned y = 0; row && y < f->height; ++y) {
        for (unsigned x = 0; x < f->width; ++x) {           /* A8B8G8R8: bytes R, G, B, A */
            const uint8_t *p = px + ((size_t)y * f->pitch + x) * 4;
            row[x * 3] = p[0]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[2];
        }
        fwrite(row, 3, f->width, out);
    }
    free(row); fclose(out);
}
int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync)
{
    (void)sync;
    if (!pParam || !pParam->base) return 0;                 /* blanking */
    unsigned flip = __atomic_add_fetch(&g_flips, 1, __ATOMIC_RELAXED);
    if (g_shot_every && flip % g_shot_every == 0) write_shot(pParam, flip);
    if (g_status_path && flip % 10 == 0) {                 /* live flip counter for a driver script */
        char tmp[512]; snprintf(tmp, sizeof tmp, "%s.tmp", g_status_path);
        FILE *f = fopen(tmp, "w");
        if (f) { fprintf(f, "%u %.1f\n", flip, (mono_us() - g_start_us) / 1e6); fclose(f); rename(tmp, g_status_path); }
    }
    if (flip % 60 == 0) {
        static uint64_t last_us; uint64_t now = mono_us();
        double window = last_us ? (now - last_us) / 1e6 : (now - g_start_us) / 1e6;
        last_us = now;
        xv_logf("[host] flip %u t=%.1f s last60=%.2f s (%.2f fps) gxm draws=%llu audio empty_submits=%llu\n", flip,
                (now - g_start_us) / 1e6, window, window > 0 ? 60.0 / window : 0.0,
                (unsigned long long)g_gxm_draws, (unsigned long long)g_audio_empty);
        xv_host_sample_dump(flip);
    }
    return 0;
}

/* ---- controller ------------------------------------------------------------------------------------ */
typedef struct { unsigned at, hold; int seconds; unsigned buttons; int axis, value; } pad_event;
static pad_event g_pad[128]; static int g_npad; static const char *g_pad_file;
static int pad_button(const char *b, unsigned *buttons, int *axis, int *value)
{
    static const struct { const char *name; unsigned mask; int axis, value; } map[] = {
        {"up", SCE_CTRL_UP, -1, 0}, {"down", SCE_CTRL_DOWN, -1, 0}, {"left", SCE_CTRL_LEFT, -1, 0},
        {"right", SCE_CTRL_RIGHT, -1, 0}, {"start", SCE_CTRL_START, -1, 0}, {"back", SCE_CTRL_SELECT, -1, 0},
        {"a", SCE_CTRL_CROSS, -1, 0}, {"b", SCE_CTRL_CIRCLE, -1, 0}, {"x", SCE_CTRL_SQUARE, -1, 0},
        {"y", SCE_CTRL_TRIANGLE, -1, 0}, {"l", SCE_CTRL_LTRIGGER, -1, 0}, {"r", SCE_CTRL_RTRIGGER, -1, 0},
        {"lup", 0, 1, 0}, {"ldown", 0, 1, 255}, {"lleft", 0, 0, 0}, {"lright", 0, 0, 255},
        {"rup", 0, 3, 0}, {"rdown", 0, 3, 255}, {"rleft", 0, 2, 0}, {"rright", 0, 2, 255},
    };
    for (unsigned i = 0; i < sizeof map / sizeof *map; ++i)
        if (!strcmp(b, map[i].name)) { *buttons = map[i].mask; *axis = map[i].axis; *value = map[i].value; return 1; }
    return 0;
}
static void pad_parse(void)
{
    const char *e = getenv("XV_PAD");
    while (e && *e && g_npad < 128) {
        pad_event ev = {0}; char b[16]; int n = 0;
        ev.seconds = *e == 't'; if (ev.seconds) ++e;
        if (sscanf(e, "%u:%15[a-z]%n", &ev.at, b, &n) < 2) { xv_logf("[host] XV_PAD parse error at '%s'\n", e); break; }
        e += n; ev.hold = ev.seconds ? 1 : 30;
        if (*e == '*') { int m = 0; if (sscanf(e, "*%u%n", &ev.hold, &m) >= 1) e += m; }
        if (pad_button(b, &ev.buttons, &ev.axis, &ev.value)) g_pad[g_npad++] = ev;
        else xv_logf("[host] XV_PAD unknown button '%s'\n", b);
        while (*e == ',' || *e == ' ') ++e;
    }
    g_pad_file = getenv("XV_PAD_FILE");
    if (g_npad || g_pad_file) xv_logf("[host] pad script: %d events%s%s\n", g_npad, g_pad_file ? ", live file " : "", g_pad_file ? g_pad_file : "");
}
static void pad_apply(SceCtrlData *d, unsigned buttons, int axis, int value)
{
    d->buttons |= buttons;
    if (axis == 0) d->lx = (unsigned char)value; else if (axis == 1) d->ly = (unsigned char)value;
    else if (axis == 2) d->rx = (unsigned char)value; else if (axis == 3) d->ry = (unsigned char)value;
}
int sceCtrlSetSamplingMode(SceCtrlPadInputMode mode) { (void)mode; return 0; }
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count)
{
    (void)port;
    if (!pad_data || count < 1) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    static pthread_once_t parsed = PTHREAD_ONCE_INIT;
    pthread_once(&parsed, pad_parse);                       /* first poll: boot.log is open, messages land there */
    memset(pad_data, 0, sizeof *pad_data);
    pad_data->timeStamp = mono_us();
    pad_data->lx = pad_data->ly = pad_data->rx = pad_data->ry = 128;
    unsigned flips = __atomic_load_n(&g_flips, __ATOMIC_RELAXED);
    double secs = (mono_us() - g_start_us) / 1e6;
    for (int i = 0; i < g_npad; ++i) {
        const pad_event *ev = &g_pad[i];
        int on = ev->seconds ? secs >= ev->at && secs < ev->at + ev->hold : flips >= ev->at && flips < ev->at + ev->hold;
        if (on) pad_apply(pad_data, ev->buttons, ev->axis, ev->value);
    }
    if (g_pad_file) {                                       /* live control: re-read at most every 100 ms */
        static uint64_t next_read; static char held[256];
        uint64_t now = mono_us();
        if (now >= next_read) {
            next_read = now + 100000; held[0] = 0;
            FILE *f = fopen(g_pad_file, "r");
            if (f) { size_t n = fread(held, 1, sizeof held - 1, f); held[n] = 0; fclose(f); }
        }
        char copy[256]; snprintf(copy, sizeof copy, "%s", held);
        for (char *save = NULL, *t = strtok_r(copy, " ,\t\r\n", &save); t; t = strtok_r(NULL, " ,\t\r\n", &save)) {
            unsigned buttons; int axis, value;
            if (pad_button(t, &buttons, &axis, &value)) pad_apply(pad_data, buttons, axis, value);
        }
    }
    return 1;
}

/* ---- audio ----------------------------------------------------------------------------------------- */
/* audio_vita.c keeps one grain outstanding: it polls GetRestSample until the port drains, then mixes and
 * submits, so every submit meets an empty port ("empty submits" below = that gap, not a device underrun). */
typedef struct { int used, len, freq; uint64_t end_us; } audio_port;
static audio_port g_ports[8];
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static audio_port *port_get(int port) { int i = port - 0x100; return i >= 0 && i < 8 && g_ports[i].used ? &g_ports[i] : NULL; }
int sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode)
{
    (void)type; (void)mode;
    if (len <= 0 || freq <= 0) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&g_audio_lock);
    for (int i = 0; i < 8; ++i) if (!g_ports[i].used) {
        g_ports[i] = (audio_port){1, len, freq, 0};
        pthread_mutex_unlock(&g_audio_lock);
        xv_logf("[host] audio port %d: %d samples at %d Hz (paced, no device)\n", 0x100 + i, len, freq);
        return 0x100 + i;
    }
    pthread_mutex_unlock(&g_audio_lock);
    return SCE_KERNEL_ERROR_NO_MEMORY;
}
int sceAudioOutOutput(int port, const void *buf)
{
    audio_port *p = port_get(port);
    if (!p) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    uint64_t now = mono_us(), dur = (uint64_t)p->len * 1000000u / (uint64_t)p->freq;
    if (!buf) { if (p->end_us > now) sleep_until_us(p->end_us); return 0; }
    if (p->end_us < now) { if (p->end_us) __atomic_add_fetch(&g_audio_empty, 1, __ATOMIC_RELAXED); p->end_us = now; }
    p->end_us += dur;
    __atomic_add_fetch(&g_audio_buffers, 1, __ATOMIC_RELAXED);
    if (p->end_us > now + dur) sleep_until_us(p->end_us - dur);   /* one playing + this one queued */
    return 0;
}
int sceAudioOutGetRestSample(int port)
{
    audio_port *p = port_get(port);
    if (!p) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    uint64_t now = mono_us();
    return p->end_us > now ? (int)((p->end_us - now) * (uint64_t)p->freq / 1000000u) : 0;
}
int sceAudioOutSetVolume(int port, SceAudioOutChannelFlag ch, int *vol) { (void)ch; (void)vol; return port_get(port) ? 0 : SCE_KERNEL_ERROR_INVALID_ARGUMENT; }
int sceAudioOutReleasePort(int port)
{
    audio_port *p = port_get(port);
    if (!p) return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
    p->used = 0; return 0;
}

/* ---- GXM (no GPU) ---------------------------------------------------------------------------------- */
/* .gxp header: "GXP\0", u8 major, u8 minor, u16 sdk, u32 size, ... u32 parameter_count at 36, u32
 * parameters_offset at 40 (relative to that field). Parameters are 16 bytes: s32 name offset (relative to
 * the parameter), u16 category/type/components/container, u8 semantic, u8 semantic index, u32 array size,
 * s32 resource index. */
static uint32_t rd32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
int sceGxmProgramCheck(const SceGxmProgram *program)
{
    const uint8_t *b = (const uint8_t *)program;
    return b && !memcmp(b, "GXP", 4) ? 0 : SCE_GXM_ERROR_INVALID_VALUE;
}
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *program, const char *name)
{
    if (!program || !name || sceGxmProgramCheck(program) < 0) return NULL;
    const uint8_t *b = (const uint8_t *)program;
    uint32_t size = rd32(b + 8), count = rd32(b + 36), first = 40 + rd32(b + 40);
    for (uint32_t i = 0; i < count && first + (i + 1) * 16 <= size; ++i) {
        const uint8_t *p = b + first + i * 16;
        int32_t off = (int32_t)rd32(p);
        uint64_t at = (uint64_t)(first + i * 16) + (uint64_t)(int64_t)off;
        if (at >= size) continue;
        if (!strncmp((const char *)b + at, name, size - at) ) return (const SceGxmProgramParameter *)p;
    }
    return NULL;
}
unsigned int sceGxmProgramParameterGetArraySize(const SceGxmProgramParameter *parameter) { return parameter ? rd32((const uint8_t *)parameter + 8) : 0; }
unsigned int sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *parameter) { return parameter ? rd32((const uint8_t *)parameter + 12) : 0; }

static volatile unsigned int g_notification[512];
enum { UNIFORM_BYTES = 64 * 1024 };
static float g_vertex_uniforms[UNIFORM_BYTES / 4], g_fragment_uniforms[UNIFORM_BYTES / 4];
typedef struct { const SceGxmProgram *program; } registered_program;
int sceGxmInitialize(const SceGxmInitializeParams *params) { (void)params; xv_logf("[host] GXM: no GPU (null backend)\n"); return 0; }
volatile unsigned int *sceGxmGetNotificationRegion(void) { return g_notification; }
int sceGxmNotificationWait(const SceGxmNotification *notification) { (void)notification; return 0; }   /* EndScene already wrote it */
int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr) { (void)base; (void)size; (void)attr; return 0; }
int sceGxmMapVertexUsseMemory(void *base, SceSize size, unsigned int *offset) { (void)base; (void)size; if (offset) *offset = 0; return 0; }
int sceGxmMapFragmentUsseMemory(void *base, SceSize size, unsigned int *offset) { (void)base; (void)size; if (offset) *offset = 0; return 0; }
int sceGxmCreateContext(const SceGxmContextParams *params, SceGxmContext **context)
{ (void)params; if (!context) return SCE_GXM_ERROR_INVALID_POINTER; *context = calloc(1, 64); return 0; }
int sceGxmShaderPatcherCreate(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher)
{ (void)params; if (!shaderPatcher) return SCE_GXM_ERROR_INVALID_POINTER; *shaderPatcher = calloc(1, 64); return 0; }
int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *shaderPatcher, const SceGxmProgram *programHeader, SceGxmShaderPatcherId *programId)
{
    (void)shaderPatcher;
    if (!programId || sceGxmProgramCheck(programHeader) < 0) return SCE_GXM_ERROR_INVALID_VALUE;
    registered_program *r = malloc(sizeof *r); r->program = programHeader;
    *programId = (SceGxmShaderPatcherId)(void *)r;
    return 0;
}
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId, const SceGxmVertexAttribute *attributes,
                                           unsigned int attributeCount, const SceGxmVertexStream *streams, unsigned int streamCount, SceGxmVertexProgram **vertexProgram)
{
    (void)shaderPatcher; (void)attributes; (void)attributeCount; (void)streams; (void)streamCount;
    if (!programId || !vertexProgram) return SCE_GXM_ERROR_INVALID_POINTER;
    *vertexProgram = calloc(1, 16); return 0;
}
int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId, SceGxmOutputRegisterFormat outputFormat,
                                             SceGxmMultisampleMode multisampleMode, const SceGxmBlendInfo *blendInfo, const SceGxmProgram *vertexProgram,
                                             SceGxmFragmentProgram **fragmentProgram)
{
    (void)shaderPatcher; (void)outputFormat; (void)multisampleMode; (void)blendInfo; (void)vertexProgram;
    if (!programId || !fragmentProgram) return SCE_GXM_ERROR_INVALID_POINTER;
    *fragmentProgram = calloc(1, 16); return 0;
}
int sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *params, SceGxmRenderTarget **renderTarget)
{ (void)params; if (!renderTarget) return SCE_GXM_ERROR_INVALID_POINTER; *renderTarget = calloc(1, 16); return 0; }
int sceGxmColorSurfaceInit(SceGxmColorSurface *surface, SceGxmColorFormat colorFormat, SceGxmColorSurfaceType surfaceType, SceGxmColorSurfaceScaleMode scaleMode,
                           SceGxmOutputRegisterSize outputRegisterSize, unsigned int width, unsigned int height, unsigned int strideInPixels, void *data)
{
    (void)colorFormat; (void)surfaceType; (void)scaleMode; (void)outputRegisterSize; (void)width; (void)height; (void)strideInPixels; (void)data;
    if (surface) memset(surface, 0, sizeof *surface);
    return 0;
}
int sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilFormat depthStencilFormat, SceGxmDepthStencilSurfaceType surfaceType,
                                  unsigned int strideInSamples, void *depthData, void *stencilData)
{
    (void)depthStencilFormat; (void)surfaceType; (void)strideInSamples; (void)depthData; (void)stencilData;
    if (surface) memset(surface, 0, sizeof *surface);
    return 0;
}
void sceGxmDepthStencilSurfaceSetBackgroundDepth(SceGxmDepthStencilSurface *surface, float backgroundDepth) { (void)surface; (void)backgroundDepth; }
void sceGxmDepthStencilSurfaceSetForceLoadMode(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilForceLoadMode forceLoad) { (void)surface; (void)forceLoad; }
void sceGxmDepthStencilSurfaceSetForceStoreMode(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilForceStoreMode forceStore) { (void)surface; (void)forceStore; }
static int texture_init(SceGxmTexture *texture) { if (!texture) return SCE_GXM_ERROR_INVALID_POINTER; memset(texture, 0, sizeof *texture); return 0; }
int sceGxmTextureInitCube(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount)
{ (void)data; (void)texFormat; (void)width; (void)height; (void)mipCount; return texture_init(texture); }
int sceGxmTextureInitLinear(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount)
{ (void)data; (void)texFormat; (void)width; (void)height; (void)mipCount; return texture_init(texture); }
int sceGxmTextureInitSwizzled(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount)
{ (void)data; (void)texFormat; (void)width; (void)height; (void)mipCount; return texture_init(texture); }
int sceGxmTextureSetMagFilter(SceGxmTexture *texture, SceGxmTextureFilter magFilter) { (void)texture; (void)magFilter; return 0; }
int sceGxmTextureSetMinFilter(SceGxmTexture *texture, SceGxmTextureFilter minFilter) { (void)texture; (void)minFilter; return 0; }
int sceGxmTextureSetMipFilter(SceGxmTexture *texture, SceGxmTextureMipFilter mipFilter) { (void)texture; (void)mipFilter; return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode addrMode) { (void)texture; (void)addrMode; return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode addrMode) { (void)texture; (void)addrMode; return 0; }
int sceGxmBeginScene(SceGxmContext *context, unsigned int flags, const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
                     SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, const SceGxmColorSurface *colorSurface,
                     const SceGxmDepthStencilSurface *depthStencil)
{
    (void)context; (void)flags; (void)renderTarget; (void)validRegion; (void)vertexSyncObject; (void)fragmentSyncObject; (void)colorSurface; (void)depthStencil;
    ++g_gxm_scenes; return 0;
}
int sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification, const SceGxmNotification *fragmentNotification)
{
    (void)context;
    if (vertexNotification && vertexNotification->address) *vertexNotification->address = vertexNotification->value;
    if (fragmentNotification && fragmentNotification->address) *fragmentNotification->address = fragmentNotification->value;
    return 0;
}
void sceGxmFinish(SceGxmContext *context) { (void)context; }
int sceGxmSetVisibilityBuffer(SceGxmContext *context, void *bufferBase, unsigned int stridePerCore) { (void)context; (void)bufferBase; (void)stridePerCore; return 0; }
void sceGxmSetBackDepthFunc(SceGxmContext *context, SceGxmDepthFunc depthFunc) { (void)context; (void)depthFunc; }
void sceGxmSetFrontDepthFunc(SceGxmContext *context, SceGxmDepthFunc depthFunc) { (void)context; (void)depthFunc; }
void sceGxmSetBackDepthWriteEnable(SceGxmContext *context, SceGxmDepthWriteMode enable) { (void)context; (void)enable; }
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *context, SceGxmDepthWriteMode enable) { (void)context; (void)enable; }
void sceGxmSetBackFragmentProgramEnable(SceGxmContext *context, SceGxmFragmentProgramMode enable) { (void)context; (void)enable; }
void sceGxmSetFrontFragmentProgramEnable(SceGxmContext *context, SceGxmFragmentProgramMode enable) { (void)context; (void)enable; }
void sceGxmSetBackStencilFunc(SceGxmContext *context, SceGxmStencilFunc func, SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail, SceGxmStencilOp depthPass,
                              unsigned char compareMask, unsigned char writeMask)
{ (void)context; (void)func; (void)stencilFail; (void)depthFail; (void)depthPass; (void)compareMask; (void)writeMask; }
void sceGxmSetFrontStencilFunc(SceGxmContext *context, SceGxmStencilFunc func, SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail, SceGxmStencilOp depthPass,
                               unsigned char compareMask, unsigned char writeMask)
{ (void)context; (void)func; (void)stencilFail; (void)depthFail; (void)depthPass; (void)compareMask; (void)writeMask; }
void sceGxmSetBackVisibilityTestEnable(SceGxmContext *context, SceGxmVisibilityTestMode enable) { (void)context; (void)enable; }
void sceGxmSetFrontVisibilityTestEnable(SceGxmContext *context, SceGxmVisibilityTestMode enable) { (void)context; (void)enable; }
void sceGxmSetBackVisibilityTestIndex(SceGxmContext *context, unsigned int index) { (void)context; (void)index; }
void sceGxmSetFrontVisibilityTestIndex(SceGxmContext *context, unsigned int index) { (void)context; (void)index; }
void sceGxmSetBackVisibilityTestOp(SceGxmContext *context, SceGxmVisibilityTestOp op) { (void)context; (void)op; }
void sceGxmSetFrontVisibilityTestOp(SceGxmContext *context, SceGxmVisibilityTestOp op) { (void)context; (void)op; }
void sceGxmSetCullMode(SceGxmContext *context, SceGxmCullMode mode) { (void)context; (void)mode; }
void sceGxmSetRegionClip(SceGxmContext *context, SceGxmRegionClipMode mode, unsigned int xMin, unsigned int yMin, unsigned int xMax, unsigned int yMax)
{ (void)context; (void)mode; (void)xMin; (void)yMin; (void)xMax; (void)yMax; }
void sceGxmSetViewport(SceGxmContext *context, float xOffset, float xScale, float yOffset, float yScale, float zOffset, float zScale)
{ (void)context; (void)xOffset; (void)xScale; (void)yOffset; (void)yScale; (void)zOffset; (void)zScale; }
void sceGxmSetViewportEnable(SceGxmContext *context, SceGxmViewportMode enable) { (void)context; (void)enable; }
void sceGxmSetVertexProgram(SceGxmContext *context, const SceGxmVertexProgram *vertexProgram) { (void)context; (void)vertexProgram; }
void sceGxmSetFragmentProgram(SceGxmContext *context, const SceGxmFragmentProgram *fragmentProgram) { (void)context; (void)fragmentProgram; }
int sceGxmSetVertexStream(SceGxmContext *context, unsigned int streamIndex, const void *streamData) { (void)context; (void)streamIndex; (void)streamData; return 0; }
int sceGxmSetFragmentTexture(SceGxmContext *context, unsigned int textureIndex, const SceGxmTexture *texture) { (void)context; (void)textureIndex; (void)texture; return 0; }
int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer)
{ (void)context; if (!uniformBuffer) return SCE_GXM_ERROR_INVALID_POINTER; *uniformBuffer = g_vertex_uniforms; return 0; }
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer)
{ (void)context; if (!uniformBuffer) return SCE_GXM_ERROR_INVALID_POINTER; *uniformBuffer = g_fragment_uniforms; return 0; }
int sceGxmSetUniformDataF(void *uniformBuffer, const SceGxmProgramParameter *parameter, unsigned int componentOffset, unsigned int componentCount, const float *sourceData)
{
    if (!uniformBuffer || !parameter || !sourceData) return SCE_GXM_ERROR_INVALID_POINTER;
    uint64_t first = (uint64_t)sceGxmProgramParameterGetResourceIndex(parameter) + componentOffset;
    if ((first + componentCount) * 4 <= UNIFORM_BYTES) memcpy((float *)uniformBuffer + first, sourceData, (size_t)componentCount * 4);
    return 0;
}
int sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType, SceGxmIndexFormat indexType, const void *indexData, unsigned int indexCount)
{ (void)context; (void)primType; (void)indexType; (void)indexData; (void)indexCount; ++g_gxm_draws; return 0; }

/* ---- FPSCR (boot.c reads/writes it around the guest's STMXCSR/LDMXCSR) ------------------------------ */
#if !defined(__arm__)
/* x86: emulate the cumulative exception bits (IOC DZC OFC UFC IXC = bits 0-4) with the host's fenv flags;
 * the control bits are kept as written (the runtime faults on any non-default control value anyway). */
static uint32_t g_fpscr_control;
uint32_t h2_platform_fpscr_read(void)
{
    int e = fetestexcept(FE_ALL_EXCEPT);
    return (g_fpscr_control & ~0x9Fu) | (e & FE_INVALID ? 1u : 0) | (e & FE_DIVBYZERO ? 2u : 0) | (e & FE_OVERFLOW ? 4u : 0) |
           (e & FE_UNDERFLOW ? 8u : 0) | (e & FE_INEXACT ? 16u : 0);
}
void h2_platform_fpscr_write(uint32_t value)
{
    g_fpscr_control = value & ~0x9Fu;
    feclearexcept(FE_ALL_EXCEPT);
    int e = (value & 1 ? FE_INVALID : 0) | (value & 2 ? FE_DIVBYZERO : 0) | (value & 4 ? FE_OVERFLOW : 0) |
            (value & 8 ? FE_UNDERFLOW : 0) | (value & 16 ? FE_INEXACT : 0);
    if (e) feraiseexcept(e);
}
#endif

__attribute__((constructor)) static void host_init(void)
{
    g_start_us = mono_us();
    g_console = getenv("XV_HOST_CONSOLE") != NULL;
    const char *shot = getenv("XV_HOST_SHOT");
    g_shot_every = shot ? (unsigned)strtoul(shot, NULL, 0) : 0;
    g_status_path = getenv("XV_HOST_STATUS");
    atexit(host_summary);
    xv_host_sample_start();
}
