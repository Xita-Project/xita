/* Synthetic taps only; independent one-tap recurrence and state/FP tests. */
#include "audio_hrtf_model.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    h2_hrtf_model s = {0}, before = s; int8_t taps[31] = {0};
    assert(!h2_hrtf_init(NULL, taps) && !h2_hrtf_init(&s, NULL));
    assert(!h2_hrtf_init(&s, taps) && !memcmp(&before, &s, sizeof s));
    taps[0] = 127; assert(h2_hrtf_init(&s, taps));
    int32_t input[32], out[32]; for (unsigned i = 0; i < 32; ++i) input[i] = 0x400000;
    float coefficient = 0;
    for (unsigned frame = 0; frame < 17; ++frame) {
        h2_hrtf_frame(&s, input, out);
        for (unsigned i = 0; i < 32; ++i) {
            coefficient += 0.01f * (1.0f - coefficient);
            assert(out[i] == lrint((double)(coefficient * 0.5f) * 8388608.0));
        }
    }
    assert(s.position == (17 * 32) % 31);
    /* Ring history survives across frames; a delayed negative tap cannot be
     * mistaken for an unfiltered copy or a cleared per-frame history. */
    memset(taps, 0, sizeof taps); taps[30] = -128; assert(h2_hrtf_init(&s, taps));
    memset(input, 0, sizeof input); input[31] = 0x400000; h2_hrtf_frame(&s, input, out);
    for (unsigned i = 0; i < 32; ++i) assert(out[i] == 0);
    memset(input, 0, sizeof input); h2_hrtf_frame(&s, input, out);
    for (unsigned i = 0; i < 32; ++i) assert(i == 29 ? out[i] < 0 : out[i] == 0);
    /* Caller rounding and sticky flags neither affect model samples nor leak
     * model exceptions back into the caller's environment. */
    int8_t varied[31]; for (unsigned i = 0; i < 31; ++i) varied[i] = (int8_t)(i * 7 - 109);
    h2_hrtf_model nearest; assert(h2_hrtf_init(&nearest, varied));
    for (unsigned i = 0; i < 32; ++i) input[i] = i & 1 ? -8388608 : 8388607;
    int32_t expected[32]; h2_hrtf_frame(&nearest, input, expected);
    const int rounds[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (unsigned r = 0; r < 4; ++r) {
        fesetround(rounds[r]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_INVALID | FE_DIVBYZERO);
        int flags = fetestexcept(FE_ALL_EXCEPT); assert(h2_hrtf_init(&s, varied));
        assert(fegetround() == rounds[r] && fetestexcept(FE_ALL_EXCEPT) == flags);
        h2_hrtf_frame(&s, input, out); assert(!memcmp(out, expected, sizeof out));
        assert(fegetround() == rounds[r] && fetestexcept(FE_ALL_EXCEPT) == flags);
        assert(!memcmp(&s, &nearest, sizeof s));
    }
    fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);
    puts("Halo 2 symmetric HRTF model: recurrence, delayed history, signed samples and FP environment pass");
    return 0;
}
