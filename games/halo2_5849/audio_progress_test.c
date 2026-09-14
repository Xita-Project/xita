#include "audio_progress.h"
#include "recomp/kernel/xk_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    h2_audio_progress p, before;
    uint32_t play = 0xA5A5, write = 0x5A5A;
    h2_audio_progress_reset(&p);
    assert(!h2_audio_progress_cursor(&p, 0, &play, &write) && play == 0xA5A5 && write == 0x5A5A);
    assert(h2_audio_progress_submit(&p, XA_GRAIN, 0));
    before = p; assert(!h2_audio_progress_submit(&p, XA_GRAIN, 0) && !memcmp(&p, &before, sizeof p));
    /* Play during an earlier silent grain must not credit that grain to the
     * new voice, even when its rest count reaches zero. */
    assert(h2_audio_progress_play(&p, 0, 106496, 44100));
    assert(h2_audio_progress_rest(&p, 512));
    assert(h2_audio_progress_cursor(&p, 0, &play, &write) && !play && !write);
    assert(h2_audio_progress_rest(&p, 0) && !p.completed_frames);
    before = p; assert(!h2_audio_progress_play(&p, 1, 1024, 48000) && !memcmp(&p, &before, sizeof p));
    uint64_t consumed = 0;
    for (unsigned grain = 0; grain < 160; ++grain) {
        uint32_t frontier = (grain * 4096u) % 106496;
        assert(h2_audio_progress_submit(&p, XA_GRAIN, frontier));
        for (unsigned remaining = XA_GRAIN;; --remaining) {
            assert(h2_audio_progress_rest(&p, remaining));
            assert(h2_audio_progress_cursor(&p, 0, &play, &write));
            uint64_t n = consumed + XA_GRAIN - remaining;
            uint32_t expected = (uint32_t)((n * ((44100ull << 16) / 48000) >> 16) % (106496/4)) * 4;
            assert(play == expected && write == frontier);
            if (!remaining) break;
            before = p; assert(!h2_audio_progress_rest(&p, remaining + 1) && !memcmp(&p, &before, sizeof p));
        }
        consumed += XA_GRAIN; assert(p.completed_frames == consumed && !p.pending);
    }
    /* Stopping cannot discard queued active samples. A completed stop holds
     * its cursor through quiet output; rewind/restart and forget preserve an
     * already queued silent grain without crediting it to a new voice. */
    assert(h2_audio_progress_submit(&p, XA_GRAIN, 4096));
    before = p; assert(!h2_audio_progress_stop(&p, 0) && !memcmp(&p, &before, sizeof p));
    assert(!h2_audio_progress_rewind(&p, 0) && !h2_audio_progress_forget(&p, 0));
    assert(h2_audio_progress_rest(&p, 0));
    assert(h2_audio_progress_stop(&p, 0));
    assert(h2_audio_progress_cursor(&p, 0, &play, &write) && play == write);
    uint32_t stop_position = play;
    assert(h2_audio_progress_submit(&p, XA_GRAIN, 0));
    assert(h2_audio_progress_rest(&p, 256));
    assert(h2_audio_progress_cursor(&p, 0, &play, &write) && play == stop_position && write == play);
    assert(!h2_audio_progress_play(&p, 0, 106496, 44100));
    assert(h2_audio_progress_rewind(&p, 0));
    assert(h2_audio_progress_cursor(&p, 0, &play, &write) && !play && !write);
    assert(h2_audio_progress_play(&p, 0, 106496, 44100));
    assert(h2_audio_progress_rest(&p, 0) && !p.completed_frames);
    assert(h2_audio_progress_stop(&p, 0));
    assert(h2_audio_progress_submit(&p, XA_GRAIN, 0));
    assert(h2_audio_progress_forget(&p, 0) && p.pending && p.remaining == XA_GRAIN && p.voice == -1);
    assert(h2_audio_progress_play(&p, 0, 106496, 44100));
    assert(h2_audio_progress_rest(&p, 0) && !p.completed_frames);
    /* Counter and invalid-input rejection precede any output/state mutation. */
    assert(h2_audio_progress_submit(&p, XA_GRAIN, p.bytes));
    assert(h2_audio_progress_cursor(&p, 0, &play, &write) && !write);
    p.completed_frames = UINT64_MAX - XA_GRAIN + 1; before = p;
    assert(!h2_audio_progress_rest(&p, 0) && !memcmp(&p, &before, sizeof p));
    play = write = 123; assert(!h2_audio_progress_cursor(&p, 0, &play, &write) && play == 123 && write == 123);
    for (unsigned test = 0; test < 6; ++test) {
        h2_audio_progress_reset(&p); before = p;
        assert(!h2_audio_progress_play(&p, test == 0 ? -1 : test == 1 ? XA_MAX_VOICES : 0,
                                      test == 2 ? 0 : test == 3 ? 3 : 1024,
                                      test == 4 ? 0 : test == 5 ? 48001 : 44100));
        assert(!memcmp(&p, &before, sizeof p));
    }
    puts("Halo 2 sink progress: single grain, pre-Play silence, monotonic rest, source step, wraps and rejection passed");
    return 0;
}
