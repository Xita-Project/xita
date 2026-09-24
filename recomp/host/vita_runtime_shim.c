/*
 * vita_runtime_shim.c - host (Linux x86-64 / armhf) stand-ins for the Vita system calls and main.c services that the
 * runtime's D3D *recording* path needs, so tools/host_build.py --runtime can link runtime/xv_d3d.c, xv_ui_gxm.c and
 * the vertex capture/upload/texture workers into the whole-game harness (recomp/host/harness.c).
 *
 * Scope: recording only. Nothing is replayed: GXM draw/state calls are no-ops, there is no GPU and no display. What
 * runs for real: the recording thread's HLE bridge (sync, index/vertex capture, constants, textures and texture
 * decode, flare quads), the capture/upload/texture worker threads (pthread-backed sceKernel threads, event flags and
 * semaphores) and the frame rotation. At each present the published frame's visibility queries complete with zero
 * samples, which is what the host null renderer (xd3d.c's weak hooks) always reported.
 *
 *  - Memory blocks: anonymous mmaps (zero-filled, like fresh Vita blocks). "Uncached" blocks are ordinary cached
 *    host memory, so the Vita's uncached-write cost is NOT represented in host or Pi profiles.
 *  - SceGxmTexture: the Vita3K control-word layout (gxm/types.h), except that the 30-bit data field holds an index
 *    into a pointer table (host pointers are 64-bit). Equal descriptors stay equal, different textures differ.
 *  - GXP reflection: parsed from the embedded program images (Vita3K SceGxmProgram / SceGxmProgramParameter), so
 *    sampler masks, c[] parameters and program flags match what the Vita reads.
 *  - Files: "app0:" maps to $XV_HOST_APP0 (default ".." = the stage root when the harness runs from recomp/);
 *    "ux0:" and everything else fail as absent.
 *
 * Diagnostic/profiling use only; no timing claim transfers to the Vita.
 */
#define _GNU_SOURCE
/* psp2 first: glibc's <sys/stat.h> defines st_ctime & co. as macros that would rename SceIoStat fields */
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include <errno.h>
#include <fcntl.h>
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

#include "../../runtime/xv_log.h"
#include "../../runtime/xv_shader.h"
#include "../../runtime/xv_frame_slots.h"

/* ---------------------------------------------------------------------------------------------------------------- */
/*  Objects: one table for threads, event flags, semaphores and memory blocks (uid = index + 0x100)                 */
/* ---------------------------------------------------------------------------------------------------------------- */
enum { OBJ_FREE, OBJ_THREAD, OBJ_EVENT, OBJ_SEMA, OBJ_BLOCK };
#define SHIM_OBJECTS 1024
typedef struct {
    int kind;
    /* thread */
    pthread_t thread; SceKernelThreadEntry entry; void *arg; SceSize arglen; int started, exit_status;
    /* event flag / semaphore */
    unsigned bits; int count, max, waiters;
    /* memory block */
    void *base; size_t size;
} shim_obj;
static shim_obj g_obj[SHIM_OBJECTS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;   /* one condition for every waitable object: rare waits */
static int g_waiters;   /* threads blocked on g_cond: a set/signal without waiters makes no futex call */

static SceUID obj_new(int kind)
{
    pthread_mutex_lock(&g_lock);
    for (unsigned i = 0; i < SHIM_OBJECTS; i++)
        if (g_obj[i].kind == OBJ_FREE) {
            memset(&g_obj[i], 0, sizeof g_obj[i]); g_obj[i].kind = kind;
            pthread_mutex_unlock(&g_lock); return (SceUID)(i + 0x100);
        }
    pthread_mutex_unlock(&g_lock);
    return (SceUID)0x80020001;   /* out of objects */
}
static shim_obj *obj_get(SceUID uid, int kind)
{
    unsigned i = (unsigned)uid - 0x100u;
    return i < SHIM_OBJECTS && g_obj[i].kind == kind ? &g_obj[i] : NULL;
}
static void deadline_after(struct timespec *ts, SceUInt us)
{
    clock_gettime(CLOCK_REALTIME, ts);
    uint64_t ns = (uint64_t)ts->tv_nsec + (uint64_t)us * 1000u;
    ts->tv_sec += (time_t)(ns / 1000000000u); ts->tv_nsec = (long)(ns % 1000000000u);
}

/* ---- time ---- */
SceUInt64 sceKernelGetProcessTimeWide(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (SceUInt64)ts.tv_sec * 1000000u + (SceUInt64)ts.tv_nsec / 1000u;
}
int sceKernelDelayThread(SceUInt delay) { usleep(delay ? delay : 1); return 0; }

/* ---- threads ---- */
static __thread SceUID tls_thread_id;
static void *thread_main(void *p)
{
    shim_obj *o = p;
    tls_thread_id = (SceUID)((o - g_obj) + 0x100);
    o->exit_status = o->entry(o->arglen, o->arg);
    return NULL;
}
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, SceSize stackSize,
    SceUInt attr, int cpuAffinityMask, const SceKernelThreadOptParam *option)
{
    (void)name; (void)initPriority; (void)stackSize; (void)attr; (void)cpuAffinityMask; (void)option;
    SceUID id = obj_new(OBJ_THREAD);
    shim_obj *o = obj_get(id, OBJ_THREAD);
    if (o) o->entry = entry;
    return id;
}
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp)
{
    shim_obj *o = obj_get(thid, OBJ_THREAD);
    if (!o || o->started) return (int)0x80020198;
    if (arglen && argp) { o->arg = malloc(arglen); memcpy(o->arg, argp, arglen); }   /* the Vita copies argp to the new stack */
    o->arglen = arglen;
    if (pthread_create(&o->thread, NULL, thread_main, o)) return (int)0x80020001;
    o->started = 1;
    return 0;
}
int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout)
{
    (void)timeout;
    shim_obj *o = obj_get(thid, OBJ_THREAD);
    if (!o || !o->started) return (int)0x80020198;
    pthread_join(o->thread, NULL); o->started = 0;
    if (stat) *stat = o->exit_status;
    return 0;
}
int sceKernelDeleteThread(SceUID thid)
{
    shim_obj *o = obj_get(thid, OBJ_THREAD);
    if (!o) return (int)0x80020198;
    free(o->arg); pthread_mutex_lock(&g_lock); o->kind = OBJ_FREE; pthread_mutex_unlock(&g_lock);
    return 0;
}
int sceKernelGetThreadId(void) { return tls_thread_id ? tls_thread_id : 0x40010001; }
int sceKernelGetThreadCurrentPriority(void) { return 160; }
int sceKernelGetThreadInfo(SceUID thid, SceKernelThreadInfo *info)
{
    (void)thid; if (!info) return (int)0x80020001;
    SceSize size = info->size; memset(info, 0, sizeof *info); info->size = size; info->currentPriority = 160;
    return 0;
}
int sceKernelChangeThreadCpuAffinityMask(SceUID thid, int mask) { (void)thid; (void)mask; return 0; }
int sceKernelGetThreadCpuAffinityMask(SceUID thid) { (void)thid; return 0; }
int sceKernelGetCpuId(void) { int c = sched_getcpu(); return c < 0 ? 0 : c & 3; }
int sceKernelGetSystemInfo(SceKernelSystemInfo *info) { (void)info; return (int)0x80020001; }   /* CPU counters: unknown */

/* ---- event flags ---- */
SceUID sceKernelCreateEventFlag(const char *name, int attr, int bits, SceKernelEventFlagOptParam *opt)
{
    (void)name; (void)attr; (void)opt;
    SceUID id = obj_new(OBJ_EVENT);
    shim_obj *o = obj_get(id, OBJ_EVENT);
    if (o) o->bits = (unsigned)bits;
    return id;
}
int sceKernelDeleteEventFlag(int evid)
{
    shim_obj *o = obj_get(evid, OBJ_EVENT);
    if (!o) return (int)0x80028003;
    pthread_mutex_lock(&g_lock); o->kind = OBJ_FREE; pthread_cond_broadcast(&g_cond); pthread_mutex_unlock(&g_lock);
    return 0;
}
int sceKernelSetEventFlag(SceUID evid, unsigned int bits)
{
    pthread_mutex_lock(&g_lock);
    shim_obj *o = obj_get(evid, OBJ_EVENT);
    if (o) { o->bits |= bits; if (g_waiters) pthread_cond_broadcast(&g_cond); }
    pthread_mutex_unlock(&g_lock);
    return o ? 0 : (int)0x80028003;
}
int sceKernelClearEventFlag(SceUID evid, unsigned int bits)
{
    pthread_mutex_lock(&g_lock);
    shim_obj *o = obj_get(evid, OBJ_EVENT);
    if (o) o->bits &= bits;   /* Vita semantics: AND with the given pattern */
    pthread_mutex_unlock(&g_lock);
    return o ? 0 : (int)0x80028003;
}
static int event_match(unsigned have, unsigned want, unsigned mode)
{ return (mode & SCE_EVENT_WAITOR) ? (have & want) != 0 : (have & want) == want; }
int sceKernelWaitEventFlag(int evid, unsigned int bits, unsigned int wait, unsigned int *outBits, SceUInt *timeout)
{
    struct timespec dl; if (timeout) deadline_after(&dl, *timeout);
    int rc = 0;
    pthread_mutex_lock(&g_lock);
    for (;;) {
        shim_obj *o = obj_get(evid, OBJ_EVENT);
        if (!o) { rc = (int)0x80028003; break; }
        if (event_match(o->bits, bits, wait)) {
            if (outBits) *outBits = o->bits;
            if (wait & SCE_EVENT_WAITCLEAR) o->bits = 0;
            else if (wait & SCE_EVENT_WAITCLEAR_PAT) o->bits &= ~bits;
            break;
        }
        if (timeout) {
            g_waiters++; int t = pthread_cond_timedwait(&g_cond, &g_lock, &dl); g_waiters--;
            if (t == ETIMEDOUT) {
                o = obj_get(evid, OBJ_EVENT);
                if (o && event_match(o->bits, bits, wait)) continue;
                if (outBits && o) *outBits = o->bits;
                *timeout = 0; rc = (int)0x80028005; break;   /* SCE_KERNEL_ERROR_WAIT_TIMEOUT */
            }
        } else { g_waiters++; pthread_cond_wait(&g_cond, &g_lock); g_waiters--; }
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

/* ---- semaphores ---- */
SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, SceKernelSemaOptParam *option)
{
    (void)name; (void)attr; (void)option;
    SceUID id = obj_new(OBJ_SEMA);
    shim_obj *o = obj_get(id, OBJ_SEMA);
    if (o) { o->count = initVal; o->max = maxVal; }
    return id;
}
int sceKernelDeleteSema(SceUID semaid)
{
    shim_obj *o = obj_get(semaid, OBJ_SEMA);
    if (!o) return (int)0x80028003;
    pthread_mutex_lock(&g_lock); o->kind = OBJ_FREE; pthread_cond_broadcast(&g_cond); pthread_mutex_unlock(&g_lock);
    return 0;
}
int sceKernelSignalSema(SceUID semaid, int signal)
{
    pthread_mutex_lock(&g_lock);
    shim_obj *o = obj_get(semaid, OBJ_SEMA);
    int rc = 0;
    if (!o) rc = (int)0x80028003;
    else if (o->max > 0 && o->count + signal > o->max) rc = (int)0x8002801D;   /* SEMA_OVF */
    else { o->count += signal; if (g_waiters) pthread_cond_broadcast(&g_cond); }
    pthread_mutex_unlock(&g_lock);
    return rc;
}
int sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout)
{
    struct timespec dl; if (timeout) deadline_after(&dl, *timeout);
    int rc = 0;
    pthread_mutex_lock(&g_lock);
    for (;;) {
        shim_obj *o = obj_get(semaid, OBJ_SEMA);
        if (!o) { rc = (int)0x80028003; break; }
        if (o->count >= signal) { o->count -= signal; break; }
        if (timeout) {
            g_waiters++; int t = pthread_cond_timedwait(&g_cond, &g_lock, &dl); g_waiters--;
            if (t == ETIMEDOUT) {
                o = obj_get(semaid, OBJ_SEMA);
                if (o && o->count >= signal) continue;
                *timeout = 0; rc = (int)0x80028005; break;
            }
        } else { g_waiters++; pthread_cond_wait(&g_cond, &g_lock); g_waiters--; }
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}
int sceKernelPollSema(SceUID semaid, int signal)
{
    pthread_mutex_lock(&g_lock);
    shim_obj *o = obj_get(semaid, OBJ_SEMA);
    int rc = !o ? (int)0x80028003 : o->count >= signal ? (o->count -= signal, 0) : (int)0x80028007;
    pthread_mutex_unlock(&g_lock);
    return rc;
}

/* ---- memory blocks ---- */
SceUID sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt)
{
    (void)name; (void)type; (void)opt;
    if (!size) return (int)0x80020001;
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return (int)0x80020001;
    SceUID id = obj_new(OBJ_BLOCK);
    shim_obj *o = obj_get(id, OBJ_BLOCK);
    if (!o) { munmap(p, size); return id; }
    o->base = p; o->size = size;
    return id;
}
int sceKernelGetMemBlockBase(SceUID uid, void **base)
{
    shim_obj *o = obj_get(uid, OBJ_BLOCK);
    if (!o) return (int)0x80020001;
    *base = o->base; return 0;
}
int sceKernelFreeMemBlock(SceUID uid)
{
    shim_obj *o = obj_get(uid, OBJ_BLOCK);
    if (!o) return (int)0x80020001;
    munmap(o->base, o->size);
    pthread_mutex_lock(&g_lock); o->kind = OBJ_FREE; pthread_mutex_unlock(&g_lock);
    return 0;
}

/* ---- clib ---- */
int sceClibSnprintf(char *dst, SceSize dst_max_size, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); int n = vsnprintf(dst, dst_max_size, fmt, ap); va_end(ap); return n; }

/* ---- files: app0: -> the stage root; everything else is absent ---- */
static int host_path(const char *file, char *out, size_t n)
{
    if (strncmp(file, "app0:", 5)) return 0;
    const char *root = getenv("XV_HOST_APP0"); if (!root) root = "..";
    const char *rest = file + 5; if (*rest == '/') rest++;
    snprintf(out, n, "%s/%s", root, rest);
    return 1;
}
SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
    (void)mode; char path[1024];
    if (!host_path(file, path, sizeof path) || (flags & (SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC | SCE_O_APPEND)))
        return (SceUID)0x80010002;   /* ENOENT */
    int fd = open(path, O_RDONLY);
    return fd < 0 ? (SceUID)0x80010002 : fd;
}
int sceIoClose(SceUID fd) { return close(fd) ? (int)0x80010009 : 0; }
int sceIoRead(SceUID fd, void *data, SceSize size) { ssize_t r = read(fd, data, size); return r < 0 ? (int)0x80010005 : (int)r; }
int sceIoWrite(SceUID fd, const void *data, SceSize size) { (void)fd; (void)data; (void)size; return (int)0x80010009; }
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence) { off_t r = lseek(fd, (off_t)offset, whence); return r < 0 ? (SceOff)(int)0x80010016 : (SceOff)r; }
int sceIoMkdir(const char *dir, SceMode mode) { (void)dir; (void)mode; return (int)0x80010002; }

/* ---------------------------------------------------------------------------------------------------------------- */
/*  GXM                                                                                                              */
/* ---------------------------------------------------------------------------------------------------------------- */
int sceGxmMapMemory(void *base, SceSize size, SceGxmMemoryAttribFlags attr) { (void)base; (void)size; (void)attr; return 0; }
int sceGxmUnmapMemory(void *base) { (void)base; return 0; }
int sceGxmMapVertexUsseMemory(void *base, SceSize size, unsigned int *offset) { (void)base; (void)size; *offset = 0; return 0; }
int sceGxmMapFragmentUsseMemory(void *base, SceSize size, unsigned int *offset) { (void)base; (void)size; *offset = 0; return 0; }
int sceGxmUnmapVertexUsseMemory(void *base) { (void)base; return 0; }
int sceGxmUnmapFragmentUsseMemory(void *base) { (void)base; return 0; }

/* Render targets and surfaces: recording creates offscreen targets; nothing ever renders into them. */
int sceGxmGetRenderTargetMemSize(const SceGxmRenderTargetParams *params, unsigned int *driverMemSize) { (void)params; *driverMemSize = 0; return 0; }
int sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *params, SceGxmRenderTarget **renderTarget)
{ (void)params; static char dummy[64]; *renderTarget = (SceGxmRenderTarget *)dummy; return 0; }
int sceGxmDestroyRenderTarget(SceGxmRenderTarget *renderTarget) { (void)renderTarget; return 0; }
int sceGxmColorSurfaceInit(SceGxmColorSurface *surface, SceGxmColorFormat colorFormat, SceGxmColorSurfaceType surfaceType,
    SceGxmColorSurfaceScaleMode scaleMode, SceGxmOutputRegisterSize outputRegisterSize, unsigned int width, unsigned int height,
    unsigned int strideInPixels, void *data)
{ (void)colorFormat; (void)surfaceType; (void)scaleMode; (void)outputRegisterSize; (void)width; (void)height; (void)strideInPixels; (void)data; memset(surface, 0, sizeof *surface); return 0; }
int sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilFormat depthStencilFormat,
    SceGxmDepthStencilSurfaceType surfaceType, unsigned int strideInSamples, void *depthData, void *stencilData)
{ (void)depthStencilFormat; (void)surfaceType; (void)strideInSamples; (void)depthData; (void)stencilData; memset(surface, 0, sizeof *surface); return 0; }
void sceGxmDepthStencilSurfaceSetForceLoadMode(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilForceLoadMode forceLoad) { (void)surface; (void)forceLoad; }
void sceGxmDepthStencilSurfaceSetForceStoreMode(SceGxmDepthStencilSurface *surface, SceGxmDepthStencilForceStoreMode forceStore) { (void)surface; (void)forceStore; }

/* Context calls: replay only, never reached by recording; accept everything. */
int sceGxmBeginScene(SceGxmContext *context, unsigned int flags, const SceGxmRenderTarget *renderTarget, const SceGxmValidRegion *validRegion,
    SceGxmSyncObject *vertexSyncObject, SceGxmSyncObject *fragmentSyncObject, const SceGxmColorSurface *colorSurface,
    const SceGxmDepthStencilSurface *depthStencil)
{ (void)context; (void)flags; (void)renderTarget; (void)validRegion; (void)vertexSyncObject; (void)fragmentSyncObject; (void)colorSurface; (void)depthStencil; return 0; }
int sceGxmEndScene(SceGxmContext *context, const SceGxmNotification *vertexNotification, const SceGxmNotification *fragmentNotification)
{ (void)context; (void)vertexNotification; (void)fragmentNotification; return 0; }
int sceGxmDraw(SceGxmContext *context, SceGxmPrimitiveType primType, SceGxmIndexFormat indexType, const void *indexData, unsigned int indexCount)
{ (void)context; (void)primType; (void)indexType; (void)indexData; (void)indexCount; return 0; }
void sceGxmFinish(SceGxmContext *context) { (void)context; }
int sceGxmDisplayQueueFinish(void) { return 0; }
static uint8_t g_uniform_scratch[64 * 1024];
int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer) { (void)context; *uniformBuffer = g_uniform_scratch; return 0; }
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *context, void **uniformBuffer) { (void)context; *uniformBuffer = g_uniform_scratch; return 0; }
int sceGxmSetVertexDefaultUniformBuffer(SceGxmContext *context, const void *uniformBuffer) { (void)context; (void)uniformBuffer; return 0; }
int sceGxmSetUniformDataF(void *uniformBuffer, const SceGxmProgramParameter *parameter, unsigned int componentOffset, unsigned int componentCount, const float *sourceData)
{ (void)uniformBuffer; (void)parameter; (void)componentOffset; (void)componentCount; (void)sourceData; return 0; }
void sceGxmSetBackStencilFunc(SceGxmContext *context, SceGxmStencilFunc func, SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail, SceGxmStencilOp depthPass, unsigned char compareMask, unsigned char writeMask)
{ (void)context; (void)func; (void)stencilFail; (void)depthFail; (void)depthPass; (void)compareMask; (void)writeMask; }
void sceGxmSetFrontStencilFunc(SceGxmContext *context, SceGxmStencilFunc func, SceGxmStencilOp stencilFail, SceGxmStencilOp depthFail, SceGxmStencilOp depthPass, unsigned char compareMask, unsigned char writeMask)
{ (void)context; (void)func; (void)stencilFail; (void)depthFail; (void)depthPass; (void)compareMask; (void)writeMask; }
void sceGxmSetBackStencilRef(SceGxmContext *context, unsigned int sref) { (void)context; (void)sref; }
void sceGxmSetFrontStencilRef(SceGxmContext *context, unsigned int sref) { (void)context; (void)sref; }
void sceGxmSetBackVisibilityTestEnable(SceGxmContext *context, SceGxmVisibilityTestMode enable) { (void)context; (void)enable; }
void sceGxmSetFrontVisibilityTestEnable(SceGxmContext *context, SceGxmVisibilityTestMode enable) { (void)context; (void)enable; }
void sceGxmSetBackVisibilityTestIndex(SceGxmContext *context, unsigned int index) { (void)context; (void)index; }
void sceGxmSetFrontVisibilityTestIndex(SceGxmContext *context, unsigned int index) { (void)context; (void)index; }
void sceGxmSetBackVisibilityTestOp(SceGxmContext *context, SceGxmVisibilityTestOp op) { (void)context; (void)op; }
void sceGxmSetFrontVisibilityTestOp(SceGxmContext *context, SceGxmVisibilityTestOp op) { (void)context; (void)op; }
void sceGxmSetCullMode(SceGxmContext *context, SceGxmCullMode mode) { (void)context; (void)mode; }
void sceGxmSetFragmentProgram(SceGxmContext *context, const SceGxmFragmentProgram *fragmentProgram) { (void)context; (void)fragmentProgram; }
void sceGxmSetVertexProgram(SceGxmContext *context, const SceGxmVertexProgram *vertexProgram) { (void)context; (void)vertexProgram; }
int sceGxmSetFragmentTexture(SceGxmContext *context, unsigned int textureIndex, const SceGxmTexture *texture) { (void)context; (void)textureIndex; (void)texture; return 0; }
void sceGxmSetFrontDepthFunc(SceGxmContext *context, SceGxmDepthFunc depthFunc) { (void)context; (void)depthFunc; }
void sceGxmSetFrontDepthWriteEnable(SceGxmContext *context, SceGxmDepthWriteMode enable) { (void)context; (void)enable; }
int sceGxmSetVertexStream(SceGxmContext *context, unsigned int streamIndex, const void *streamData) { (void)context; (void)streamIndex; (void)streamData; return 0; }
void sceGxmSetViewport(SceGxmContext *context, float xOffset, float xScale, float yOffset, float yScale, float zOffset, float zScale)
{ (void)context; (void)xOffset; (void)xScale; (void)yOffset; (void)yScale; (void)zOffset; (void)zScale; }
int sceGxmSetVisibilityBuffer(SceGxmContext *context, void *bufferBase, unsigned int stridePerCore) { (void)context; (void)bufferBase; (void)stridePerCore; return 0; }
void sceGxmSetWClampEnable(SceGxmContext *context, SceGxmWClampMode enable) { (void)context; (void)enable; }
void sceGxmSetWClampValue(SceGxmContext *context, float clampValue) { (void)context; (void)clampValue; }

/* Shader patcher: registration keeps the program image; created programs are distinct dummy objects. */
int sceGxmShaderPatcherCreate(const SceGxmShaderPatcherParams *params, SceGxmShaderPatcher **shaderPatcher)
{ (void)params; static char patcher[64]; *shaderPatcher = (SceGxmShaderPatcher *)patcher; return 0; }
int sceGxmShaderPatcherDestroy(SceGxmShaderPatcher *shaderPatcher) { (void)shaderPatcher; return 0; }
int sceGxmShaderPatcherRegisterProgram(SceGxmShaderPatcher *shaderPatcher, const SceGxmProgram *programHeader, SceGxmShaderPatcherId *programId)
{ (void)shaderPatcher; *programId = (SceGxmShaderPatcherId)programHeader; return 0; }
int sceGxmShaderPatcherUnregisterProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId) { (void)shaderPatcher; (void)programId; return 0; }
int sceGxmShaderPatcherCreateVertexProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId, const SceGxmVertexAttribute *attributes,
    unsigned int attributeCount, const SceGxmVertexStream *streams, unsigned int streamCount, SceGxmVertexProgram **vertexProgram)
{ (void)shaderPatcher; (void)programId; (void)attributes; (void)attributeCount; (void)streams; (void)streamCount; *vertexProgram = malloc(16); return *vertexProgram ? 0 : (int)0x805B0001; }
int sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmShaderPatcherId programId, SceGxmOutputRegisterFormat outputFormat,
    SceGxmMultisampleMode multisampleMode, const SceGxmBlendInfo *blendInfo, const SceGxmProgram *vertexProgram, SceGxmFragmentProgram **fragmentProgram)
{ (void)shaderPatcher; (void)programId; (void)outputFormat; (void)multisampleMode; (void)blendInfo; (void)vertexProgram; *fragmentProgram = malloc(16); return *fragmentProgram ? 0 : (int)0x805B0001; }
int sceGxmShaderPatcherReleaseVertexProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmVertexProgram *vertexProgram) { (void)shaderPatcher; free(vertexProgram); return 0; }
int sceGxmShaderPatcherReleaseFragmentProgram(SceGxmShaderPatcher *shaderPatcher, SceGxmFragmentProgram *fragmentProgram) { (void)shaderPatcher; free(fragmentProgram); return 0; }

/* GXP reflection (Vita3K gxm/types.h: SceGxmProgram header, 16-byte SceGxmProgramParameter records). */
static uint32_t gxp_u32(const void *p, unsigned off) { uint32_t v; memcpy(&v, (const uint8_t *)p + off, 4); return v; }
int sceGxmProgramCheck(const SceGxmProgram *program)
{ return program && !memcmp(program, "GXP\0", 4) ? 0 : (int)0x805B0005; }
unsigned int sceGxmProgramGetSize(const SceGxmProgram *program) { return gxp_u32(program, 8); }
SceGxmProgramType sceGxmProgramGetType(const SceGxmProgram *program)
{ return (gxp_u32(program, 0x14) & 1u) ? SCE_GXM_FRAGMENT_PROGRAM : SCE_GXM_VERTEX_PROGRAM; }
SceBool sceGxmProgramIsDiscardUsed(const SceGxmProgram *program) { return (gxp_u32(program, 0x14) >> 3) & 1u; }
SceBool sceGxmProgramIsDepthReplaceUsed(const SceGxmProgram *program) { return (gxp_u32(program, 0x14) >> 4) & 1u; }
/* default_uniform_buffer_count at 0x64 (tools/test_depth_shader.py reads the same field). */
unsigned int sceGxmProgramGetDefaultUniformBufferSize(const SceGxmProgram *program) { return gxp_u32(program, 0x64) * 4u; }
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *program, const char *name)
{
    if (sceGxmProgramCheck(program)) return NULL;
    const uint8_t *base = (const uint8_t *)program;
    uint32_t count = gxp_u32(program, 0x24), size = gxp_u32(program, 8);
    const uint8_t *params = base + 0x28 + gxp_u32(program, 0x28);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = params + 16u * i;
        if (p + 16 > base + size) break;
        int32_t off; memcpy(&off, p, 4);
        const char *pn = (const char *)p + off;
        if ((const uint8_t *)pn >= base && (const uint8_t *)pn < base + size && !strcmp(pn, name))
            return (const SceGxmProgramParameter *)p;
    }
    return NULL;
}
static uint16_t param_bits(const SceGxmProgramParameter *p) { uint16_t v; memcpy(&v, (const uint8_t *)p + 4, 2); return v; }
SceGxmParameterCategory sceGxmProgramParameterGetCategory(const SceGxmProgramParameter *parameter) { return (SceGxmParameterCategory)(param_bits(parameter) & 15u); }
SceGxmParameterType sceGxmProgramParameterGetType(const SceGxmProgramParameter *parameter) { return (SceGxmParameterType)((param_bits(parameter) >> 4) & 15u); }
unsigned int sceGxmProgramParameterGetComponentCount(const SceGxmProgramParameter *parameter) { return (param_bits(parameter) >> 8) & 15u; }
unsigned int sceGxmProgramParameterGetContainerIndex(const SceGxmProgramParameter *parameter) { return (param_bits(parameter) >> 12) & 15u; }
unsigned int sceGxmProgramParameterGetArraySize(const SceGxmProgramParameter *parameter) { return gxp_u32(parameter, 8); }
unsigned int sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *parameter) { return gxp_u32(parameter, 12); }

/* Textures: Vita3K control-word layout, data field = pointer-table index (see the header comment). */
typedef struct {
    uint32_t w0, w1, w2, w3;
} shim_tex;
#define TEX_PTRS 65536u
static const void *g_tex_ptr[TEX_PTRS];
static unsigned g_tex_ptrs;
static pthread_mutex_t g_tex_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t tex_ptr_id(const void *p)
{
    if (!p) return 0;
    uint32_t id = 0;
    pthread_mutex_lock(&g_tex_lock);
    unsigned h = (unsigned)(((uintptr_t)p >> 4) * 2654435761u) & (TEX_PTRS - 1u);
    for (unsigned i = 0; i < TEX_PTRS; i++, h = (h + 1) & (TEX_PTRS - 1u)) {
        if (g_tex_ptr[h] == p) { id = h; break; }
        if (!g_tex_ptr[h]) { g_tex_ptr[h] = p; g_tex_ptrs++; id = h; break; }
    }
    pthread_mutex_unlock(&g_tex_lock);
    if (!id && g_tex_ptr[0] != p) { fprintf(stderr, "[shim] texture pointer table full\n"); abort(); }
    return id + 1u;   /* 0 stays "no data" */
}
static unsigned log2_floor(unsigned v) { unsigned l = 0; while (v > 1) { v >>= 1; l++; } return l; }
static int tex_init(SceGxmTexture *texture, const void *data, SceGxmTextureFormat fmt, unsigned w, unsigned h, unsigned mips, uint32_t type, int base2)
{
    shim_tex *t = (shim_tex *)texture;
    uint32_t f = (uint32_t)fmt;
    if (!w || !h || w > 4096 || h > 4096) return (int)0x805B0005;
    unsigned mip_field = mips ? (mips - 1u) & 15u : 0;
    t->w0 = (mip_field << 17) | (31u << 21) /* lod bias */ | ((f >> 31) << 31);
    uint32_t wh = base2 ? (log2_floor(h) | (log2_floor(w) << 16)) : (((h - 1u) & 0xFFFu) | (((w - 1u) & 0xFFFu) << 12));
    t->w1 = wh | (((f >> 24) & 0x1Fu) << 24) | ((type >> 29) << 29);
    t->w2 = tex_ptr_id(data) << 2;
    t->w3 = (((f >> 12) & 7u) << 28) | (1u << 31);
    return 0;
}
int sceGxmTextureInitSwizzled(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount)
{ return tex_init(texture, data, texFormat, width, height, mipCount, SCE_GXM_TEXTURE_SWIZZLED, 1); }
int sceGxmTextureInitCube(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount)
{ return tex_init(texture, data, texFormat, width, height, mipCount, SCE_GXM_TEXTURE_CUBE, 1); }
int sceGxmTextureInitLinear(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int mipCount)
{ return tex_init(texture, data, texFormat, width, height, mipCount, SCE_GXM_TEXTURE_LINEAR, 0); }
int sceGxmTextureInitLinearStrided(SceGxmTexture *texture, const void *data, SceGxmTextureFormat texFormat, unsigned int width, unsigned int height, unsigned int byteStride)
{
    int rc = tex_init(texture, data, texFormat, width, height, 1, SCE_GXM_TEXTURE_LINEAR_STRIDED, 0);
    if (!rc) { shim_tex *t = (shim_tex *)texture; t->w0 = (t->w0 & ~(0x3Fu << 21)) | (((byteStride >> 2) & 0x3Fu) << 21); }
    return rc;
}
#define TEX_FIELD(word, shift, bits) (((shim_tex *)texture)->word >> (shift) & ((1u << (bits)) - 1u))
#define TEX_SET(word, shift, bits, v) do { shim_tex *t_ = (shim_tex *)texture; t_->word = (t_->word & ~(((1u << (bits)) - 1u) << (shift))) | (((uint32_t)(v) & ((1u << (bits)) - 1u)) << (shift)); } while (0)
int sceGxmTextureSetUAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode addrMode) { TEX_SET(w0, 6, 3, addrMode); return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *texture, SceGxmTextureAddrMode addrMode) { TEX_SET(w0, 3, 3, addrMode); return 0; }
int sceGxmTextureSetMipFilter(SceGxmTexture *texture, SceGxmTextureMipFilter mipFilter) { TEX_SET(w0, 9, 1, mipFilter); return 0; }
int sceGxmTextureSetMinFilter(SceGxmTexture *texture, SceGxmTextureFilter minFilter) { TEX_SET(w0, 10, 2, minFilter); return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *texture, SceGxmTextureFilter magFilter) { TEX_SET(w0, 12, 2, magFilter); return 0; }
int sceGxmTextureSetLodBias(SceGxmTexture *texture, unsigned int bias) { TEX_SET(w0, 21, 6, bias); return 0; }
SceGxmTextureAddrMode sceGxmTextureGetUAddrMode(const SceGxmTexture *texture) { return (SceGxmTextureAddrMode)TEX_FIELD(w0, 6, 3); }
SceGxmTextureAddrMode sceGxmTextureGetVAddrMode(const SceGxmTexture *texture) { return (SceGxmTextureAddrMode)TEX_FIELD(w0, 3, 3); }
SceGxmTextureFilter sceGxmTextureGetMinFilter(const SceGxmTexture *texture) { return (SceGxmTextureFilter)TEX_FIELD(w0, 10, 2); }
SceGxmTextureFilter sceGxmTextureGetMagFilter(const SceGxmTexture *texture) { return (SceGxmTextureFilter)TEX_FIELD(w0, 12, 2); }
SceGxmTextureType sceGxmTextureGetType(const SceGxmTexture *texture) { return (SceGxmTextureType)(TEX_FIELD(w1, 29, 3) << 29); }
SceGxmTextureFormat sceGxmTextureGetFormat(const SceGxmTexture *texture)
{ return (SceGxmTextureFormat)((TEX_FIELD(w0, 31, 1) << 31) | (TEX_FIELD(w1, 24, 5) << 24) | (TEX_FIELD(w3, 28, 3) << 12)); }
static int tex_base2(const SceGxmTexture *texture)
{ uint32_t type = TEX_FIELD(w1, 29, 3) << 29; return type == SCE_GXM_TEXTURE_SWIZZLED || type == SCE_GXM_TEXTURE_CUBE; }
unsigned int sceGxmTextureGetWidth(const SceGxmTexture *texture) { return tex_base2(texture) ? 1u << TEX_FIELD(w1, 16, 4) : TEX_FIELD(w1, 12, 12) + 1u; }
unsigned int sceGxmTextureGetHeight(const SceGxmTexture *texture) { return tex_base2(texture) ? 1u << TEX_FIELD(w1, 0, 4) : TEX_FIELD(w1, 0, 12) + 1u; }
void *sceGxmTextureGetData(const SceGxmTexture *texture)
{ uint32_t id = TEX_FIELD(w2, 2, 30); return id ? (void *)g_tex_ptr[id - 1u] : NULL; }

/* ---------------------------------------------------------------------------------------------------------------- */
/*  main.c services                                                                                                  */
/* ---------------------------------------------------------------------------------------------------------------- */
volatile uint64_t xv_pump_us_acc;
void xv_render_target_drain(void) {}
void xv_present_drain(void) {}
void xv_settings_pipeline(int enabled) { (void)enabled; }
void xv_settings_frame_cap(unsigned cap) { (void)cap; }
void xv_settings_resolution(unsigned height) { (void)height; }
struct xv_dash_graphics;
struct xv_dash_graphics *xv_dash_graphics_create(const char *data_root) { (void)data_root; return NULL; }
const char *xv_dash_graphics_input(struct xv_dash_graphics *panel, uint32_t edge, int *value) { (void)panel; (void)edge; (void)value; return NULL; }
void xv_dash_graphics_snapshot(const struct xv_dash_graphics *panel, void *view) { (void)panel; (void)view; }
void xv_dash_graphics_status(struct xv_dash_graphics *panel, const char *message) { (void)panel; (void)message; }
void xv_log_get_status(xv_log_status *out) { memset(out, 0, sizeof *out); }
int xv_log_report_begin_frame(unsigned frame) { (void)frame; return 0; }

/* The published frame: wait for its CPU uploads, then complete its visibility queries with zero samples (the
 * pump's retirement, synchronously). Then the harness's 60-frame kernel reports, as softgfx.c's present did. */
extern uint32_t xv_ui_gxm_mesh_frame(void);
extern void xv_vertex_upload_wait(unsigned slot);
extern void xv_d3d_visibility_prepare(SceGxmContext *ctx, uint32_t frame, unsigned w, unsigned h);
extern void xv_d3d_visibility_complete(uint32_t frame);
extern unsigned xd3d_frame(void);
extern void xv_host_reports_present(unsigned) __attribute__((weak));
void xv_host_present_hook(uint32_t frame) __attribute__((weak));   /* optional per-frame observer (verify reports) */
void xv_present(void)
{
    uint32_t mesh = xv_ui_gxm_mesh_frame();
    if (mesh != UINT32_MAX) {
        xv_vertex_upload_wait(mesh % XV_FRAME_SLOTS);
        xv_d3d_visibility_prepare(NULL, mesh, 640, 480);
        if (xv_host_present_hook) xv_host_present_hook(mesh);
        xv_d3d_visibility_complete(mesh);
    }
    if (xv_host_reports_present) xv_host_reports_present(xd3d_frame());
}

/* Runtime bring-up in main.c's recomp order (shader patcher, UI bridge, mesh path), before the guest starts. */
#include "../../shaders/xv_layouts.h"
extern int xv_ui_gxm_init(void);
extern int xv_d3d_init(const xv_vs_desc_t *const *table, unsigned count);
extern uint32_t xv_d3d_RegisterVertexShader(const xv_vs_desc_t *desc);
extern void xv_d3d_set_clear_shader(uint32_t handle);
extern void xv_d3d_configure_render_preparation(void);
extern void xv_cutout_override(int enabled);
void xv_host_runtime_init(void)
{
    if (xv_shader_init() != 0) { fprintf(stderr, "[shim] shader patcher init failed\n"); exit(3); }
    if (xv_ui_gxm_init() != 0) { fprintf(stderr, "[shim] UI GXM bridge init failed\n"); exit(3); }
    if (xv_d3d_init(xv_halo_vs, XV_HALO_VS_COUNT) != 0) { fprintf(stderr, "[shim] xv_d3d init failed\n"); exit(3); }
    xv_d3d_set_clear_shader(xv_d3d_RegisterVertexShader(&xv_vs_clear));
    xv_d3d_configure_render_preparation();
    xv_cutout_override(0);
    fprintf(stderr, "[shim] host runtime up: GXM recording bridge linked (no replay, visibility = 0 samples)\n");
}
