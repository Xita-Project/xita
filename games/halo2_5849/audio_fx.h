#pragma once
#include "dsp_engine.h"
#include "audio_hrtf_model.h"

/* Bounded original FXIN2 loop: bin13 -> routes0..5; bins23..25 -> routes0,1.
 * Each bin23..25 also has a fixed spatial voice into bins6/7/10.
 * Their mono signed-24 sources all read the prior completed GP frame. All
 * other voices must be inactive. The caller owns and serializes the engine,
 * this state, and every read of that engine. */
#define H2_FX_SOURCES 7
/* Internal source keys, distinct from nonspatial voices using the same bins. */
#ifndef H2_FX_SPATIAL23
#define H2_FX_SPATIAL23 0x10017u
#define H2_FX_SPATIAL24 0x10018u
#define H2_FX_SPATIAL25 0x10019u
#endif
typedef struct { unsigned routes; uint64_t frames; } h2_audio_fx_source;
typedef struct {
    h2_dsp_engine *engine;
    unsigned bound, playing;
    uint64_t frames;
    h2_audio_fx_source sources[H2_FX_SOURCES];
    h2_hrtf_model spatial[3];
} h2_audio_fx;
/* Stable bit0=bin13; then nonspatial/spatial pairs23,24,25. Zero is unsupported. */
unsigned h2_audio_fx_mask(unsigned bin);
int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin);
int h2_audio_fx_bind_spatial(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin, const int8_t taps[31]);
int h2_audio_fx_route(h2_audio_fx *fx, unsigned bin, unsigned routes);
int h2_audio_fx_play(h2_audio_fx *fx, unsigned bin);
int h2_audio_fx_forget(h2_audio_fx *fx, unsigned bin);
/* Execute complete 32-sample frames, reading every active source before
 * running the GP. Accumulate complete voice contributions before signed-24
 * saturation; spatial sums use the pinned model's single-worker order.
 * Output is the real GP's FL/FR monitor taps (not EP/AC3 or surround downmix).
 * A fault poisons the engine; never submit partially computed output. */
int h2_audio_fx_render(h2_audio_fx *fx, int16_t *stereo, unsigned frames);
