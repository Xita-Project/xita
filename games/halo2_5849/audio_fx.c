#include "audio_fx.h"
#include <string.h>

unsigned h2_audio_fx_mask(unsigned key)
{
    if (key == 13) return 1;
    if (key >= 15 && key <= 22) return 1u << (7 + key - 15);
    for (unsigned bin = 23; bin <= 25; ++bin) {
        unsigned bit = 1u << (1 + (bin - 23) * 2);
        if (key == bin) return bit;
        if (key == (0x10000u | bin)) return bit << 1;
    }
    return 0;
}
static unsigned source_index(unsigned mask) { return mask ? (unsigned)__builtin_ctz(mask) : 0; }
static unsigned source_bin(unsigned index) { return index == 0 ? 13 : index < 7 ? 23 + (index - 1) / 2 : 15 + index - 7; }
static int spatial_key(unsigned key) { return key >= H2_FX_SPATIAL23 && key <= H2_FX_SPATIAL25; }
int h2_audio_fx_bind(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin)
{
    unsigned mask = h2_audio_fx_mask(bin); int32_t source[32];
    if (!fx || !mask || spatial_key(bin) || (fx->bound & mask) || (fx->engine && fx->engine != engine) ||
        (bin != 13 && (fx->bound != mask - 1 || fx->playing != mask - 1)) ||
        (bin >= 15 && bin <= 22 && fx->filtered != 3) ||
        !h2_dsp_read_fx_frame(engine, bin, source)) return 0;
    fx->engine = engine; fx->bound |= mask;
    fx->sources[source_index(mask)] = (h2_audio_fx_source){.routes = 2, .output_mask = 3};
    return 1;
}
int h2_audio_fx_bind_spatial(h2_audio_fx *fx, h2_dsp_engine *engine, unsigned bin, const int8_t taps[31])
{
    unsigned mask = h2_audio_fx_mask(0x10000u | bin);
    int32_t samples[32]; h2_hrtf_model state;
    if (!fx || bin < 23 || bin > 25 || fx->engine != engine || fx->bound != mask - 1 || fx->playing != mask - 1 ||
        !h2_dsp_read_fx_frame(engine, bin, samples) || !h2_hrtf_init(&state, taps)) return 0;
    fx->spatial[bin - 23] = state; fx->sources[source_index(mask)] = (h2_audio_fx_source){.routes = 5, .output_mask = 0x4C0};
    fx->bound |= mask; return 1;
}
int h2_audio_fx_route(h2_audio_fx *fx, unsigned bin, unsigned routes)
{
    unsigned mask = h2_audio_fx_mask(bin);
    if (!fx || !mask || !(fx->bound & mask) || (fx->playing & mask) ||
        bin != 13 || (routes != 2 && routes != 6)) return 0;
    fx->sources[0].routes = routes; fx->sources[0].output_mask = (1u << routes) - 1; return 1;
}
/* Native142 repeats the completed configuration during map sound setup.
 * Only the exact already-running layout is idempotent; never expand the
 * accepted mutable routing state or clear any source/filter/DSP history. */
static int completed_configuration(const h2_audio_fx *fx)
{
    static const unsigned counts[7]={6,4,5,4,5,2,5};
    static const unsigned outputs[7]={63,64,0,128,0,0,1024};
    if(!fx || !fx->engine || fx->bound!=0x7fff || fx->playing!=0x7fff || fx->filtered!=3)return 0;
    for(unsigned i=0;i<7;++i)
        if(fx->sources[i].routes!=counts[i] || fx->sources[i].output_mask!=outputs[i])return 0;
    for(unsigned i=7;i<H2_FX_SOURCES;++i)
        if(fx->sources[i].routes!=1 || fx->sources[i].output_mask!=(1u<<(6+(i-7)%4)))return 0;
    return 1;
}
int h2_audio_fx_route_mask(h2_audio_fx *fx, unsigned key, unsigned output_mask)
{
    if(completed_configuration(fx))
        return (key==23 && output_mask==64) || (key==24 && output_mask==128) ||
               (key==H2_FX_SPATIAL25 && output_mask==1024);
    if (key >= 15 && key <= 22) {
        unsigned mask = h2_audio_fx_mask(key), index = source_index(mask);
        if (!fx || fx->filtered != 3 || fx->bound != mask * 2 - 1 || fx->playing != mask - 1 ||
            output_mask != (1u << (6 + (key - 15) % 4))) return 0;
        fx->sources[index].routes = 1; fx->sources[index].output_mask = output_mask; return 1;
    }
    if (!fx || fx->bound != 127 || fx->playing != 127) return 0;
    unsigned index, count;
    if (key == 23 && output_mask == 64) { index = 1; count = 4; }
    else if (key == 24 && output_mask == 128 && fx->sources[1].output_mask == 64 && !fx->sources[2].output_mask) {
        index = 3; count = 4;
    } else if (key == H2_FX_SPATIAL25 && output_mask == 1024 && fx->sources[3].output_mask == 128 && !fx->sources[4].output_mask) {
        index = 6; count = 5;
    } else return 0;
    if (fx->sources[index].routes != count && fx->sources[index].routes != 2) return 0;
    fx->sources[index].routes = count; fx->sources[index].output_mask = output_mask; return 1;
}
int h2_audio_fx_mute(h2_audio_fx *fx, unsigned key)
{
    if(completed_configuration(fx))return key==H2_FX_SPATIAL23 || key==H2_FX_SPATIAL24 || key==25;
    if (!fx || fx->bound != 127 || fx->playing != 127) return 0;
    unsigned index;
    if (key == H2_FX_SPATIAL23 && fx->sources[1].output_mask == 64) index = 2;
    else if (key == H2_FX_SPATIAL24 && fx->sources[3].output_mask == 128 && !fx->sources[2].output_mask) index = 4;
    else if (key == 25 && fx->sources[6].output_mask == 1024 && !fx->sources[4].output_mask) index = 5;
    else return 0;
    if (fx->sources[index].routes != (key == 25 ? 2u : 5u)) return 0;
    fx->sources[index].output_mask = 0; return 1;
}
int h2_audio_fx_filter(h2_audio_fx *fx, unsigned key)
{
    if(completed_configuration(fx))return key==23 || key==24;
    if (!fx || fx->bound != 127 || fx->playing != 127 ||
        fx->sources[1].output_mask != 64 || fx->sources[2].output_mask ||
        fx->sources[3].output_mask != 128 || fx->sources[4].output_mask ||
        fx->sources[5].output_mask || fx->sources[6].output_mask != 1024 ||
        (key != 23 && key != 24) || (key == 24 && !(fx->filtered & 1))) return 0;
    /* Original setter replaces coefficients without clearing voice history.
     * A zero-initialized new voice owns its independent integrator state. */
    fx->filtered |= 1u << (key - 23); return 1;
}
int h2_audio_fx_play(h2_audio_fx *fx, unsigned bin)
{
    unsigned mask = h2_audio_fx_mask(bin), index = source_index(mask);
    if (!fx || !mask || !(fx->bound & mask) || (fx->playing & mask) ||
        fx->sources[index].routes != (bin == 13 ? 6u : bin >= 15 && bin <= 22 ? 1u : spatial_key(bin) ? 5u : 2u)) return 0;
    fx->playing |= mask; return 1;
}
int h2_audio_fx_forget(h2_audio_fx *fx, unsigned bin)
{
    unsigned mask = h2_audio_fx_mask(bin);
    if (!fx || !mask || !(fx->bound & mask) || (fx->playing & mask)) return 0;
    fx->bound &= ~mask; fx->sources[source_index(mask)] = (h2_audio_fx_source){0};
    if (spatial_key(bin)) memset(&fx->spatial[(bin & 0xffff) - 23], 0, sizeof fx->spatial[0]);
    if (!fx->bound) *fx = (h2_audio_fx){0};
    return 1;
}
static int mix_full_loop(h2_audio_fx *fx, int32_t bins[32][32], const int16_t *pcm)
{
    int32_t sources[H2_FX_SOURCES][32];
    for (unsigned v = 0; v < H2_FX_SOURCES; ++v)
        if ((fx->playing & (1u << v)) && !h2_dsp_read_fx_frame(fx->engine, source_bin(v), sources[v])) return 0;
    float mixed[11][32] = {{0}}; h2_hrtf_fp fp = h2_hrtf_enter();
    /* The movie voice is created after the FX voices, so contributes first
     * in the original reverse creation order. Preserve separate FL/FR values
     * and clamp only after the prior-frame FX contributions are added. */
    if(pcm)for(unsigned i=0;i<32;++i)for(unsigned ch=0;ch<2;++ch)
        mixed[ch][i]=pcm[i*2+ch]/32768.0f;
    /* Each original Play inserts at the MP-list head. Use that reverse
     * creation order as one deterministic reference worker, retaining float
     * contributions until the final GP conversion (not per-voice clipping). */
    for (unsigned v = H2_FX_SOURCES; v-- > 0;) if (fx->playing & (1u << v)) {
        if (v && v < 7 && !(v & 1)) {
            float filtered[32]; h2_hrtf_frame_float(&fx->spatial[(v - 2) / 2], sources[v], filtered);
            /* Muting changes gains only: the active voice still reads and
             * advances its filter history before contributing zero. */
            for (unsigned bin = 0; bin < 11; ++bin) if (fx->sources[v].output_mask & (1u << bin))
                for (unsigned i = 0; i < 32; ++i) mixed[bin][i] += filtered[i];
        } else {
            float samples[32];
            for (unsigned i = 0; i < 32; ++i) {
                samples[i] = sources[v][i] / 8388608.0f;
                if ((v == 1 || v == 3) && (fx->filtered & (1u << ((v - 1) / 2))))
                    samples[i] = h2_audio_filter_sample(&fx->lowpass[(v - 1) / 2], samples[i]);
            }
            for (unsigned bin = 0; bin < 11; ++bin) if (fx->sources[v].output_mask & (1u << bin))
                for (unsigned i = 0; i < 32; ++i) mixed[bin][i] += samples[i];
        }
    }
    for (unsigned bin = 0; bin < 11; ++bin)
        for (unsigned i = 0; i < 32; ++i) bins[bin][i] = h2_hrtf_quantize(mixed[bin][i]);
    h2_hrtf_leave(fp); return 1;
}
static int render(h2_audio_fx *fx, int16_t *stereo, unsigned frames, const int16_t *pcm)
{
    if (!fx || !fx->engine || !fx->playing || (fx->playing & ~fx->bound) || !stereo ||
        !frames || frames > 1024 || (frames & 31) ||
        (pcm && (fx->playing!=0x7fff || fx->filtered!=3 ||
         ((uintptr_t)pcm<=(uintptr_t)stereo ? (uintptr_t)stereo-(uintptr_t)pcm : (uintptr_t)pcm-(uintptr_t)stereo)<frames*4u))) return 0;
    for (unsigned at = 0; at < frames; at += 32) {
        int32_t source[32], bins[32][32] = {{0}};
        uint8_t monitor[256];
        if (fx->playing & ~7u) {
            if (!mix_full_loop(fx, bins, pcm ? pcm+at*2 : NULL)) return 0;
        } else {
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
            h2_hrtf_frame(&fx->spatial[0], source, filtered);
            /* Original zero-position LightHRTF4Channel Play: routes6/7/10
             * unity; routes8/9 muted. These do not overlap the two 2D routes. */
            memcpy(bins[6], filtered, sizeof filtered); memcpy(bins[7], filtered, sizeof filtered);
            memcpy(bins[10], filtered, sizeof filtered);
        }
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
int h2_audio_fx_render(h2_audio_fx *fx,int16_t *stereo,unsigned frames)
{return render(fx,stereo,frames,NULL);}
int h2_audio_fx_render_pcm(h2_audio_fx *fx,int16_t *stereo,unsigned frames,const int16_t *pcm)
{return pcm && render(fx,stereo,frames,pcm);}
