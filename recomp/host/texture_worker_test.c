/* Actual worker implementation with pthread-backed Vita synchronization.
 * Check output/lifetime, serial fallback, startup failures, and shutdown. */
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../runtime/xv_texture_worker.c"

static sem_t semaphores[8];
static unsigned next_sema, live_semas, fail_step, step, cleaned, starts;
static pthread_t native_thread, caller;
static SceKernelThreadEntry entry;
static int running;
void xv_logf(const char *fmt, ...) { (void)fmt; }
void xv_cpu_log_thread(const char *role) { assert(!strcmp(role, "texture-decode")); }
SceUID sceKernelCreateSema(const char *n, SceUInt a, int init, int max, SceKernelSemaOptParam *o)
{
    (void)n; (void)a; (void)o; assert(max == 1 && init == 0);
    if (++step == fail_step) return -1;
    unsigned id = ++next_sema; assert(id < 8);
    assert(!sem_init(&semaphores[id], 0, 0)); live_semas++; return id;
}
int sceKernelDeleteSema(SceUID id) { assert(!sem_destroy(&semaphores[id])); live_semas--; return 0; }
int sceKernelSignalSema(SceUID id, int n) { assert(n == 1); return sem_post(&semaphores[id]); }
int sceKernelWaitSema(SceUID id, int n, SceUInt *t) { (void)t; assert(n == 1); return sem_wait(&semaphores[id]); }
SceUID sceKernelCreateThread(const char *n, SceKernelThreadEntry e, int p, SceSize s, SceUInt a, int mask, const SceKernelThreadOptParam *o)
{
    (void)n; (void)p; (void)s; (void)a; (void)o;
    assert(mask == SCE_KERNEL_CPU_MASK_USER_0);
    if (++step == fail_step) return -1;
    entry = e; return 100;
}
static void *run(void *arg) { (void)arg; entry(0, NULL); return NULL; }
int sceKernelStartThread(SceUID id, SceSize n, void *p)
{
    (void)id; (void)n; (void)p;
    if (++step == fail_step) return -1;
    assert(!pthread_create(&native_thread, NULL, run, NULL)); starts++; running = 1; return 0;
}
int sceKernelWaitThreadEnd(SceUID id, int *s, SceUInt *t)
{ (void)id; (void)s; (void)t; assert(!pthread_join(native_thread, NULL)); running = 0; return 0; }
int sceKernelDeleteThread(SceUID id) { (void)id; assert(!running); return 0; }
int sceKernelGetThreadCurrentPriority(void) { return 64; }
int sceKernelDelayThread(SceUInt us) { usleep(us); return 0; }
SceUInt64 sceKernelGetProcessTimeWide(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
void xv_gpu_write_barrier(void)
{ assert(!pthread_equal(pthread_self(), caller)); cleaned++; }

static void reset(void)
{
    xv_texture_worker_shutdown(); assert(!running && !live_semas);
    next_sema = step = fail_step = 0;
}
static unsigned encode_block(unsigned x,unsigned y,unsigned w,unsigned h)
{
    unsigned index=0,bit=0;
    for (unsigned scale=1;scale<w || scale<h;scale*=2) {
        if (scale<h) { if (y&scale) index|=1u<<bit; ++bit; }
        if (scale<w) { if (x&scale) index|=1u<<bit; ++bit; }
    }
    return index;
}

int main(void)
{
    caller = pthread_self();
    uint8_t *src = malloc(2u << 20);
    uint32_t *out, *expected = malloc(2u << 20), palette[256];
    assert(src && expected && !posix_memalign((void **)&out, 64, 2u << 20));
    for (unsigned i = 0; i < (2u << 20); i++) src[i] = (i * 37u + (i >> 8)) & 255;
    for (unsigned i = 0; i < 256; i++) palette[i] = i * 0x01372189u;
    unsigned formats[] = {0x0C,0x0E,0x0F,0x12,0x1E,0x3F,0x40,0x41,0x06,0x07,
        0x02,0x03,0x04,0x05,0x10,0x11,0x1C,0x1D,0x0B,0,0x19,1,0x13,0x1F,0x1B,0x1A,0x20};
    unsigned sizes[][2] = {{128,128},{256,64},{64,256},{32,512},{512,32},{256,256},{7,5},{16,16}};
    unsigned cases = 0;
    for (unsigned pass = 0; pass < 4; pass++) for (unsigned f = 0; f < sizeof formats / sizeof *formats; f++)
    for (unsigned s = 0; s < sizeof sizes / sizeof *sizes; s++) {
        unsigned w = sizes[s][0], h = sizes[s][1];
        xv_texture_job j = { src, expected, pass & 1 ? palette : NULL, formats[f], w, h, w * 4 + 64, 0, pass == 2, pass == 3, 0 };
        memset(expected, 0xAB, w*h*4 + 64); memset(out, 0xAB, w*h*4 + 64);
        assert(!xv_tex_decode_range(&j, 0, xv_tex_units(&j)));
        j.dst = out; assert(!xv_texture_decode(&j));
        assert(!memcmp(out, expected, w*h*4 + 64)); cases++;
        /* Descriptor/source lifetime ends at return: poison before next task. */
        memset(out, 0xCC, w*h*4); src[0] ^= 0x5A;
    }
    assert(cleaned == jobs && jobs > 500 && starts == 1);
    unsigned bc_cases=0;
    for (unsigned fmt=0;fmt<3;++fmt) for (unsigned lx=0;lx<=10;++lx) for (unsigned ly=0;ly<=10;++ly) {
        unsigned w=1u<<lx,h=1u<<ly,bw=(w+3)/4,bh=(h+3)/4,block=fmt ? 16 : 8;
        unsigned bytes=bw*bh*block;
        memset(expected,0xAB,bytes+64);memset(out,0xAB,bytes+64);
        for (unsigned y=0;y<bh;++y) for (unsigned x=0;x<bw;++x)
            memcpy((uint8_t *)expected+encode_block(x,y,bw,bh)*block,src+(y*bw+x)*block,block);
        xv_texture_job bc={src,out,NULL,fmt==0 ? 0x0C : fmt==1 ? 0x0E : 0x0F,w,h,0,0,0,0,1};
        assert(!xv_texture_decode(&bc));assert(!memcmp(out,expected,bytes+64));
        memset(out,0xCC,bytes);bc_cases++;
    }
    xv_texture_worker_report();
    reset();
    xv_texture_job j = { src, out, palette, 0x0B, 128, 128, 128, 0, 0, 0, 0 };
    unsigned before = cleaned;
    for (unsigned fail = 1; fail <= 4; fail++) {
        fail_step = fail; assert(!xv_texture_decode(&j)); assert(cleaned == before);
        reset();
    }
    setenv("XV_TEXTURE_WORKER", "0", 1);
    assert(!xv_texture_decode(&j) && !live_semas && cleaned == before); reset();
    unsetenv("XV_TEXTURE_WORKER");
    assert(!xv_texture_decode(&j) && cleaned == before + 1); reset();
    free(src); free(out); free(expected);
    printf("texture worker: %u format/size/palette cases, buffer lifetime, fallback and shutdown passed\n", cases);
    printf("compressed block worker: %u square/rectangle/mip-tail cases match independent layout, with output guards\n",bc_cases);
    return 0;
}
