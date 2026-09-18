#include "audio_bins.h"
#include "recomp/kernel/xk_audio.h"
#include <string.h>

static uint8_t headroom[32];

void h2_audio_bins_reset(void)
{
    xk_audio_lock();
    /* Owned XDK5849 listener constructor 37CC6A: 31 ones, then one zero. */
    memset(headroom, 1, 31); headroom[31] = 0;
    xk_audio_unlock();
}
int h2_audio_bins_set(uint32_t bin, uint32_t amount)
{
    if (bin >= 32) return -1;
    xk_audio_lock();
    /* Original listener retains the low byte; hardware consumes low 3 bits. */
    headroom[bin] = (uint8_t)amount;
    xk_audio_unlock();
    return 0;
}
void h2_audio_bins_snapshot(uint8_t out[32])
{ xk_audio_lock(); memcpy(out, headroom, 32); xk_audio_unlock(); }

void h2_audio_bins_filter(int32_t *stereo, int frames)
{
    /* The current software mixer accumulates only the fixed FL/FR route.
     * Bin gain is linear, so apply it to these unsaturated sums, never to
     * already-clipped output. Retain the mixer's floor quantization for signed
     * integer PCM; this is not an emulation of the APU's full DSP precision. */
    for (unsigned channel = 0; channel < 2; ++channel) {
        int64_t divisor = 1u << (headroom[channel] & 7);
        for (int frame = 0; frame < frames; ++frame) {
            int64_t value = stereo[frame * 2 + channel];
            stereo[frame * 2 + channel] = (int32_t)(value >= 0 ? value / divisor
                : -((-value + divisor - 1) / divisor));
        }
    }
}
