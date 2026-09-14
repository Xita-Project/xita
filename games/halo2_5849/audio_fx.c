#include "audio_fx.h"
#include <string.h>

unsigned h2_audio_fx_mask(unsigned bin)
{ return bin == 13 ? 1 : bin == 23 ? 2 : bin == H2_FX_SPATIAL23 ? 4 : 0; }
static unsigned source_index(unsigned mask) { return mask == 1 ? 0 : mask == 2 ? 1 : 2; }
static unsigned source_bin(unsigned index) { return index == 0 ? 13 : 23; }
int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin)
{
    unsigned mask = h2_audio_fx_mask(bin); int32_t source[32];
    if (!fx || !mask || bin == H2_FX_SPATIAL23 || (fx->bound & mask) || (fx->engine && fx->engine != engine) ||
        !h2_dsp_read_fx_frame(engine, bin, source)) return 0;
    fx->engine = engine; fx->bound |= mask;
    fx->sources[mask == 1 ? 0 : 1] = (h2_audio_fx_source){.routes = 2};
    return 1;
}
int h2_audio_fx_bind_spatial(h2_audio_fx *fx, h2_dsp_engine *engine, const int8_t taps[31])
{
    int32_t samples[32]; h2_hrtf_model state;
    if (!fx || fx->engine != engine || fx->bound != 3 || fx->playing != 3 ||
        !h2_dsp_read_fx_frame(engine, 23, samples) || !h2_hrtf_init(&state, taps)) return 0;
    fx->spatial = state; fx->sources[2] = (h2_audio_fx_source){.routes = 5};
    fx->bound |= 4; return 1;
}
int h2_audio_fx_route(h2_audio_fx *fx, unsigned bin, unsigned routes)
{
    unsigned mask = h2_audio_fx_mask(bin);
    if (!fx || !mask || !(fx->bound & mask) || (fx->playing & mask) ||
        bin != 13 || (routes != 2 && routes != 6)) return 0;
    fx->sources[0].routes = routes; return 1;
}
int h2_audio_fx_play(h2_audio_fx *fx, unsigned bin)
{
    unsigned mask = h2_audio_fx_mask(bin), index = source_index(mask);
    if (!fx || !mask || !(fx->bound & mask) || (fx->playing & mask) ||
        fx->sources[index].routes != (bin == 13 ? 6u : bin == 23 ? 2u : 5u)) return 0;
    fx->playing |= mask; return 1;
}
int h2_audio_fx_forget(h2_audio_fx *fx, unsigned bin)
{
    unsigned mask = h2_audio_fx_mask(bin);
    if (!fx || !mask || !(fx->bound & mask) || (fx->playing & mask)) return 0;
    fx->bound &= ~mask; fx->sources[source_index(mask)] = (h2_audio_fx_source){0};
    if (mask == 4) memset(&fx->spatial, 0, sizeof fx->spatial);
    if (!fx->bound) *fx = (h2_audio_fx){0};
    return 1;
}
int h2_audio_fx_render(h2_audio_fx *fx, int16_t *stereo, unsigned frames)
{
    if (!fx || !fx->engine || !fx->playing || (fx->playing & ~fx->bound) || !stereo ||
        !frames || frames > 1024 || (frames & 31)) return 0;
    for (unsigned at = 0; at < frames; at += 32) {
        int32_t source[32], bins[32][32] = {{0}};
        uint8_t monitor[256];
        for (unsigned v = 0; v < 2; ++v) if (fx->playing & (1u << v)) {
            if (!h2_dsp_read_fx_frame(fx->engine, source_bin(v), source)) return 0;
            for (unsigned bin = 0; bin < fx->sources[v].routes; ++bin)
                for (unsigned i = 0; i < 32; ++i) bins[bin][i] += source[i];
        }
        /* Two signed-24 unity sources fit exactly in int32 and also in the
         * pinned reference's float accumulator. Clamp only after both have
         * contributed; per-source clipping would lose opposite-sign energy. */
        for (unsigned bin = 0; bin < 6; ++bin) for (unsigned i = 0; i < 32; ++i) {
            if (bins[bin][i] > 8388607) bins[bin][i] = 8388607;
            if (bins[bin][i] < -8388608) bins[bin][i] = -8388608;
        }
        if (fx->playing & 4) {
            int32_t filtered[32];
            if (!h2_dsp_read_fx_frame(fx->engine, 23, source)) return 0;
            h2_hrtf_frame(&fx->spatial, source, filtered);
            /* Original zero-position LightHRTF4Channel Play: routes6/7/10
             * unity; routes8/9 muted. These do not overlap the two 2D routes. */
            memcpy(bins[6], filtered, sizeof filtered); memcpy(bins[7], filtered, sizeof filtered);
            memcpy(bins[10], filtered, sizeof filtered);
        }
        if (!h2_dsp_mix_frame(fx->engine, bins) ||
            !h2_dsp_copy_space(fx->engine, 0, 0x3000, monitor, sizeof monitor)) return 0;
        for (unsigned i = 0; i < 32; ++i) for (unsigned channel = 0; channel < 2; ++channel) {
            const uint8_t *p = monitor + (channel * 32 + i) * 4;
            /* Discard the low eight Q23 bits, retaining signed two's-complement
             * 16-bit output exactly, including negative floor quantization. */
            unsigned bits = p[1] | (unsigned)p[2] << 8;
            stereo[(at + i) * 2 + channel] = (int16_t)(bits < 0x8000 ? (int)bits : (int)bits - 65536);
        }
        ++fx->frames;
        for (unsigned v = 0; v < H2_FX_SOURCES; ++v)
            if (fx->playing & (1u << v)) ++fx->sources[v].frames;
    }
    return 1;
}
