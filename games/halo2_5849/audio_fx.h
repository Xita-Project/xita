#pragma once
#include "dsp_engine.h"

/* Bounded nonspatial FXIN2 sources: bin13 -> routes0..5 and bin23 -> routes0,1.
 * Their mono signed-24 sources all read the prior completed GP frame. All
 * other voices must be inactive. The caller owns and serializes the engine,
 * this state, and every read of that engine. */
#define H2_FX_SOURCES 2
typedef struct { unsigned routes; uint64_t frames; } h2_audio_fx_source;
typedef struct {
    h2_dsp_engine *engine;
    unsigned bound, playing;
    uint64_t frames;
    h2_audio_fx_source sources[H2_FX_SOURCES];
} h2_audio_fx;
/* Stable bit0=bin13, bit1=bin23; zero means unsupported. */
unsigned h2_audio_fx_mask(unsigned bin);
int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin);
int h2_audio_fx_route(h2_audio_fx *fx, unsigned bin, unsigned routes);
int h2_audio_fx_play(h2_audio_fx *fx, unsigned bin);
int h2_audio_fx_forget(h2_audio_fx *fx, unsigned bin);
/* Execute complete 32-sample frames, reading every active source before
 * running the GP. Accumulate unity routes exactly before signed-24 saturation.
 * Output is the real GP's FL/FR monitor taps (not EP/AC3 or surround downmix).
 * A fault poisons the engine; never submit partially computed output. */
int h2_audio_fx_render(h2_audio_fx *fx, int16_t *stereo, unsigned frames);
