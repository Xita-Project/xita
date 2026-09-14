/* Actual shared mixer with the opt-in stereo-bin stage. No game bytes. */
#include "audio_bins.h"
#include "recomp/xv_x86rt.h"
#include "recomp/kernel/xk_audio.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
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
uint64_t xk_os_monotonic_us(void) { return 42; }
static int16_t reference(int32_t sum, unsigned shift)
{
    double scaled = floor(ldexp((double)sum, -(int)shift));
    return scaled < -32768 ? -32768 : scaled > 32767 ? 32767 : (int16_t)scaled;
}
int main(void)
{
    unsetenv("XV_VOLUME");
    g_xram = calloc(1, 0x5000); g_img_base = g_xram; g_xpt = calloc(1u << 20, 4);
    assert(g_xram && g_xpt); g_xpt[1] = 0; g_xpt[2] = 0x1000; g_xpt[3] = 0x3000;
    X_M16(0x1100) = 1; X_M16(0x1102) = 2; X_M32(0x1104) = 48000;
    X_M32(0x1108) = 192000; X_M16(0x110C) = 4; X_M16(0x110E) = 16;
    int16_t pcm[512];
    for (unsigned i = 0; i < 256; ++i) { pcm[i * 2] = 8001; pcm[i * 2 + 1] = -6001; }
    x_guest_write(0x2FE0, pcm, sizeof pcm);
    assert(xk_audio_init() == 0); h2_audio_bins_reset();
    uint8_t bins[32], before[32]; h2_audio_bins_snapshot(bins);
    for (unsigned b = 0; b < 32; ++b) assert(bins[b] == (b != 31));
    for (unsigned b = 0; b < 32; ++b) assert(h2_audio_bins_set(b, 0xABCDEF00u + b) == 0);
    h2_audio_bins_snapshot(bins);
    for (unsigned b = 0; b < 32; ++b) assert(bins[b] == b);
    memcpy(before, bins, 32);
    assert(h2_audio_bins_set(32, 0) == -1 && h2_audio_bins_set(UINT32_MAX, 0) == -1);
    h2_audio_bins_snapshot(bins); assert(!memcmp(bins, before, 32) && !locked);

    /* Twelve voices exceed both int16 limits before the bin gain. Applying
     * gain after clipping would yield different, incorrect sample values. */
    for (unsigned v = 0; v < 12; ++v) {
        assert(xk_audio_voice_new(1, 0x1100) == (int)v);
        xk_audio_voice_set_data(v, 0x2FE0, sizeof pcm); xk_audio_voice_play(v, 1);
    }
    int16_t out[XA_GRAIN * 2]; xk_audio_mix(out, XA_GRAIN); /* prime interpolation */
    for (unsigned left = 0; left < 8; ++left) for (unsigned right = 0; right < 8; ++right) {
        assert(h2_audio_bins_set(0, 0xF8u + left) == 0);
        assert(h2_audio_bins_set(1, 0xFFFFFFF8u + right) == 0);
        xk_audio_mix(out, XA_GRAIN);
        for (unsigned f = 0; f < XA_GRAIN; ++f) {
            assert(out[f * 2] == reference(12 * 4000, left));
            assert(out[f * 2 + 1] == reference(12 * -3001, right));
        }
    }
    /* Changing an unconnected effect/surround bin must not reroute a voice. */
    assert(h2_audio_bins_set(0, 1) == 0 && h2_audio_bins_set(1, 2) == 0);
    for (unsigned b = 2; b < 32; ++b) {
        assert(h2_audio_bins_set(b, 0) == 0); xk_audio_mix(out, 1);
        assert(out[0] == 24000 && out[1] == -9003);
    }
    for (unsigned v = 0; v < 12; ++v) xk_audio_voice_free(v);
    xk_audio_mix(out, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN * 2; ++i) assert(!out[i]);
    /* Integer gain stage handles the complete accumulator domain portably. */
    const int32_t edge[] = {INT_MIN, INT_MAX, -129, -1, 0, 1, 127, 128};
    for (unsigned s = 0; s < 8; ++s) {
        assert(!h2_audio_bins_set(0, s) && !h2_audio_bins_set(1, s));
        for (unsigned i = 0; i < sizeof edge / sizeof *edge; ++i) {
            int32_t sum[2] = {edge[i], edge[i]}; xk_audio_lock(); h2_audio_bins_filter(sum, 1); xk_audio_unlock();
            assert(sum[0] == (int32_t)floor(ldexp((double)edge[i], -(int)s)) && sum[0] == sum[1]);
        }
    }
    h2_audio_bins_reset(); h2_audio_bins_snapshot(bins);
    for (unsigned b = 0; b < 32; ++b) assert(bins[b] == (b != 31));
    free(g_xpt); free(g_xram);
    puts("Halo 2 mix-bin defaults, byte/mask ABI, pre-clipping gain and stereo routing passed");
    return 0;
}
