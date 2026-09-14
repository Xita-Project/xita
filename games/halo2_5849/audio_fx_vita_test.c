/* Actual concurrent output worker, actual DSP core, synthetic halt program. */
#define H2_AUDIO_DSP 1
#define main pcm_worker_tests
#include "audio_vita_test.c"
#undef main
h2_dsp_engine *h2_test_fx_engine(void);
void h2_test_fx_fault(h2_dsp_engine *);
static h2_dsp_engine *open_fx(void)
{
    assert(h2_audio_backend_open() == 0);
    h2_dsp_engine *s = h2_test_fx_engine();
    assert(h2_audio_backend_fx_bind(s, 13) < 0); /* original headroom is one */
    for (unsigned i = 0; i < 6; ++i) assert(h2_audio_backend_set_headroom(i, 0) == 0);
    assert(h2_audio_backend_fx_bind(s, 12) < 0);
    assert(h2_audio_backend_fx_bind(s, 13) == 0);
    assert(h2_audio_backend_set_headroom(1, 1) < 0);
    assert(h2_audio_backend_play(0, 106496, 44100) < 0);
    assert(h2_audio_backend_fx_play() < 0); /* unconfigured default route */
    assert(h2_audio_backend_fx_route(5) < 0 && h2_audio_backend_fx_route(6) == 0);
    return s;
}
int main(void)
{
    pcm_worker_tests();
    h2_dsp_engine *s = open_fx();
    assert(h2_audio_backend_fx_forget() == 0 && !fx.engine);
    assert(h2_audio_backend_fx_bind(s, 13) == 0 && h2_audio_backend_fx_route(6) == 0);
    atomic_store(&faults, 1u << F_FX_SLOW);
    assert(h2_audio_backend_fx_play() == 0);
    h2_audio_backend_status status;
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_submitted_frames >= 3 * XA_GRAIN) break; usleep(1000); }
    assert(!status.error && status.fx_computed_frames >= status.fx_submitted_frames);
    assert(status.fx_submitted_frames >= status.fx_consumed_frames && status.fx_consumed_frames >= XA_GRAIN);
    assert(status.fx_compute_us && status.fx_max_compute_us && status.last_peak_left == 1953 && status.last_peak_right == 1953);
    assert(status.fx_deadline_misses >= 3 && status.fx_empty_after_compute >= 2);
    for (unsigned i = 0; i < 20; ++i) {
        uint32_t state[32]; memset(state, 0x55, sizeof state);
        assert(h2_audio_backend_effect_read(s, 0, 0, state, sizeof state));
        for (unsigned j = 0; j < 32; ++j) assert(state[j] == 0x765432);
        assert(!h2_audio_backend_effect_read(s, 0, 1, state, sizeof state));
        for (unsigned j = 0; j < 32; ++j) assert(state[j] == 0x765432);
    }
    assert(h2_audio_backend_fx_forget() < 0 && h2_audio_backend_fx_route(2) < 0 && h2_audio_backend_fx_play() < 0);
    assert(h2_audio_backend_close() == 0 && !resources && !atomic_load(&queued));
    atomic_store(&faults, 0);
    h2_audio_backend_snapshot(&status); assert(status.fx_consumed_frames == status.fx_submitted_frames);
    h2_dsp_destroy(s);
    /* A GP fault before completing a grain submits none of its partial data. */
    s = open_fx(); h2_test_fx_fault(s);
    assert(h2_audio_backend_fx_play() < 0);
    h2_audio_backend_snapshot(&status); assert(status.error == (uint32_t)-1004 && !status.fx_submitted_frames);
    assert(h2_audio_backend_close() == 0 && !resources); h2_dsp_destroy(s);
    /* Actual sink failures remain terminal and cannot advance FX consumption. */
    s = open_fx(); atomic_store(&faults, 1u << F_WRITE);
    assert(h2_audio_backend_fx_play() < 0);
    h2_audio_backend_snapshot(&status); assert(status.error && !status.fx_submitted_frames && !status.fx_consumed_frames);
    atomic_store(&faults, 0); assert(h2_audio_backend_close() == 0 && !resources); h2_dsp_destroy(s);
    puts("Halo 2 FX worker: actual DSP output, retained grains, real sink progress, ownership and fault rollback pass");
    return 0;
}
