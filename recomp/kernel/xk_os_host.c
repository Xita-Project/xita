/* xk_os_host.c - Linux implementation of xk_os.h (POSIX files, clock_gettime, ucontext fibers). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <ucontext.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "xk_os.h"

void xk_os_log(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fputs("[xk] ", stderr); vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ---- files ------------------------------------------------------------------------- */
struct xk_file { int fd; };
struct xk_dir  { DIR *d; char path[1024]; };

xk_file *xk_os_open(const char *path, int write, int create, int truncate, int *err_is_missing)
{
    int flags = write ? O_RDWR : O_RDONLY;
    if (create) flags |= O_CREAT;
    if (truncate) flags |= O_TRUNC;
    int fd = open(path, flags, 0644);
    if (fd < 0 && !write && errno == EACCES) fd = open(path, O_RDONLY);
    if (fd < 0) { if (err_is_missing) *err_is_missing = (errno == ENOENT || errno == ENOTDIR); return NULL; }
    xk_file *f = calloc(1, sizeof *f); f->fd = fd; return f;
}
void xk_os_close(xk_file *f) { if (f) { close(f->fd); free(f); } }
int64_t xk_os_read(xk_file *f, uint64_t pos, void *buf, uint32_t n) { return pread(f->fd, buf, n, (off_t)pos); }
int64_t xk_os_write(xk_file *f, uint64_t pos, const void *buf, uint32_t n) { return pwrite(f->fd, buf, n, (off_t)pos); }
int64_t xk_os_size(xk_file *f) { struct stat st; return fstat(f->fd, &st) == 0 ? st.st_size : -1; }
int xk_os_truncate(xk_file *f, uint64_t size) { return ftruncate(f->fd, (off_t)size); }
int xk_os_flush(xk_file *f) { return fsync(f->fd); }
int xk_os_stat(const char *path, int *is_dir, uint64_t *size, uint64_t *mtime_100ns)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (is_dir) *is_dir = S_ISDIR(st.st_mode);
    if (size) *size = (uint64_t)st.st_size;
    if (mtime_100ns) *mtime_100ns = ((uint64_t)st.st_mtime + 11644473600ull) * 10000000ull;
    return 0;
}
int xk_os_unlink(const char *path) { return unlink(path); }
int xk_os_mkdir(const char *path) { return mkdir(path, 0755); }
int xk_os_rmdir(const char *path) { return rmdir(path); }
int xk_os_rename(const char *from, const char *to) { return rename(from, to); }
xk_dir *xk_os_opendir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return NULL;
    xk_dir *x = calloc(1, sizeof *x); x->d = d; snprintf(x->path, sizeof x->path, "%s", path); return x;
}
int xk_os_readdir(xk_dir *d, char *name, unsigned cap, int *is_dir, uint64_t *size)
{
    struct dirent *e;
    while ((e = readdir(d->d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(name, cap, "%s", e->d_name);
        char full[1200]; snprintf(full, sizeof full, "%s/%s", d->path, e->d_name);
        struct stat st; if (stat(full, &st) == 0) { *is_dir = S_ISDIR(st.st_mode); *size = st.st_size; } else { *is_dir = 0; *size = 0; }
        return 1;
    }
    return 0;
}
void xk_os_closedir(xk_dir *d) { if (d) { closedir(d->d); free(d); } }
int xk_os_freespace(const char *path, uint64_t *free_bytes, uint64_t *total_bytes)
{
    struct statvfs v; if (statvfs(path, &v) != 0) return -1;
    *free_bytes = (uint64_t)v.f_bavail * v.f_frsize; *total_bytes = (uint64_t)v.f_blocks * v.f_frsize; return 0;
}

/* ---- time --------------------------------------------------------------------------- */
uint64_t xk_os_time_100ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}
uint64_t xk_os_monotonic_us(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint32_t)ts.tv_nsec / 1000u;   /* tv_nsec < 1e9: a 32-bit divide (armhf has no 64-bit divide: __udivmoddi4 was 10-15% of the Pi scene helper with XV_HLE_TIMING) */
}
void xk_os_sleep_us(uint64_t us) { struct timespec ts = { (time_t)(us / 1000000ull), (long)((us % 1000000ull) * 1000) }; nanosleep(&ts, NULL); }

#include <pthread.h>
static pthread_mutex_t g_scheduler_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_scheduler_cond=PTHREAD_COND_INITIALIZER;
static int g_scheduler_notified;
int xk_os_scheduler_prepare(void) { return 1; }
void xk_os_scheduler_notify(void)
{
    pthread_mutex_lock(&g_scheduler_mutex);
    g_scheduler_notified=1;
    pthread_cond_signal(&g_scheduler_cond);
    pthread_mutex_unlock(&g_scheduler_mutex);
}
void xk_os_scheduler_wait(uint64_t us)
{
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);
    until.tv_sec+=us/1000000;until.tv_nsec+=(us%1000000)*1000;
    if (until.tv_nsec>=1000000000) {until.tv_sec++;until.tv_nsec-=1000000000;}
    pthread_mutex_lock(&g_scheduler_mutex);
    while (!g_scheduler_notified)
        if (pthread_cond_timedwait(&g_scheduler_cond,&g_scheduler_mutex,&until)) break;
    g_scheduler_notified=0;
    pthread_mutex_unlock(&g_scheduler_mutex);
}

/* ---- fibers ---------------------------------------------------------------------------- */
struct xk_fiber { ucontext_t uc; void *stack; void (*entry)(void *); void *arg; };
static xk_fiber g_main_fiber;
static xk_fiber *g_current = &g_main_fiber;
static _Thread_local xk_fiber *g_native_guest;

static void fiber_trampoline(void)
{
    xk_fiber *f = g_current;
    f->entry(f->arg);
    fprintf(stderr, "[xk] fiber entry returned - switching to scheduler\n");
    xk_os_fiber_switch(&g_main_fiber);
}

xk_fiber *xk_os_fiber_create(void (*entry)(void *), void *arg, size_t host_stack)
{
    xk_fiber *f = calloc(1, sizeof *f);
    f->stack = malloc(host_stack); f->entry = entry; f->arg = arg;
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = f->stack; f->uc.uc_stack.ss_size = host_stack; f->uc.uc_link = NULL;
    makecontext(&f->uc, fiber_trampoline, 0);
    return f;
}
void xk_os_fiber_switch(xk_fiber *to)
{
    xk_fiber *from = g_current;
    if (from == to) return;
    { extern void xv_render_view_fiber_switch(void) __attribute__((weak));   /* a scene on the render table drops back to live before another guest thread runs */
      if (xv_render_view_fiber_switch) xv_render_view_fiber_switch(); }
    g_current = to;
    g_native_guest = to == &g_main_fiber ? NULL : to;
    swapcontext(&from->uc, &to->uc);
}
xk_fiber *xk_os_fiber_current(void) { return g_current; }
int xk_os_fiber_is_current_guest(void)
{
    /* Foreign threads short-circuit before inspecting shared scheduler state. */
    return g_native_guest && g_native_guest == g_current;
}
xk_fiber *xk_os_fiber_main(void) { return &g_main_fiber; }
void xk_os_fiber_destroy(xk_fiber *f) { if (f && f != &g_main_fiber) { free(f->stack); free(f); } }

/* ---- input ----------------------------------------------------------------------------- */
void xk_os_pad_poll(xk_os_pad *p)
{
    /* Scripted input for the harness: XV_PAD="200:down,320:a,400:start,900:lup*300" = input at game frame N
     * held for *hold frames (default 4).  Buttons up/down/left/right/start/back/a/b/x/y/l/r, sticks
     * lup/ldown/lleft/lright/rup/rdown/rleft/rright (full deflection) - same syntax as the Vita pad.txt.
     * XV_PRESS_A keeps the old A+START pulse. */
    static unsigned polls; static int init; static struct { unsigned at, hold; char btn[8]; } ev[64]; static int nev; static int press_a;
    if (!init) {
        init = 1; press_a = getenv("XV_PRESS_A") != NULL;
        const char *e = getenv("XV_PAD");
        while (e && *e && nev < 64) { unsigned at, hold = 4; char b[8]; int n; if (sscanf(e, "%u:%7[a-z0-9]%n", &at, b, &n) < 2) break; e += n;
            if (*e == '*') { int n2; if (sscanf(e, "*%u%n", &hold, &n2) >= 1) e += n2; }
            ev[nev].at = at; ev[nev].hold = hold; snprintf(ev[nev].btn, 8, "%s", b); nev++; while (*e == ',' || *e == ' ') e++; }
    }
    extern unsigned xd3d_frame(void) __attribute__((weak));
    memset(p, 0, sizeof *p); p->connected = 1; polls = xd3d_frame ? xd3d_frame() : polls + 1;   /* index by game frame */
    if (press_a && polls > 60 && (polls % 90) < 4) { p->analog[0] = 255; p->buttons |= 0x10; }
    for (int i = 0; i < nev; ++i) if (polls >= ev[i].at && polls < ev[i].at + ev[i].hold) {
        const char *b = ev[i].btn;
        if (!strcmp(b, "up")) p->buttons |= 1; else if (!strcmp(b, "down")) p->buttons |= 2;
        else if (!strcmp(b, "left")) p->buttons |= 4; else if (!strcmp(b, "right")) p->buttons |= 8;
        else if (!strcmp(b, "start")) p->buttons |= 0x10; else if (!strcmp(b, "back")) p->buttons |= 0x20;
        else if (!strcmp(b, "a")) p->analog[0] = 255; else if (!strcmp(b, "b")) p->analog[1] = 255;
        else if (!strcmp(b, "x")) p->analog[2] = 255; else if (!strcmp(b, "y")) p->analog[3] = 255;
        else if (!strcmp(b, "l")) p->analog[6] = 255; else if (!strcmp(b, "r")) p->analog[7] = 255;
        else if (!strcmp(b, "lup")) p->ly = 32767; else if (!strcmp(b, "ldown")) p->ly = -32767;
        else if (!strcmp(b, "lleft")) p->lx = -32767; else if (!strcmp(b, "lright")) p->lx = 32767;
        else if (!strcmp(b, "rup")) p->ry = 32767; else if (!strcmp(b, "rdown")) p->ry = -32767;
        else if (!strcmp(b, "rleft")) p->rx = -32767; else if (!strcmp(b, "rright")) p->rx = 32767;
        else if (!strcmp(b, "force")) p->force_start = 1;                                      /* force MP start (XV_FORCE_START) */
        else if (b[0] == 'p' && b[1] == '2') {                                             /* virtual player 2 (XV_PAD2=1) */
            const char *q = b + 2;
            if (!strcmp(q, "up")) p->p2_buttons |= 1; else if (!strcmp(q, "down")) p->p2_buttons |= 2;
            else if (!strcmp(q, "left")) p->p2_buttons |= 4; else if (!strcmp(q, "right")) p->p2_buttons |= 8;
            else if (!strcmp(q, "start")) p->p2_buttons |= 0x10; else if (!strcmp(q, "back")) p->p2_buttons |= 0x20;
            else if (!strcmp(q, "a")) p->p2_analog[0] = 255; else if (!strcmp(q, "b")) p->p2_analog[1] = 255;
            else if (!strcmp(q, "x")) p->p2_analog[2] = 255; else if (!strcmp(q, "y")) p->p2_analog[3] = 255;
        }
    }
}

/* ---- audio sink (host): real-time pacing by sleeping; XV_WAV=<file> appends the mix as 16-bit stereo WAV
 * (header patched on exit), so the decode can be checked by ear without a sound device. ------------- */
#include <pthread.h>
#include <stdio.h>
#include "xk_audio.h"
static pthread_mutex_t g_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_wav; static uint32_t g_wav_bytes; static int g_audio_rate;
static void wav_header(FILE *f, uint32_t bytes, int rate)
{
    uint8_t h[44]; uint32_t br = (uint32_t)rate * 4;
    memcpy(h, "RIFF", 4); uint32_t v = 36 + bytes; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4); uint16_t s = 1; memcpy(h + 20, &s, 2); s = 2; memcpy(h + 22, &s, 2);
    v = (uint32_t)rate; memcpy(h + 24, &v, 4); memcpy(h + 28, &br, 4); s = 4; memcpy(h + 32, &s, 2); s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f); fflush(f);
}
static void wav_close(void) { if (g_wav) { wav_header(g_wav, g_wav_bytes, g_audio_rate); fclose(g_wav); g_wav = NULL; } }
int xk_os_audio_open(int rate, int grain)
{
    (void)grain; g_audio_rate = rate;
    const char *w = getenv("XV_WAV");
    if (w) { g_wav = fopen(w, "wb"); if (g_wav) { wav_header(g_wav, 0, rate); atexit(wav_close); } }
    return 0;                                     /* always "available": the mixer paces itself */
}
void xk_os_audio_write(const int16_t *stereo, int frames)
{
    if (g_wav) { fwrite(stereo, 4, (size_t)frames, g_wav); g_wav_bytes += (uint32_t)frames * 4; if ((g_wav_bytes & 0x3FFFF) < (uint32_t)frames * 4) wav_header(g_wav, g_wav_bytes, g_audio_rate); fseek(g_wav, 0, SEEK_END); }
    xk_os_sleep_us((uint64_t)frames * 1000000ull / (uint64_t)g_audio_rate);
}
static void *audio_thread_main(void *arg) { void (*fn)(void *) = (void (*)(void *))arg; fn(NULL); return NULL; }
int xk_os_audio_thread_start(void (*fn)(void *), void *arg)
{
    (void)arg; pthread_t t;
    return pthread_create(&t, NULL, audio_thread_main, (void *)fn) == 0 ? 0 : -1;
}
void xk_os_audio_mutex_lock(void)   { pthread_mutex_lock(&g_audio_mutex); }
void xk_os_audio_mutex_unlock(void) { pthread_mutex_unlock(&g_audio_mutex); }
