/* Actual concurrent output worker, actual DSP core, synthetic halt program. */
#define H2_AUDIO_DSP 1
#define main pcm_worker_tests
#include "audio_vita_test.c"
#undef main
h2_dsp_engine *h2_test_fx_engine(void);
void h2_test_fx_fault(h2_dsp_engine *);
static atomic_uint second_play_done;
static void *start_second(void *unused)
{
    (void)unused; assert(h2_audio_backend_fx_play(23) == 0);
    atomic_store(&second_play_done, 1); return NULL;
}
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
    assert(h2_audio_backend_fx_play(13) < 0); /* unconfigured default route */
    assert(h2_audio_backend_fx_route(13, 5) < 0 && h2_audio_backend_fx_route(13, 6) == 0);
    return s;
}
int main(void)
{
    pcm_worker_tests();
    h2_dsp_engine *s = open_fx();
    assert(h2_audio_backend_fx_forget(13) == 0 && !fx.engine);
    assert(h2_audio_backend_fx_bind(s, 13) == 0 && h2_audio_backend_fx_route(13, 6) == 0);
    atomic_store(&faults, 1u << F_FX_SLOW);
    assert(h2_audio_backend_fx_play(13) == 0);
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
    /* Hold a real pending sink grain, then let the worker prepare the next
     * first-source grain. Starting source23 cannot relabel either old grain
     * or claim either was its own successful submission. */
    for (;;) {
        sceKernelLockMutex(progress_mutex, 1, NULL);
        if (progress.pending) { atomic_store(&faults, 1u << F_HOLD); sceKernelUnlockMutex(progress_mutex, 1); break; }
        sceKernelUnlockMutex(progress_mutex, 1); usleep(1000);
    }
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_computed_frames > status.fx_submitted_frames) break; usleep(1000); }
    assert(h2_audio_backend_fx_bind(s, 23) == 0);
    pthread_t starter; assert(!pthread_create(&starter, NULL, start_second, NULL));
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_playing_mask == 3) break; usleep(1000); }
    assert(!status.fx_source_submitted[1] && !atomic_load(&second_play_done));
    atomic_store(&faults, 0); assert(!pthread_join(starter, NULL) && atomic_load(&second_play_done));
    h2_audio_backend_snapshot(&status);
    assert(status.fx_bound_mask == 3 && status.fx_playing_mask == 3 && status.fx_source_submitted[1] >= XA_GRAIN);
    assert(status.fx_source_submitted[0] == status.fx_submitted_frames);
    assert(status.last_peak_left == 3906 && status.last_peak_right == 3906);
    /* Third, spatial owner must also complete only its own tagged grain.
     * Synthetic fixture's real GP exposes input bins separately in core tests;
     * its monitor here still reports the two unchanged nonspatial sources. */
    int8_t taps[31] = {127};
    assert(h2_audio_backend_fx_bind_spatial(s, 23, taps) < 0); /* bins6..10 default headroom */
    for (unsigned i = 6; i <= 10; ++i) assert(h2_audio_backend_set_headroom(i, 0) == 0);
    assert(h2_audio_backend_fx_bind_spatial(s, 23, taps) == 0);
    assert(h2_audio_backend_set_headroom(10, 1) < 0);
    assert(h2_audio_backend_fx_play(H2_FX_SPATIAL23) == 0);
    h2_audio_backend_snapshot(&status);
    assert(status.fx_bound_mask == 7 && status.fx_playing_mask == 7 && status.fx_source_submitted[2] >= XA_GRAIN);
    assert(status.last_peak_left == 3906 && status.last_peak_right == 3906);
    assert(h2_audio_backend_fx_forget(H2_FX_SPATIAL23) < 0);
    for (unsigned bin = 24; bin <= 25; ++bin) {
        assert(h2_audio_backend_fx_bind(s, bin) == 0 && h2_audio_backend_fx_play(bin) == 0);
        assert(h2_audio_backend_fx_bind_spatial(s, bin, taps) == 0 && h2_audio_backend_fx_play(0x10000u | bin) == 0);
        h2_audio_backend_snapshot(&status); unsigned mask = (1u << (3 + (bin - 23) * 2)) - 1;
        assert(status.fx_bound_mask == mask && status.fx_playing_mask == mask);
        assert(status.last_peak_left == (bin == 24 ? 5859u : 7812u) && status.last_peak_right == status.last_peak_left);
        assert(status.fx_source_submitted[2 + (bin - 23) * 2] >= XA_GRAIN);
    }

    assert(h2_audio_backend_fx_forget(23) < 0);
    assert(h2_audio_backend_fx_forget(13) < 0 && h2_audio_backend_fx_route(13, 2) < 0 && h2_audio_backend_fx_play(13) < 0);
    assert(h2_audio_backend_close() == 0 && !resources && !atomic_load(&queued));
    atomic_store(&faults, 0);
    h2_audio_backend_snapshot(&status); assert(status.fx_consumed_frames == status.fx_submitted_frames);
    for (unsigned i = 0; i < 7; ++i) assert(status.fx_source_consumed[i] == status.fx_source_submitted[i]);
    h2_dsp_destroy(s);
/* GCC's TSan runtime fails its own longjmp-buffer check on this injected DSP
 * fault. The TSan concurrency run skips only this fixture; normal and
 * ASan/UBSan runs retain it, and production fault handling is unchanged. */
#ifndef H2_FX_TSAN_SKIP_LONGJMP
    /* A GP fault before completing a grain submits none of its partial data. */
    s = open_fx(); h2_test_fx_fault(s);
    assert(h2_audio_backend_fx_play(13) < 0);
    h2_audio_backend_snapshot(&status); assert(status.error == (uint32_t)-1004 && !status.fx_submitted_frames);
    assert(h2_audio_backend_close() == 0 && !resources); h2_dsp_destroy(s);
#endif
    /* Actual sink failures remain terminal and cannot advance FX consumption. */
    s = open_fx(); atomic_store(&faults, 1u << F_WRITE);
    assert(h2_audio_backend_fx_play(13) < 0);
    h2_audio_backend_snapshot(&status); assert(status.error && !status.fx_submitted_frames && !status.fx_consumed_frames);
    atomic_store(&faults, 0); assert(h2_audio_backend_close() == 0 && !resources); h2_dsp_destroy(s);
    puts("Halo 2 FX worker: actual DSP output, retained grains, real sink progress, ownership and fault rollback pass");
    return 0;
}
