/*
 * Fixed symmetric, zero-delay subset of xemu's HRTF filter.
 * Copyright (c) 2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. This library is distributed WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See <https://www.gnu.org/licenses/>.
 *
 * Adapted from xemu 75650bd8cd91945f7b79774e2cee0b200ca373ff vp/hrtf.h.
 * Only identical left/right coefficients and zero interaural delay are used.
 * Normalization and 0.01 parameter smoothing follow that emulator model;
 * these are not established MCPX transition timing. No owned taps are here.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fenv.h>

typedef struct {
    float history[31], current[31], target[31];
    unsigned position;
} h2_hrtf_model;
/* The model uses nearest, gradual-underflow arithmetic independently of the
 * guest thread's controls, then restores all host controls/status. */
#ifdef __arm__
typedef uint32_t h2_hrtf_fp;
static inline h2_hrtf_fp h2_hrtf_enter(void)
{
    uint32_t old, zero = 0;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(old) :: "memory");
    __asm__ volatile("vmsr fpscr, %0" :: "r"(zero) : "memory"); return old;
}
static inline void h2_hrtf_leave(h2_hrtf_fp old)
{ __asm__ volatile("vmsr fpscr, %0" :: "r"(old) : "memory"); }
#else
typedef fenv_t h2_hrtf_fp;
static inline h2_hrtf_fp h2_hrtf_enter(void)
{ fenv_t old; feholdexcept(&old); fesetround(FE_TONEAREST); return old; }
static inline void h2_hrtf_leave(h2_hrtf_fp old) { fesetenv(&old); }
#endif
static inline int h2_hrtf_init(h2_hrtf_model *state, const int8_t taps[31])
{
    if (!state || !taps) return 0;
    unsigned magnitude = 0;
    for (unsigned i = 0; i < 31; ++i) magnitude += taps[i] < 0 ? -(int)taps[i] : taps[i];
    if (!magnitude) return 0;
    h2_hrtf_fp fp = h2_hrtf_enter(); h2_hrtf_model next = {0}; float sum = 0;
    for (unsigned i = 0; i < 31; ++i) { next.target[i] = taps[i] / 128.0f; sum += fabsf(next.target[i]); }
    if (sum != 1.0f) for (unsigned i = 0; i < 31; ++i) next.target[i] /= sum;
    *state = next; h2_hrtf_leave(fp); return 1;
}
static inline void h2_hrtf_frame(h2_hrtf_model *state, const int32_t in[32], int32_t out[32])
{
    h2_hrtf_fp fp = h2_hrtf_enter();
    for (unsigned n = 0; n < 32; ++n) {
        for (unsigned k = 0; k < 31; ++k)
            state->current[k] += 0.01f * (state->target[k] - state->current[k]);
        state->history[state->position] = in[n] / 8388608.0f;
        float acc = 0;
        for (unsigned k = 0; k < 31; ++k) {
            unsigned index = state->position >= k ? state->position - k : state->position + 31 - k;
            acc += state->current[k] * state->history[index];
        }
        double scaled = (double)acc * 8388608.0;
        out[n] = scaled >= 8388607.0 ? 8388607 : scaled <= -8388608.0 ? -8388608 : (int32_t)lrint(scaled);
        if (++state->position == 31) state->position = 0;
    }
    h2_hrtf_leave(fp);
}
