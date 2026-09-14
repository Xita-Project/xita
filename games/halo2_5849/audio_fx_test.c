/* Synthetic DSP program only; exercises the actual GP and FX consumer. */
#define main engine_test_main
#include "dsp_engine_test.c"
#undef main
#include "audio_fx.c"

static h2_dsp_engine *fx_fixture(void)
{
    h2_dsp_engine *s = fixture();
    s->core.registers[DSP_REG_X0] = 1;
    s->core.pram[0] = (8u << 16) | (1u << 15) | (1u << 14) | (DSP_REG_X0 << 8) | 4;
    s->core.pram[1] = 12u << 16; /* next frame jumps back to the checked halt */
    assert(h2_dsp_zero_frame(s)); return s;
}
#ifndef H2_AUDIO_FX_TEST_MAIN
#define H2_AUDIO_FX_TEST_MAIN main
#endif
int H2_AUDIO_FX_TEST_MAIN(void)
{
    h2_dsp_engine *s = fx_fixture(); h2_audio_fx fx = {0}, before;
    int16_t out[2048]; memset(out, 0x55, sizeof out);
    assert(!h2_audio_fx_bind(NULL, s, 13));
    assert(!h2_audio_fx_bind(&fx, s, 12) && !fx.engine);
    assert(!h2_audio_fx_bind(&fx, NULL, 13) && !fx.engine);
    assert(h2_audio_fx_bind(&fx, s, 13) && fx.sources[0].routes == 2 && !fx.playing);
    before = fx; assert(!h2_audio_fx_play(&fx, 13) && !memcmp(&before, &fx, sizeof fx));
    assert(!h2_audio_fx_route(&fx, 13, 5) && !memcmp(&before, &fx, sizeof fx));
    assert(h2_audio_fx_forget(&fx, 13) && !fx.engine);
    assert(h2_audio_fx_bind(&fx, s, 13));
    assert(h2_audio_fx_route(&fx, 13, 6) && h2_audio_fx_play(&fx, 13));
    before = fx;
    assert(!h2_audio_fx_play(&fx, 13) && !h2_audio_fx_forget(&fx, 13) && !h2_audio_fx_route(&fx, 13, 2));
    for (unsigned n = 0; n < 1056; ++n) if (!n || n > 1024 || (n & 31)) {
        assert(!h2_audio_fx_render(&fx, out, n));
        assert(!memcmp(&before, &fx, sizeof fx));
        for (unsigned i = 0; i < 2048; ++i) assert(out[i] == 0x5555);
    }
    const int32_t samples[] = {0, 1, -1, 255, -255, 256, -256, 8388607, -8388608, 123456, -123456};
    for (unsigned i = 0; i < 32; ++i) put32(s->scratch + 0xb100 + i * 4, (uint32_t)samples[i % 11]);
    assert(h2_audio_fx_render(&fx, out, 1024) && fx.frames == 32);
    for (unsigned i = 0; i < 1024; ++i) {
        int32_t sample = samples[(i % 32) % 11];
        int expected = sample >= 0 ? sample / 256 : -((-sample + 255) / 256);
        assert(out[i * 2] == expected && out[i * 2 + 1] == expected);
    }
    for (unsigned b = 0; b < 32; ++b) for (unsigned i = 0; i < 32; ++i)
        assert(s->core.mixbuffer[b * 32 + i] == (b < 6 ? (uint32_t)samples[i % 11] & 0xffffff : 0));
    /* Add an independent source while the first is active. Inactive release
     * and rebind cannot alter the first source's state or DSP frame count. */
    h2_dsp_engine *other = fx_fixture(); before = fx;
    assert(!h2_audio_fx_bind(&fx, other, 23) && !memcmp(&before, &fx, sizeof fx));
    h2_dsp_destroy(other);
    assert(h2_audio_fx_bind(&fx, s, 23) && fx.bound == 3 && fx.playing == 1);
    assert(h2_audio_fx_forget(&fx, 23) && !memcmp(&before, &fx, sizeof fx));
    assert(h2_audio_fx_bind(&fx, s, 23));
    assert(!h2_audio_fx_route(&fx, 23, 6) && h2_audio_fx_play(&fx, 23));
    const int32_t pairs[][2] = {{8388607,1},{-8388608,-1},{8388607,8388607},{-8388608,-8388608},
        {8388607,-8388608},{-1,1},{255,1},{-255,-1},{123456,-654321},{8388500,107},{-8388500,-108}};
    for (unsigned i = 0; i < 32; ++i) {
        put32(s->scratch + 0xb100 + i * 4, (uint32_t)pairs[i % 11][0]);
        put32(s->scratch + 0xb600 + i * 4, (uint32_t)pairs[i % 11][1]);
    }
    uint64_t engine_frames = s->status.frames;
    assert(h2_audio_fx_render(&fx, out, 32));
    assert(fx.frames == 33 && s->status.frames == engine_frames + 1);
    assert(fx.sources[0].frames == 33 && fx.sources[1].frames == 1);
    for (unsigned i = 0; i < 32; ++i) {
        int64_t sum = (int64_t)pairs[i % 11][0] + pairs[i % 11][1];
        if (sum > 8388607) sum = 8388607; if (sum < -8388608) sum = -8388608;
        int expected = sum >= 0 ? sum / 256 : -((-sum + 255) / 256);
        assert(out[i * 2] == expected && out[i * 2 + 1] == expected);
        for (unsigned b = 0; b < 32; ++b)
            assert(s->core.mixbuffer[b * 32 + i] == ((uint32_t)(b < 2 ? sum : b < 6 ? pairs[i % 11][0] : 0) & 0xffffff));
    }
    before = fx; assert(!h2_audio_fx_forget(&fx, 23) && !h2_audio_fx_bind(&fx, s, 23));
    assert(!h2_audio_fx_bind(&fx, s, 24) && !memcmp(&before, &fx, sizeof fx));
    /* Fixed spatial voice is a distinct owner of the same prior FX23 frame.
     * Its filtered routes must not replace or multiply the two 2D routes. */
    int8_t taps[31] = {127};
    assert(!h2_audio_fx_bind(&fx, s, H2_FX_SPATIAL23));
    assert(h2_audio_fx_bind_spatial(&fx, s, 23, taps) && fx.bound == 7 && fx.playing == 3);
    assert(h2_audio_fx_forget(&fx, H2_FX_SPATIAL23) && !memcmp(&before, &fx, sizeof fx));
    assert(h2_audio_fx_bind_spatial(&fx, s, 23, taps));
    assert(!h2_audio_fx_route(&fx, H2_FX_SPATIAL23, 5));
    assert(h2_audio_fx_play(&fx, H2_FX_SPATIAL23) && fx.playing == 7);
    for (unsigned i = 0; i < 32; ++i) {
        put32(s->scratch + 0xb100 + i * 4, 0);
        put32(s->scratch + 0xb600 + i * 4, 0x400000);
    }
    engine_frames = s->status.frames; assert(h2_audio_fx_render(&fx, out, 32));
    assert(s->status.frames == engine_frames + 1 && fx.sources[2].frames == 1);
    float gain = 0;
    for (unsigned i = 0; i < 32; ++i) {
        gain += 0.01f * (1.0f - gain); uint32_t expected = (uint32_t)lrint((double)(gain * 0.5f) * 8388608.0);
        assert(out[i * 2] == 16384 && out[i * 2 + 1] == 16384);
        for (unsigned bin = 0; bin < 32; ++bin) {
            uint32_t wanted = bin < 2 ? 0x400000 : bin == 6 || bin == 7 || bin == 10 ? expected : 0;
            assert(s->core.mixbuffer[bin * 32 + i] == wanted);
        }
    }
    assert(!h2_audio_fx_forget(&fx, H2_FX_SPATIAL23));
    /* Continue the exact creation order for the remaining original pairs.
     * Each new filter starts independently; one inactive owner can be removed
     * without changing any existing history or source progress. */
    before = fx; assert(!h2_audio_fx_bind(&fx, s, 25) && !memcmp(&before, &fx, sizeof fx));
    float curves[3] = {gain,0,0};
    for (unsigned bin = 24; bin <= 25; ++bin) {
        assert(h2_audio_fx_bind(&fx, s, bin) && h2_audio_fx_play(&fx, bin));
        before = fx;
        assert(h2_audio_fx_bind_spatial(&fx, s, bin, taps));
        assert(h2_audio_fx_forget(&fx, 0x10000u | bin) && !memcmp(&before, &fx, sizeof fx));
        assert(h2_audio_fx_bind_spatial(&fx, s, bin, taps));
        assert(h2_audio_fx_play(&fx, 0x10000u | bin));
    }
    assert(fx.bound == 127 && fx.playing == 127);
    unsigned saturated = 0;
    for (unsigned frame = 0; frame < 2; ++frame) {
        int32_t sample = frame ? -8388608 : 8388607;
        for (unsigned bin = 13; bin <= 25; ++bin) if (bin == 13 || bin >= 23)
            for (unsigned i = 0; i < 32; ++i) put32(s->scratch + 0xb000 + (bin - 11) * 128 + i * 4, (uint32_t)sample);
        assert(h2_audio_fx_render(&fx, out, 32));
        for (unsigned i = 0; i < 32; ++i) {
            float total = 0;
            for (unsigned voice = 3; voice-- > 0;) {
                curves[voice] += 0.01f * (1.0f - curves[voice]);
                total += curves[voice] * (sample / 8388608.0f);
            }
            double scaled = (double)total * 8388608.0;
            int32_t expected = scaled >= 8388607.0 ? 8388607 : scaled <= -8388608.0 ? -8388608 : lrint(scaled);
            saturated += scaled >= 8388607.0 || scaled <= -8388608.0;
            assert(out[i * 2] == (frame ? -32768 : 32767) && out[i * 2 + 1] == out[i * 2]);
            for (unsigned bin = 6; bin <= 10; ++bin) {
                uint32_t wanted = bin == 6 || bin == 7 || bin == 10 ? (uint32_t)expected & 0xffffff : 0;
                assert(s->core.mixbuffer[bin * 32 + i] == wanted);
            }
        }
    }
    assert(saturated > 32 && fx.sources[2].frames == 3 && fx.sources[4].frames == 2 && fx.sources[6].frames == 2);
    before = fx; assert(!h2_audio_fx_bind(&fx, s, 26) && !h2_audio_fx_bind_spatial(&fx, s, 26, taps));
    assert(!h2_audio_fx_forget(&fx, H2_FX_SPATIAL25) && !memcmp(&before, &fx, sizeof fx));
    /* A subsequent frame fault is terminal, with no completed frame counted. */
    s->core.pc = 0x1000; assert(!h2_audio_fx_render(&fx, out, 32) && fx.frames == 36 && s->status.fault);
    assert(!h2_audio_fx_render(&fx, out, 32)); h2_dsp_destroy(s);
    puts("Halo 2 FX source: checked ownership, six independent unity routes, real DSP frames and signed GP output pass");
    return 0;
}
