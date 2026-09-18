#pragma once
#include <stdint.h>

/* Only the fixed mono/stereo front-left/front-right route exists in this
 * mixer. Values for other bins are retained; their routing is unsupported. */
void h2_audio_bins_reset(void);
int h2_audio_bins_set(uint32_t bin, uint32_t headroom);
void h2_audio_bins_snapshot(uint8_t out[32]);
/* Shared mixer calls this with its mutex held, before int16 saturation. */
void h2_audio_bins_filter(int32_t *stereo, int frames);
