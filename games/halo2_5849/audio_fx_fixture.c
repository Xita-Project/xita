/* Separate translation unit keeps the interpreter's execution guard distinct
 * from the fake platform's concurrent thread state. No owned program bytes. */
#define H2_AUDIO_FX_TEST_MAIN fx_fixture_test_main
#include "audio_fx_test.c"
h2_dsp_engine *h2_test_fx_engine(void)
{
    h2_dsp_engine *s = fx_fixture();
    for (unsigned i = 0; i < 32; ++i) {
        put32(s->scratch + 0xb100 + i * 4, 500000);
        put32(s->scratch + 0xb600 + i * 4, 500000);
    }
    s->effect_count = 1; s->state_offset = 0x818;
    s->effects[0] = (h2_dsp_effect){.state_offset = 0x818, .state_bytes = 128};
    for (unsigned i = 0; i < 32; ++i) s->core.xram[0x80 + i] = 0x765432;
    return s;
}
void h2_test_fx_fault(h2_dsp_engine *s) { s->core.pc = 0x1000; }
