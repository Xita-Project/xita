#pragma once
#include <math.h>

/* Project implementation of the fixed state-variable low-pass recurrence
 * selected by the observed XDK filter (cutoff=0, resonance=0x8000).
 * This is an opt-in reference model, not a hardware equivalence claim.
 * Only f=q=1 is supported. See docs/halo2-fxin2-filter.md for provenance,
 * original register evidence, reference comparisons and limitations.
 * Caller uses the mixer FP scope: round-to-nearest, no contraction. */
typedef struct { float band, low; } h2_audio_filter;
static inline float h2_audio_filter_sample(h2_audio_filter *state, float input)
{
    /* Nearest float to sqrt(1/2 + 0.01). Keep the nonlinear band damping
     * and integrator updates separately rounded, including persistent
     * state across grains and repeated identical filter selection. */
    float driven = input * 0x1.6da422p-1f;
    float band = state->band;
    band -= band * band * band * 0.001f;
    float high = driven - state->low - band;
    band += high;
    state->low += band;
    state->band = band;
    return fminf(fmaxf(state->low, -1.0f), 1.0f);
}
