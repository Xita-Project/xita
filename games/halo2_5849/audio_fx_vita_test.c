/* Actual concurrent output worker, actual DSP core, synthetic halt program. */
#define H2_AUDIO_DSP 1
#define main pcm_worker_tests
#include "audio_vita_test.c"
#undef main
h2_dsp_engine *h2_test_fx_engine(void);
void h2_test_fx_fault(h2_dsp_engine *);
static atomic_uint second_play_done;
static atomic_uint effect_writer_done;
static void *write_effect_pairs(void *arg)
{
    for(unsigned i=1;i<=2000;i++)assert(h2_audio_backend_effect_write_pair(arg,0,64,i,i^0xffffffu));
    atomic_store(&effect_writer_done,1);return NULL;
}
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
    uint32_t reverb_parameters[66]={0};
    /* Concurrent worker ownership is checked before dereferencing foreign
     * engines. This fixture has no effect9, so it cannot publish a fake ack. */
    assert(!h2_audio_backend_queue_reverb9((h2_dsp_engine*)(uintptr_t)1,7,reverb_parameters));
    assert(!h2_audio_backend_queue_reverb9(NULL,7,reverb_parameters));
    assert(!h2_audio_backend_queue_reverb9(s,7,reverb_parameters));
    assert(!reverb_pending);
    for (unsigned i = 0; i < 20; ++i) {
        uint32_t state[32]; memset(state, 0x55, sizeof state);
        assert(h2_audio_backend_effect_read(s, 0, 0, state, sizeof state));
        for (unsigned j = 0; j < 32; ++j) assert(state[j] == 0x765432);
        assert(!h2_audio_backend_effect_read(s, 0, 1, state, sizeof state));
        for (unsigned j = 0; j < 32; ++j) assert(state[j] == 0x765432);
    }
    /* Real worker runs concurrently. Each serialized query sees a complete
     * pair, while rejected owners/words cannot alter the live DSP state. */
    assert(h2_audio_backend_effect_write_pair(s,0,64,0,0xffffff));
    assert(!h2_audio_backend_effect_write_pair((h2_dsp_engine *)(uintptr_t)1,0,64,1,2));
    assert(!h2_audio_backend_effect_write_pair(s,0,64,1,0x1000000));
    pthread_t writer;assert(!pthread_create(&writer,NULL,write_effect_pairs,s));
    do {
        uint32_t pair[2];assert(h2_audio_backend_effect_read(s,0,64,pair,8));
        assert(pair[0]<=2000 && (pair[0]^pair[1])==0xffffff);
    } while(!atomic_load(&effect_writer_done));
    assert(!pthread_join(writer,NULL));
    uint32_t last_pair[2];assert(h2_audio_backend_effect_read(s,0,64,last_pair,8));
    assert(last_pair[0]==2000 && last_pair[1]==(2000^0xffffffu));
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

    /* Preserve a previously computed old-route grain, then change the real
     * next-frame mix under the worker lock. Neither old grain is discarded. */
    atomic_store(&faults, 1u << F_HOLD);
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_computed_frames > status.fx_submitted_frames) break; usleep(1000); }
    uint64_t old_computed = status.fx_computed_frames;
    assert(h2_audio_backend_fx_route_mask(24, 64) < 0);
    assert(h2_audio_backend_fx_route_mask(23, 64) == 0);
    h2_audio_backend_snapshot(&status); assert(status.last_peak_left == 7812 && status.fx_computed_frames == old_computed);
    atomic_store(&faults, 0);
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_submitted_frames > old_computed) break; usleep(1000); }
    assert(status.last_peak_left == 5859 && status.last_peak_right == 5859 && !status.error);
    assert(h2_audio_backend_fx_mute(H2_FX_SPATIAL23) == 0);
    h2_audio_backend_snapshot(&status); uint64_t muted_submitted = status.fx_source_submitted[2];
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_source_submitted[2] > muted_submitted) break; usleep(1000); }
    assert(status.fx_playing_mask == 127 && !status.error && status.last_peak_left == 5859);
    assert(h2_audio_backend_fx_route_mask(24,128) == 0 && h2_audio_backend_fx_mute(H2_FX_SPATIAL24) == 0);
    assert(h2_audio_backend_fx_route_mask(H2_FX_SPATIAL25,1024) == 0 && h2_audio_backend_fx_mute(25) == 0);
    h2_audio_backend_snapshot(&status); uint64_t final_computed = status.fx_computed_frames;
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_submitted_frames > final_computed) break; usleep(1000); }
    assert(status.last_peak_left == 1953 && status.last_peak_right == 1953 && status.fx_playing_mask == 127 && !status.error);



    atomic_store(&faults, 1u << F_HOLD);
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_computed_frames > status.fx_submitted_frames) break; usleep(1000); }
    uint64_t filter_computed = status.fx_computed_frames;
    assert(h2_audio_backend_fx_filter(24) < 0 && h2_audio_backend_fx_filter(25) < 0);
    assert(h2_audio_backend_fx_filter(23) == 0 && h2_audio_backend_fx_filter(24) == 0);
    h2_audio_backend_snapshot(&status); assert(status.fx_computed_frames == filter_computed && status.last_peak_left == 1953);
    atomic_store(&faults, 0);
    for (;;) { h2_audio_backend_snapshot(&status); if (status.fx_submitted_frames > filter_computed) break; usleep(1000); }
    assert(status.last_peak_left == 1953 && !status.error && status.fx_playing_mask == 127);
    assert(h2_audio_backend_fx_filter(23) == 0 && h2_audio_backend_fx_filter(24) == 0);
    for (unsigned bin = 15; bin <= 22; ++bin) {
        assert(h2_audio_backend_fx_bind(s,bin) == 0);
        assert(h2_audio_backend_fx_play(bin) < 0); /* default route cannot be used */
        assert(h2_audio_backend_fx_route_mask(bin,1u << (6 + (bin - 15) % 4)) == 0);
        assert(h2_audio_backend_fx_play(bin) == 0);
        h2_audio_backend_snapshot(&status);
        assert(status.fx_source_submitted[7 + bin - 15] >= XA_GRAIN && !status.error);
        assert(status.last_peak_left == 1953 && status.last_peak_right == 1953);
    }
    assert(status.fx_playing_mask == 0x7fff && status.fx_bound_mask == 0x7fff);
    atomic_store(&faults,1u<<F_HOLD);
    for(;;){h2_audio_backend_snapshot(&status);if(status.fx_computed_frames>status.fx_submitted_frames)break;usleep(1000);}
    uint64_t mute_computed=status.fx_computed_frames,prior_sources[H2_FX_SOURCES];
    memcpy(prior_sources,status.fx_source_consumed,sizeof prior_sources);
    for(unsigned bin=15;bin<=22;++bin)assert(h2_audio_backend_fx_mute(bin)==0);
    h2_audio_backend_snapshot(&status);assert(status.fx_computed_frames==mute_computed&&!status.error);
    atomic_store(&faults,0);
    for(;;){h2_audio_backend_snapshot(&status);if(status.fx_consumed_frames>mute_computed)break;usleep(1000);}
    for(unsigned v=0;v<H2_FX_SOURCES;++v)assert(status.fx_source_consumed[v]>prior_sources[v]);
    assert(status.fx_playing_mask==0x7fff&&status.last_peak_left==1953&&!status.error);
    assert(h2_audio_backend_fx_forget(23) < 0);
    assert(h2_audio_backend_fx_forget(13) < 0 && h2_audio_backend_fx_route(13, 2) < 0 && h2_audio_backend_fx_play(13) < 0);
    assert(h2_audio_backend_close() == 0 && !resources && !atomic_load(&queued));
    atomic_store(&faults, 0);
    h2_audio_backend_snapshot(&status); assert(status.fx_consumed_frames == status.fx_submitted_frames);
    for (unsigned i = 0; i < H2_FX_SOURCES; ++i) assert(status.fx_source_consumed[i] == status.fx_source_submitted[i]);
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
