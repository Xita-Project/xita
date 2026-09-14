#pragma once
#include "dsp_engine.h"

/* Sole nonspatial FXIN2/bin13 owner. Its mono signed-24 source is the prior
 * completed GP frame. All other voices must be inactive. The caller owns and
 * serializes the engine, this state, and every read of that engine. */
typedef struct {
    h2_dsp_engine *engine;
    unsigned routes, playing;
    uint64_t frames;
} h2_audio_fx;
int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin);
int h2_audio_fx_route(h2_audio_fx *fx, unsigned routes);
int h2_audio_fx_play(h2_audio_fx *fx);
int h2_audio_fx_forget(h2_audio_fx *fx);
/* Execute complete 32-sample frames. Output is the real GP's FL/FR monitor
 * taps (not EP/AC3 or a surround downmix). A DSP fault poisons the engine;
 * partially computed output must never be submitted to the sink. */
int h2_audio_fx_render(h2_audio_fx *fx, int16_t *stereo, unsigned frames);
