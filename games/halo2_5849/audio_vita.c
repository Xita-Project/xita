/* Halo 2 owns this output worker; the shared mixer algorithms are unchanged.
 * Its OS calls are renamed at compile time, so the CE sink is not selected. */
#include "audio_host.h"
#include "audio_bins.h"
#include "recomp/kernel/xk_audio.h"
#ifndef H2_AUDIO_PLATFORM_TEST
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#endif
#include <string.h>

static SceUID port = -1, mutex = -1, worker = -1, ready = -1;
static int started, running;
static uint32_t grains, nonzero_grains, peak, error;
static uint32_t last_peak_left, last_peak_right;
static _Alignas(64) int16_t output[2][XA_GRAIN * 2];
extern void xv_logf(const char *, ...);

int h2_audio_sink_open(int rate, int grain)
{
    if (port >= 0 || mutex >= 0 || rate != XA_OUT_RATE || grain != XA_GRAIN) return -1;
    mutex = sceKernelCreateMutex("h2_audio_mix", 0, 0, NULL);
    if (mutex < 0) return -1;
    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, grain, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return -1;
    int volume[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    if (sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volume) < 0) {
        return -1;
    }
    return 0;
}
void h2_audio_sink_lock(void) { sceKernelLockMutex(mutex, 1, NULL); }
void h2_audio_sink_unlock(void) { sceKernelUnlockMutex(mutex, 1); }

static int mix_worker(SceSize bytes, void *arg)
{
    (void)bytes; (void)arg;
    unsigned slot = 0, first = 1;
    while (__atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        xk_audio_mix(output[slot], XA_GRAIN);
        int rc = sceAudioOutOutput(port, output[slot]);
        if (rc >= 0) {
            uint32_t maximum = 0;
            uint32_t channel_peak[2] = {0};
            for (unsigned i = 0; i < XA_GRAIN * 2; ++i) {
                int value = output[slot][i];
                uint32_t absolute = value < 0 ? (uint32_t)-value : (uint32_t)value;
                if (absolute > maximum) maximum = absolute;
                if (absolute > channel_peak[i & 1]) channel_peak[i & 1] = absolute;
            }
            __atomic_store_n(&last_peak_left, channel_peak[0], __ATOMIC_RELAXED);
            __atomic_store_n(&last_peak_right, channel_peak[1], __ATOMIC_RELAXED);
            __atomic_add_fetch(&grains, 1, __ATOMIC_RELAXED);
            if (maximum) __atomic_add_fetch(&nonzero_grains, 1, __ATOMIC_RELAXED);
            if (maximum > __atomic_load_n(&peak, __ATOMIC_RELAXED))
                __atomic_store_n(&peak, maximum, __ATOMIC_RELAXED);
        } else __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE);
        if (first) { first = 0; sceKernelSignalSema(ready, 1); }
        if (rc < 0) break;
        slot ^= 1; /* Keep the last submitted grain immutable while mixing. */
    }
    /* Complete the retained buffer before close can release port or storage. */
    int drained = sceAudioOutOutput(port, NULL);
    if (drained < 0) __atomic_store_n(&error, (uint32_t)drained, __ATOMIC_RELEASE);
    return 0;
}
int h2_audio_backend_close(void)
{
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    if (worker >= 0 && started) {
        SceUInt timeout = 2000000;
        if (sceKernelWaitThreadEnd(worker, NULL, &timeout) < 0) return -1;
    }
    int result = 0;
    started = 0;
    if (worker >= 0) { if (sceKernelDeleteThread(worker) < 0) result = -1; else worker = -1; }
    if (ready >= 0) { if (sceKernelDeleteSema(ready) < 0) result = -1; else ready = -1; }
    if (port >= 0) { if (sceAudioOutReleasePort(port) < 0) result = -1; else port = -1; }
    if (mutex >= 0) { if (sceKernelDeleteMutex(mutex) < 0) result = -1; else mutex = -1; }
    return result;
}
static int open_failed(void) { return h2_audio_backend_close() < 0 ? -2 : -1; }
int h2_audio_backend_open(void)
{
    if (port >= 0 || worker >= 0 || mutex >= 0 || ready >= 0) return -2;
    grains = nonzero_grains = peak = error = 0;
    last_peak_left = last_peak_right = 0;
    memset(output, 0, sizeof output);
    if (xk_audio_init() < 0) return open_failed();
    h2_audio_bins_reset();
    ready = sceKernelCreateSema("h2_audio_ready", 0, 0, 1, NULL);
    if (ready < 0) return open_failed();
    worker = sceKernelCreateThread("h2_audio_output", mix_worker, 80, 64 * 1024, 0,
                                   SCE_KERNEL_CPU_MASK_USER_ALL, NULL);
    if (worker < 0) return open_failed();
    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
    if (sceKernelStartThread(worker, 0, NULL) < 0) return open_failed();
    started = 1;
    SceUInt timeout = 2000000;
    if (sceKernelWaitSema(ready, 1, &timeout) < 0 || __atomic_load_n(&error, __ATOMIC_ACQUIRE)) {
        return open_failed();
    }
    xv_logf("[h2/audio] Vita sink accepted first grain: port=%d thread=%d rate=%d stereo grain=%d\n",
            port, worker, XA_OUT_RATE, XA_GRAIN);
    return 0;
}
int h2_audio_backend_health(void)
{ return port >= 0 && started && !__atomic_load_n(&error, __ATOMIC_ACQUIRE) ? 0 : -1; }
uint32_t h2_audio_backend_free_voices(void) { return xk_audio_free_voices(); }
int h2_audio_backend_set_headroom(uint32_t bin, uint32_t amount)
{ return h2_audio_backend_health() < 0 ? -1 : h2_audio_bins_set(bin, amount); }
void h2_audio_backend_snapshot(h2_audio_backend_status *out)
{
    *out = (h2_audio_backend_status){
        .grains = __atomic_load_n(&grains, __ATOMIC_RELAXED),
        .nonzero_grains = __atomic_load_n(&nonzero_grains, __ATOMIC_RELAXED),
        .peak = __atomic_load_n(&peak, __ATOMIC_RELAXED),
        .error = __atomic_load_n(&error, __ATOMIC_ACQUIRE), .port = port, .thread = worker,
        .last_peak_left = __atomic_load_n(&last_peak_left, __ATOMIC_RELAXED),
        .last_peak_right = __atomic_load_n(&last_peak_right, __ATOMIC_RELAXED)
    };
}
