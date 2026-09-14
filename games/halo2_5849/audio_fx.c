#include "audio_fx.h"
#include <string.h>

int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin)
{
    int32_t source[32];
    if (!fx || fx->engine || bin != 13 || !h2_dsp_read_fx_frame(engine, bin, source)) return 0;
    *fx = (h2_audio_fx){.engine = engine, .routes = 2};
    return 1;
}
int h2_audio_fx_route(h2_audio_fx *fx, unsigned routes)
{
    if (!fx || !fx->engine || fx->playing || (routes != 2 && routes != 6)) return 0;
    fx->routes = routes; return 1;
}
int h2_audio_fx_play(h2_audio_fx *fx)
{
    if (!fx || !fx->engine || fx->playing || fx->routes != 6) return 0;
    fx->playing = 1; return 1;
}
int h2_audio_fx_forget(h2_audio_fx *fx)
{
    if (!fx || !fx->engine || fx->playing) return 0;
    *fx = (h2_audio_fx){0}; return 1;
}
int h2_audio_fx_render(h2_audio_fx *fx, int16_t *stereo, unsigned frames)
{
    if (!fx || !fx->engine || !fx->playing || fx->routes != 6 || !stereo ||
        !frames || frames > 1024 || (frames & 31)) return 0;
    for (unsigned at = 0; at < frames; at += 32) {
        int32_t source[32], bins[32][32] = {{0}};
        uint8_t monitor[256];
        if (!h2_dsp_read_fx_frame(fx->engine, 13, source)) return 0;
        for (unsigned bin = 0; bin < fx->routes; ++bin) memcpy(bins[bin], source, sizeof source);
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
    }
    return 1;
}
