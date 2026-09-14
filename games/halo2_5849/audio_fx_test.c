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
    assert(h2_audio_fx_bind(&fx, s, 13) && fx.routes == 2 && !fx.playing);
    before = fx; assert(!h2_audio_fx_play(&fx) && !memcmp(&before, &fx, sizeof fx));
    assert(!h2_audio_fx_route(&fx, 5) && !memcmp(&before, &fx, sizeof fx));
    assert(h2_audio_fx_forget(&fx) && !fx.engine);
    assert(h2_audio_fx_bind(&fx, s, 13));
    assert(h2_audio_fx_route(&fx, 6) && h2_audio_fx_play(&fx));
    before = fx;
    assert(!h2_audio_fx_play(&fx) && !h2_audio_fx_forget(&fx) && !h2_audio_fx_route(&fx, 2));
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
    /* A subsequent frame fault is terminal, with no completed frame counted. */
    s->core.pc = 0x1000; assert(!h2_audio_fx_render(&fx, out, 32) && fx.frames == 32 && s->status.fault);
    assert(!h2_audio_fx_render(&fx, out, 32)); h2_dsp_destroy(s);
    puts("Halo 2 FX source: checked ownership, six independent unity routes, real DSP frames and signed GP output pass");
    return 0;
}
