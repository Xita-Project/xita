#include "audio_progress.h"
#include "recomp/kernel/xk_audio.h"
#include <limits.h>

void h2_audio_progress_reset(h2_audio_progress *p)
{ *p = (h2_audio_progress){.voice = -1, .queued_voice = -1}; }
int h2_audio_progress_play(h2_audio_progress *p, int voice, uint32_t bytes, uint32_t rate)
{
    if (p->voice >= 0 || voice < 0 || voice >= XA_MAX_VOICES || !bytes || (bytes & 3) ||
        !rate || rate > XA_OUT_RATE) return 0;
    p->voice = voice; p->bytes = bytes;
    p->step = (uint32_t)(((uint64_t)rate << 16) / XA_OUT_RATE);
    p->completed_frames = 0; p->write_position = 0;
    return 1;
}
int h2_audio_progress_submit(h2_audio_progress *p, uint32_t frames, uint32_t decoded_position)
{
    if (p->pending || !frames || frames > XA_GRAIN ||
        (p->voice >= 0 && ((decoded_position & 3) || decoded_position > p->bytes))) return 0;
    p->pending = 1; p->remaining = frames; p->queued_voice = p->voice;
    p->queued_frames = p->voice >= 0 ? frames : 0;
    if (p->voice >= 0) p->write_position = decoded_position % p->bytes;
    return 1;
}
int h2_audio_progress_rest(h2_audio_progress *p, uint32_t remaining)
{
    if (!p->pending || remaining > p->remaining ||
        (!remaining && p->completed_frames > UINT64_MAX - p->queued_frames)) return 0;
    p->remaining = remaining;
    if (!remaining) { p->completed_frames += p->queued_frames; p->pending = 0; }
    return 1;
}
int h2_audio_progress_cursor(const h2_audio_progress *p, int voice, uint32_t *play, uint32_t *write)
{
    if (voice < 0 || p->voice != voice || !p->step || !p->bytes) return 0;
    uint64_t frames = p->completed_frames;
    uint32_t in_flight = p->pending && p->queued_voice == voice ? p->queued_frames - p->remaining : 0;
    if (frames > UINT64_MAX - in_flight) return 0;
    frames += in_flight;
    if (frames > UINT64_MAX / p->step) return 0;
    /* Same 16.16 source step as the real mixer; whole stereo PCM frames.
     * The decoder frontier is independently supplied by that actual mixer. */
    *play = (uint32_t)(((frames * p->step) >> 16) % (p->bytes / 4)) * 4;
    *write = p->write_position;
    return 1;
}
