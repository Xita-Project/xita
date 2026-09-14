#pragma once
#include <stdint.h>

/* Single active looping PCM voice, one outstanding sink grain. The owner
 * serializes Play/mix/submit/rest observations; no wall-clock extrapolation. */
typedef struct {
    int voice, queued_voice;
    uint32_t bytes, step, pending, remaining, queued_frames, write_position;
    uint32_t stopped, rewound;
    uint64_t completed_frames;
} h2_audio_progress;

void h2_audio_progress_reset(h2_audio_progress *p);
int h2_audio_progress_play(h2_audio_progress *p, int voice, uint32_t bytes, uint32_t rate);
int h2_audio_progress_submit(h2_audio_progress *p, uint32_t frames, uint32_t decoded_position);
int h2_audio_progress_rest(h2_audio_progress *p, uint32_t remaining);
int h2_audio_progress_cursor(const h2_audio_progress *p, int voice, uint32_t *play, uint32_t *write);

/* Stop finishes only once its real active grain has drained. Rewind is bounded
 * to zero on a stopped voice; silent pending grains retain their ownership. */
int h2_audio_progress_stop(h2_audio_progress *p, int voice);
int h2_audio_progress_rewind(h2_audio_progress *p, int voice);
int h2_audio_progress_forget(h2_audio_progress *p, int voice);
