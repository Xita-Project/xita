#pragma once
#include "dsp_engine.h"
#include "audio_hrtf_model.h"
#include "audio_filter_model.h"

/* Bounded original FXIN2 loop: bin13 -> routes0..5; bins23..25 initially ->0,1.
 * Each bin23..25 also has a fixed spatial voice initially into bins6/7/10.
 * The verified later sequence routes23->6,24->7,spatial25->10 and mutes
 * spatial23,spatial24,nonspatial25, without retiring their active processing.
 * After that, bins15..22 each route to one of6/7/8/9, repeated twice.
 * Their mono signed-24 sources all read the prior completed GP frame. All
 * other voices must be inactive. The caller owns and serializes the engine,
 * this state, and every read of that engine. */
/* Internal source keys, distinct from nonspatial voices using the same bins. */
#include "audio_fx_limits.h"
/* attenuation: the original VP mix-bin volume that SetVolume writes at once
 * (0x381CE4/0x380B97): -(volume+bin gain)*64/100 in 1/64 dB saturated at FFF,
 * which the voice processor treats as silence (gain 10^(-a/1280) otherwise). */
typedef struct { unsigned routes, output_mask, attenuation; uint64_t frames; } h2_audio_fx_source;
typedef struct {
    h2_dsp_engine *engine;
    unsigned bound, playing;
    uint64_t frames;
    h2_audio_fx_source sources[H2_FX_SOURCES];
    h2_hrtf_model spatial[3];
    unsigned filtered, muted_extra; /* bits 0..7 mirror FX15..22 attenuation FFF */
    h2_audio_filter lowpass[2]; /* independent nonspatial23/24 histories */
} h2_audio_fx;
/* Stable source bits are defined in audio_fx_limits.h. Zero is unsupported. */
unsigned h2_audio_fx_mask(unsigned bin);
int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin);
int h2_audio_fx_bind_spatial(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin, const int8_t taps[31]);
int h2_audio_fx_route(h2_audio_fx *fx, unsigned bin, unsigned routes);
/* Exact original active FX23/24/spatial25 replacements and inactive single
 * routes for15..22 only; exact unchanged repeats are accepted after the full
 * configuration. The creation/Play sequence remains enforced. */
int h2_audio_fx_route_mask(h2_audio_fx *fx, unsigned bin, unsigned output_mask);
int h2_audio_fx_mute(h2_audio_fx *fx, unsigned key);
/* Active FX15..22 SetVolume from the original zone loop (0x21F069): any
 * 12-bit VP attenuation; FFF is the audited mute, 0 unity. Routes, source
 * ownership, filters and all processing time are retained. */
int h2_audio_fx_attenuate(h2_audio_fx *fx, unsigned key, unsigned attenuation);
int h2_audio_fx_filter(h2_audio_fx *fx, unsigned key);
/* Read-only admission for the observed unchanged fixed-geometry commit. */
int h2_audio_fx_fixed_commit_ready(const h2_audio_fx *fx);
int h2_audio_fx_play(h2_audio_fx *fx, unsigned bin);
int h2_audio_fx_forget(h2_audio_fx *fx, unsigned bin);
/* Execute complete 32-sample frames, reading every active source before
 * running the GP. Accumulate complete voice contributions before signed-24
 * saturation; spatial sums use the pinned model's single-worker order.
 * Output is the real GP's FL/FR monitor taps (not EP/AC3 or surround downmix).
 * A fault poisons the engine; never submit partially computed output. */
int h2_audio_fx_render(h2_audio_fx *fx, int16_t *stereo, unsigned frames);
/* Verified late movie voice: already decoded/resampled PCM16 FL/FR at unity.
 * Add before the complete FX loop's contributions, then convert once to GP
 * signed24. Input and output must not overlap. No surround route is inferred. */
int h2_audio_fx_render_pcm(h2_audio_fx *fx,int16_t *stereo,unsigned frames,const int16_t *pcm);
