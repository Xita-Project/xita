/* Shared real PCM mixer and read-only capacity query, without a host device. */
#include "recomp/xv_x86rt.h"
#include "recomp/kernel/xk_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static unsigned locked;
void xv_logf(const char *format, ...) { (void)format; }
void xk_os_log(const char *format, ...) { (void)format; }
int xk_os_audio_open(int rate, int grain) { assert(rate == XA_OUT_RATE && grain == XA_GRAIN); return 0; }
void xk_os_audio_mutex_lock(void) { assert(!locked); locked = 1; }
void xk_os_audio_mutex_unlock(void) { assert(locked); locked = 0; }
uint64_t xk_os_monotonic_us(void) { return 1234567; }
int main(void)
{
    unsetenv("XV_VOLUME");
    g_xram = calloc(1, 0x5000); g_img_base = g_xram; g_xpt = calloc(1u << 20, 4);
    assert(g_xram && g_xpt); g_xpt[1] = 0; g_xpt[2] = 0x1000; g_xpt[3] = 0x3000;
    X_M16(0x1100) = 1; X_M16(0x1102) = 2; X_M32(0x1104) = 48000;
    X_M32(0x1108) = 192000; X_M16(0x110C) = 4; X_M16(0x110E) = 16;
    int16_t pcm[512];
    for (unsigned i = 0; i < 256; ++i) { pcm[i * 2] = 8000; pcm[i * 2 + 1] = -6000; }
    x_guest_write(0x2FE0, pcm, sizeof pcm);
    assert(xk_audio_init() == 0 && xk_audio_free_voices() == XA_MAX_VOICES && !locked);
    int voice = xk_audio_voice_new(1, 0x1100);
    assert(voice == 0 && xk_audio_free_voices() == XA_MAX_VOICES - 1);
    xk_audio_voice_set_data(voice, 0x2FE0, sizeof pcm); xk_audio_voice_play(voice, 1);
    int16_t out[XA_GRAIN * 2]; xk_audio_mix(out, XA_GRAIN);
    assert(out[0] == 0 && out[1] == 0); /* existing one-frame interpolation history */
    for (unsigned i = 1; i < XA_GRAIN; ++i) assert(out[i * 2] == 4000 && out[i * 2 + 1] == -3000);
    assert(xk_audio_last_mix_us() == 1234567 && xk_audio_free_voices() == XA_MAX_VOICES - 1);
    xk_audio_voice_stop(voice); xk_audio_mix(out, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN * 2; ++i) assert(out[i] == 0);
    xk_audio_voice_free(voice); assert(xk_audio_free_voices() == XA_MAX_VOICES);
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) assert(xk_audio_voice_new(1, 0x1100) == (int)i);
    assert(xk_audio_free_voices() == 0 && xk_audio_voice_new(1, 0x1100) == -1);
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) xk_audio_voice_free(i);
    assert(xk_audio_free_voices() == XA_MAX_VOICES && !locked);
    free(g_xpt); free(g_xram); puts("Halo 2 shared PCM mixer/capacity tests passed"); return 0;
}
