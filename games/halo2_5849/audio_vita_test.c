/* The real worker/lifecycle code, with a concurrent fake Vita platform. */
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
typedef int SceUID;
typedef unsigned SceSize;
typedef unsigned SceUInt;
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_MODE_STEREO 1
#define SCE_AUDIO_VOLUME_0DB 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
#define SCE_KERNEL_CPU_MASK_USER_ALL 0x70000
static int sceKernelCreateMutex(const char *, int, int, void *);
static int sceKernelDeleteMutex(int);
static int sceKernelLockMutex(int, int, void *);
static int sceKernelUnlockMutex(int, int);
static int sceAudioOutOpenPort(int, int, int, int);
static int sceAudioOutSetVolume(int, int, const int *);
static int sceAudioOutReleasePort(int);
static int sceAudioOutOutput(int, const void *);
static int sceAudioOutGetRestSample(int);
static int sceKernelDelayThread(unsigned);
static uint64_t sceKernelGetProcessTimeWide(void);
static int sceKernelCreateSema(const char *, int, int, int, void *);
static int sceKernelDeleteSema(int);
static int sceKernelSignalSema(int, int);
static int sceKernelWaitSema(int, int, SceUInt *);
static int sceKernelCreateThread(const char *, int (*)(SceSize, void *), int, unsigned, int, int, void *);
static int sceKernelDeleteThread(int);
static int sceKernelStartThread(int, unsigned, void *);
static int sceKernelWaitThreadEnd(int, void *, SceUInt *);
#define H2_AUDIO_PLATFORM_TEST 1
#include "audio_vita.c"
#include "audio_bins.c"

enum { F_MUTEX, F_PROGRESS, F_PORT, F_VOLUME, F_SEMA, F_THREAD, F_START, F_READY, F_WRITE, F_JOIN, F_RELEASE, F_REST, F_REVERSE, F_STALL, F_FX_SLOW, F_HOLD };
static atomic_uint faults;
static unsigned resources;
static atomic_uint outputs, drains, active, queued;
static int fake_voice = -1;
static uint32_t fake_decoded;
static int fake_playing;
static pthread_t native_thread;
static pthread_mutex_t native_mutex[2];
static sem_t native_ready;
static int launched, joined;
#if H2_AUDIO_DSP
static atomic_int hold_on_stream_submit;
#endif
static int (*native_entry)(SceSize, void *);
static int failing(unsigned bit) { return (atomic_load(&faults) & (1u << bit)) != 0; }
void xv_logf(const char *format, ...) { (void)format; }
#ifdef H2_AUDIO_TEST_REAL_MIXER
#define xk_os_audio_open h2_audio_sink_open
#define xk_os_audio_mutex_lock h2_audio_sink_lock
#define xk_os_audio_mutex_unlock h2_audio_sink_unlock
#define XK_AUDIO_OUTPUT_FILTER h2_audio_bins_filter
#include "audio_mixer_bridge.c"
#undef xk_os_audio_open
#undef xk_os_audio_mutex_lock
#undef xk_os_audio_mutex_unlock
#undef XK_AUDIO_OUTPUT_FILTER
void xk_os_log(const char *format, ...) { (void)format; }
uint64_t xk_os_monotonic_us(void) { return sceKernelGetProcessTimeWide(); }
#else
#if H2_AUDIO_DSP
int h2_audio_stream_cursor_read(int voice,h2_stream_cursor *out) { (void)voice;(void)out;return 0; }
int xk_audio_stream_push(int voice,uint32_t guest,uint32_t size) { (void)voice;(void)guest;(void)size;return -1; }
int xk_audio_stream_pop_consumed(int voice) { (void)voice;return 0; }
#endif
int xk_audio_init(void) { fake_voice = -1; fake_decoded = 0; fake_playing = 0; return h2_audio_sink_open(XA_OUT_RATE, XA_GRAIN); }
void xk_audio_lock(void) { h2_audio_sink_lock(); }
void xk_audio_unlock(void) { h2_audio_sink_unlock(); }
void xk_audio_mix(int16_t *out, int count)
{
    assert(count == XA_GRAIN && !((uintptr_t)out & 63));
    h2_audio_sink_lock();
    for (int i = 0; i < count * 2; ++i) out[i] = (i & 1) ? -4321 : 1234;
    if (fake_playing) fake_decoded = (fake_decoded + 4096) % 106496;
    h2_audio_sink_unlock();
}
void xk_audio_voice_play(int voice, int loop)
{ assert(voice == 0 && loop == 1); xk_audio_lock(); fake_voice = voice; fake_playing = 1; xk_audio_unlock(); }
void xk_audio_voice_stop(int voice)
{ assert(voice == fake_voice); xk_audio_lock(); fake_playing = 0; xk_audio_unlock(); }
int xk_audio_voice_playing(int voice) {
#if H2_AUDIO_DSP
    return voice == fake_voice && fake_playing;
#else
    assert(voice == fake_voice); return fake_playing;
#endif
}
void xk_audio_voice_set_pos(int voice, uint32_t position)
{ assert(voice == fake_voice && !position); xk_audio_lock(); fake_decoded = position; xk_audio_unlock(); }
uint32_t xk_audio_voice_pos(int voice) { assert(voice == fake_voice); return fake_decoded; }
uint32_t xk_audio_free_voices(void) { return 42; }
#endif
static int sceKernelCreateMutex(const char *name, int a, int b, void *p)
{
    unsigned index = !strcmp(name, "h2_audio_progress"), bit = index ? 16 : 1;
    assert(!a && !b && !p && !(resources & bit));
    if (failing(index ? F_PROGRESS : F_MUTEX)) return -10;
    assert(!pthread_mutex_init(&native_mutex[index], NULL)); resources |= bit; return 10 + index;
}
static int sceKernelDeleteMutex(int id)
{
    unsigned index = id - 10, bit = index ? 16 : 1; assert(index < 2 && (resources & bit) && !atomic_load(&active));
    assert(!pthread_mutex_destroy(&native_mutex[index])); resources &= ~bit; return 0;
}
static int sceKernelLockMutex(int id, int n, void *p)
{ assert(id >= 10 && id <= 11 && n == 1 && !p); return pthread_mutex_lock(&native_mutex[id - 10]); }
static int sceKernelUnlockMutex(int id, int n)
{ assert(id >= 10 && id <= 11 && n == 1); return pthread_mutex_unlock(&native_mutex[id - 10]); }
static int sceAudioOutOpenPort(int type, int grain, int rate, int mode)
{
    assert(type == 0 && grain == XA_GRAIN && rate == XA_OUT_RATE && mode == 1 && !(resources & 2));
    if (failing(F_PORT)) return -20;
    resources |= 2; return 20;
}
static int sceAudioOutSetVolume(int id, int mask, const int *volume)
{ assert(id == 20 && mask == 3 && volume[0] == 32768 && volume[1] == 32768); return failing(F_VOLUME) ? -21 : 0; }
static int sceAudioOutReleasePort(int id)
{
    assert(id == 20 && (resources & 2) && !atomic_load(&active));
    if (failing(F_RELEASE)) return -22;
    resources &= ~2u; return 0;
}
static int sceAudioOutOutput(int id, const void *data)
{
    assert(id == 20 && (resources & 2));
    if (!data) { assert(!atomic_load(&queued)); atomic_fetch_add(&drains, 1); return 0; }
    const int16_t *samples = data;
#if H2_AUDIO_DSP
    if (fx.playing) {
        /* A retained old grain may precede the new source's first grain. */
        assert(samples[0] == 1953 || samples[0] == 3906 || samples[0] == 5859 || samples[0] == 7812);
        for (unsigned i = 0; i < XA_GRAIN * 2; ++i) assert(samples[i] == samples[0]);
    }
    else
#endif
#ifdef H2_AUDIO_TEST_REAL_MIXER
        for (unsigned i=0;i<XA_GRAIN*2;++i) assert(!samples[i]);
#else
        assert(samples[0] == 1234 && samples[XA_GRAIN * 2 - 1] == -4321);
#endif
    if (failing(F_WRITE)) return -23;
    assert(!atomic_load(&queued)); atomic_store(&queued, XA_GRAIN);
#if H2_AUDIO_DSP
    if (gp_stream_decoded && atomic_exchange(&hold_on_stream_submit,0)) atomic_fetch_or(&faults,1u<<F_HOLD);
#endif
    atomic_fetch_add(&outputs, 1); usleep(1000); return 0;
}
static int sceAudioOutGetRestSample(int id)
{
    assert(id == 20); unsigned remaining = atomic_load(&queued);
    if (failing(F_REST)) return -24;
    if (failing(F_REVERSE)) return XA_GRAIN + 1;
    if (failing(F_STALL)) return (int)remaining;
    if (failing(F_HOLD)) return (int)remaining;
    if (failing(F_FX_SLOW)) { atomic_store(&queued, 0); return 0; }
    if (remaining) { assert(remaining >= 128); remaining -= 128; atomic_store(&queued, remaining); }
    return (int)remaining;
}
static int sceKernelDelayThread(unsigned delay) { assert(delay == 1000); usleep(delay); return 0; }
static uint64_t sceKernelGetProcessTimeWide(void)
{
    static atomic_uint slow_time;
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    uint32_t extra = failing(F_FX_SLOW) ? atomic_fetch_add(&slow_time, 50000) : atomic_load(&slow_time);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000 + (failing(F_STALL) ? 2000000 : 0) + extra;
}
static int sceKernelCreateSema(const char *name, int a, int initial, int maximum, void *p)
{
    (void)name; assert(!a && !initial && maximum == 1 && !p && !(resources & 4));
    if (failing(F_SEMA)) return -30;
    assert(!sem_init(&native_ready, 0, 0)); resources |= 4; return 30;
}
static int sceKernelDeleteSema(int id)
{ assert(id == 30 && (resources & 4) && !atomic_load(&active)); assert(!sem_destroy(&native_ready)); resources &= ~4u; return 0; }
static int sceKernelSignalSema(int id, int count)
{ assert(id == 30 && count == 1); return sem_post(&native_ready); }
static int sceKernelWaitSema(int id, int count, SceUInt *timeout)
{
    assert(id == 30 && count == 1 && *timeout == 2000000);
    if (failing(F_READY)) return -31;
    struct timespec limit; assert(!clock_gettime(CLOCK_REALTIME, &limit)); limit.tv_sec += 2;
    return sem_timedwait(&native_ready, &limit);
}
static void *thread_entry(void *p)
{
    (void)p; atomic_store(&active, 1); native_entry(0, NULL); atomic_store(&active, 0); return NULL;
}
static int sceKernelCreateThread(const char *name, int (*fn)(SceSize, void *), int priority, unsigned stack, int flags, int affinity, void *p)
{
    (void)name; assert(priority == 80 && stack == 65536 && !flags && affinity == SCE_KERNEL_CPU_MASK_USER_ALL && !p && !(resources & 8));
    if (failing(F_THREAD)) return -40;
    native_entry = fn; resources |= 8; launched = joined = 0; return 40;
}
static int sceKernelDeleteThread(int id)
{ assert(id == 40 && (resources & 8) && (!launched || joined) && !atomic_load(&active)); resources &= ~8u; return 0; }
static int sceKernelStartThread(int id, unsigned size, void *p)
{
    assert(id == 40 && !size && !p);
    if (failing(F_START)) return -41;
    launched = 1; assert(!pthread_create(&native_thread, NULL, thread_entry, NULL)); return 0;
}
static int sceKernelWaitThreadEnd(int id, void *status, SceUInt *timeout)
{
    assert(id == 40 && !status && *timeout == 2000000 && launched && !joined);
    if (failing(F_JOIN)) return -42;
    assert(!pthread_join(native_thread, NULL)); joined = 1; return 0;
}
int main(void)
{
    for (unsigned f = F_MUTEX; f <= F_WRITE; ++f) {
        atomic_store(&faults, 1u << f);
        assert(h2_audio_backend_open() == -1 && !resources && !atomic_load(&active));
        assert(h2_audio_backend_health() == -1);
    }
    atomic_store(&faults, 0);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        unsigned old_outputs = atomic_load(&outputs), old_drains = atomic_load(&drains);
        assert(h2_audio_backend_open() == 0 && resources == 31);
        assert(h2_audio_backend_open() == -2 && resources == 31); /* never resets a live device */
        assert(h2_audio_backend_health() == 0 && h2_audio_backend_free_voices() == 42);
        h2_audio_backend_status state; h2_audio_backend_snapshot(&state);
        assert(state.grains >= 1 && state.nonzero_grains >= 1 && state.peak == 4321 && !state.error);
        assert(state.last_peak_left == 1234 && state.last_peak_right == 4321);
        uint8_t bins[32]; h2_audio_bins_snapshot(bins);
        for (unsigned b = 0; b < 32; ++b) assert(bins[b] == (b != 31));
        assert(h2_audio_backend_set_headroom(31, 0xFF) == 0);
        h2_audio_bins_snapshot(bins); assert(bins[31] == 0xFF);
        assert(atomic_load(&outputs) > old_outputs && state.port == 20 && state.thread == 40);
        uint32_t play = 0, write = 0;
        assert(h2_audio_backend_play(0, 106496, 44100) == 0);
        assert(h2_audio_backend_play(0, 106496, 44100) < 0); /* restart not in this contract */
        assert(h2_audio_backend_cursor(1, &play, &write) < 0);
        for (unsigned poll = 0; poll < 100 && !play; ++poll) {
            assert(h2_audio_backend_cursor(0, &play, &write) == 0); usleep(1000);
        }
        assert(play && !(play & 3) && play < 106496 && !(write & 3) && write < 106496);
        uint32_t voice_status = 99;
        assert(h2_audio_backend_status_voice(0, &voice_status) == 0 && voice_status == 5);
        assert(h2_audio_backend_forget(0) < 0 && h2_audio_backend_rewind(0) < 0);
        assert(h2_audio_backend_stop(1) < 0);
        assert(h2_audio_backend_stop(0) == 0);
        assert(h2_audio_backend_status_voice(0, &voice_status) == 0 && !voice_status);
        assert(h2_audio_backend_cursor(0, &play, &write) == 0 && play == write);
        uint32_t stopped_position = play; usleep(20000);
        assert(h2_audio_backend_cursor(0, &play, &write) == 0 && play == stopped_position && write == play);
        assert(h2_audio_backend_play(0, 106496, 44100) < 0);
        assert(h2_audio_backend_stop(0) == 0 && h2_audio_backend_rewind(0) == 0);
        assert(h2_audio_backend_cursor(0, &play, &write) == 0 && !play && !write);
        assert(h2_audio_backend_play(0, 106496, 44100) == 0);
        assert(h2_audio_backend_stop(0) == 0 && h2_audio_backend_forget(0) == 0);
        assert(h2_audio_backend_cursor(0, &play, &write) < 0);
        assert(h2_audio_backend_play(0, 106496, 44100) == 0);
        assert(h2_audio_backend_stop(0) == 0 && h2_audio_backend_forget(0) == 0);
        assert(h2_audio_backend_close() == 0 && !resources && !atomic_load(&active) && !atomic_load(&queued));
        assert(atomic_load(&drains) == old_drains + 1 && h2_audio_backend_health() == -1);
        assert(h2_audio_backend_close() == 0); /* harmless duplicate shutdown */
        assert(h2_audio_backend_set_headroom(0, 0) == -1);
    }
    /* A join failure must retain everything that a running worker could use. */
    assert(h2_audio_backend_open() == 0);
    atomic_store(&faults, 1u << F_JOIN);
    assert(h2_audio_backend_close() == -1 && resources == 31);
    atomic_store(&faults, 0); assert(h2_audio_backend_close() == 0 && !resources);
    /* A failed, invalid or stalled observation cannot invent a cursor or
     * free an unverified outstanding grain. Retain resources until real
     * successful observations drain it; do not join a terminated worker twice. */
    for (unsigned f = F_REST; f <= F_STALL; ++f) {
        assert(h2_audio_backend_open() == 0);
        assert(h2_audio_backend_play(0, 106496, 44100) == 0);
        for (;;) {
            sceKernelLockMutex(progress_mutex, 1, NULL);
            if (progress.pending) { atomic_store(&faults, 1u << f); sceKernelUnlockMutex(progress_mutex, 1); break; }
            sceKernelUnlockMutex(progress_mutex, 1); usleep(1000);
        }
        uint32_t play = 123, write = 456;
        assert(h2_audio_backend_stop(0) < 0);
        assert(h2_audio_backend_cursor(0, &play, &write) < 0 && play == 123 && write == 456);
        assert(h2_audio_backend_close() < 0 && resources == 31 && !atomic_load(&active));
        atomic_store(&faults, 0);
        int rc = -1; for (unsigned tries = 0; tries < 10 && rc; ++tries) rc = h2_audio_backend_close();
        assert(!rc && !resources && !atomic_load(&queued));
    }
    /* Failed rollback is a separate terminal result, never NODRIVER success. */
    atomic_store(&faults, (1u << F_THREAD) | (1u << F_RELEASE));
    assert(h2_audio_backend_open() == -2 && resources == 2);
    atomic_store(&faults, 0); assert(h2_audio_backend_close() == 0 && !resources);
    puts("Halo 2 concurrent audio worker/lifecycle tests passed"); return 0;
}
