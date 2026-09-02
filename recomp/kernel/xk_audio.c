/* xk_audio.c - software mixer behind the DirectSound HLE (see xk_audio.h). */
#include "xk.h"
#include "xk_audio.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static xa_voice g_v[XA_MAX_VOICES];
static int g_available;
static int g_master_pct = 100;

/* ---- Xbox ADPCM (IMA, 64 samples per block, 36 bytes per channel per block) ---------------------- */
static const int ima_index_tbl[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int ima_step_tbl[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };

static inline int16_t ima_step(int *pred, int *idx, unsigned nib)
{
    int step = ima_step_tbl[*idx], diff = step >> 3;
    if (nib & 1) diff += step >> 2;
    if (nib & 2) diff += step >> 1;
    if (nib & 4) diff += step;
    if (nib & 8) diff = -diff;
    int s = *pred + diff; if (s > 32767) s = 32767; if (s < -32768) s = -32768; *pred = s;
    *idx += ima_index_tbl[nib & 15]; if (*idx < 0) *idx = 0; if (*idx > 88) *idx = 88;
    return (int16_t)s;
}

/* decode one Xbox ADPCM block (channels interleaved in 4-byte groups after the per-channel headers) into
 * 64 interleaved frames */
static void adpcm_block(const uint8_t *src, int channels, int16_t *out)
{
    int pred[2], idx[2];
    for (int ch = 0; ch < channels; ++ch) {
        pred[ch] = (int16_t)(src[ch * 4] | (src[ch * 4 + 1] << 8));
        idx[ch] = src[ch * 4 + 2]; if (idx[ch] > 88) idx[ch] = 88;
        out[ch] = (int16_t)pred[ch];
    }
    const uint8_t *p = src + 4 * channels;
    /* 8 groups x (4 bytes per channel) = 32 bytes per channel = 64 nibbles; header sample is frame 0's
     * predictor, the 64 decoded samples follow (the block is 64 samples: the first one is the header) */
    int frame = 1;
    for (int g = 0; g < 8; ++g) {
        for (int ch = 0; ch < channels; ++ch) {
            for (int b = 0; b < 4; ++b) {
                uint8_t byte = p[(g * channels + ch) * 4 + b];
                int f0 = frame + b * 2, f1 = f0 + 1;
                int16_t s0 = ima_step(&pred[ch], &idx[ch], byte & 0xF), s1 = ima_step(&pred[ch], &idx[ch], byte >> 4);
                if (f0 < 64) out[f0 * channels + ch] = s0;
                if (f1 < 64) out[f1 * channels + ch] = s1;
            }
        }
        frame += 8;
    }
}

/* ---- voice bookkeeping ------------------------------------------------------------------------ */
static void voice_parse_wfx(xa_voice *v, uint32_t wfx)
{
    v->adpcm = 0; v->channels = 2; v->rate = 48000; v->bits = 16; v->block_align = 4;
    if (!wfx) return;
    unsigned tag = X_M16(wfx), ch = X_M16(wfx + 2); uint32_t rate = X_M32(wfx + 4); unsigned ba = X_M16(wfx + 12), bits = X_M16(wfx + 14);
    if (ch == 1 || ch == 2) v->channels = (int)ch;
    if (rate >= 4000 && rate <= 96000) v->rate = (int)rate;
    if (tag == 0x69) { v->adpcm = 1; v->block_align = 36u * (uint32_t)v->channels; if (ba) v->block_align = ba; }
    else { v->bits = (bits == 8) ? 8 : 16; v->block_align = (uint32_t)v->channels * (uint32_t)(v->bits / 8); }
}

int xk_audio_init(void)
{
    memset(g_v, 0, sizeof g_v);
    { const char *e = getenv("XV_VOLUME"); if (e) g_master_pct = atoi(e); }
    g_available = xk_os_audio_open(XA_OUT_RATE, XA_GRAIN) >= 0;
    XK_LOG("[audio] mixer %s (%d Hz, grain %d, %d voices)\n", g_available ? "on" : "OFF (no device)", XA_OUT_RATE, XA_GRAIN, XA_MAX_VOICES);
    return g_available ? 0 : -1;
}
int  xk_audio_available(void) { return g_available; }
void xk_audio_lock(void)   { xk_os_audio_mutex_lock(); }
void xk_audio_unlock(void) { xk_os_audio_mutex_unlock(); }

int xk_audio_voice_new(int kind, uint32_t wfx_guest)
{
    xk_audio_lock();
    int i; for (i = 0; i < XA_MAX_VOICES; ++i) if (!g_v[i].used) break;
    if (i == XA_MAX_VOICES) { xk_audio_unlock(); return -1; }
    xa_voice *v = &g_v[i]; memset(v, 0, sizeof *v); v->used = 1; v->kind = kind; v->volume = 1.0f;
    voice_parse_wfx(v, wfx_guest);
    xk_audio_unlock();
    { static unsigned n; if (n++ < 12) XK_LOG("[audio] voice %d: %s %s %d ch %d Hz%s\n", i, kind == 2 ? "stream" : "buffer", v->adpcm ? "XADPCM" : "PCM", v->channels, v->rate, v->adpcm ? "" : (v->bits == 8 ? " 8-bit" : " 16-bit")); }
    return i;
}
void xk_audio_voice_free(int i) { if (i < 0 || i >= XA_MAX_VOICES) return; xk_audio_lock(); g_v[i].used = 0; g_v[i].playing = 0; xk_audio_unlock(); }
void xk_audio_voice_set_format(int i, uint32_t wfx) { if (i < 0) return; xk_audio_lock(); voice_parse_wfx(&g_v[i], wfx); xk_audio_unlock(); }
void xk_audio_voice_set_data(int i, uint32_t guest, uint32_t size) { if (i < 0) return; xk_audio_lock(); g_v[i].data = guest; g_v[i].size = size; g_v[i].pos = 0; g_v[i].blk_frames = 0; xk_audio_unlock(); }
void xk_audio_voice_play(int i, int looping)
{
    if (i < 0) return; xk_audio_lock(); xa_voice *v = &g_v[i];
    v->looping = looping; v->playing = 1; v->blk_frames = v->blk_i = 0; v->frac = 0; v->frames_out = 0;
    if (v->kind == 1 && v->pos >= v->size) v->pos = 0;
    xk_audio_unlock();
}
void xk_audio_voice_stop(int i) { if (i < 0) return; xk_audio_lock(); g_v[i].playing = 0; xk_audio_unlock(); }
void xk_audio_voice_set_pos(int i, uint32_t p) { if (i < 0) return; xk_audio_lock(); xa_voice *v = &g_v[i]; if (v->block_align) p -= p % v->block_align; v->pos = p; v->blk_frames = 0; xk_audio_unlock(); }
uint32_t xk_audio_voice_pos(int i) { return i < 0 ? 0 : g_v[i].pos; }
int  xk_audio_voice_playing(int i) { return i < 0 ? 0 : g_v[i].playing; }
void xk_audio_voice_set_volume_db100(int i, int32_t db100)
{
    if (i < 0) return;
    if (db100 > 0) db100 = 0; if (db100 < -10000) db100 = -10000;
    g_v[i].volume = db100 <= -9600 ? 0.0f : powf(10.0f, (float)db100 / 2000.0f);
}
void xk_audio_voice_set_frequency(int i, uint32_t hz) { if (i >= 0) g_v[i].freq_override = (hz >= 100 && hz <= 192000) ? (int)hz : 0; }
void xk_audio_voice_set_loop(int i, uint32_t start, uint32_t len) { if (i < 0) return; xk_audio_lock(); g_v[i].loop_start = start; g_v[i].loop_len = len; xk_audio_unlock(); }

int xk_audio_stream_push(int i, uint32_t guest, uint32_t size)
{
    if (i < 0) return -1; xk_audio_lock(); xa_voice *v = &g_v[i];
    if (v->nq >= XA_MAX_PKTS) { xk_audio_unlock(); return -1; }
    xa_pkt *p = &v->q[(v->qhead + v->nq) % XA_MAX_PKTS]; p->guest = guest; p->size = size; p->consumed = 0; v->nq++;
    v->playing = 1;
    xk_audio_unlock(); return 0;
}
int xk_audio_stream_pop_consumed(int i)
{
    if (i < 0) return 0; xk_audio_lock(); xa_voice *v = &g_v[i]; int r = 0;
    if (v->nq && v->q[v->qhead].consumed) { v->qhead = (v->qhead + 1) % XA_MAX_PKTS; v->nq--; if (v->rd > 0) v->rd--; r = 1; }
    xk_audio_unlock(); return r;
}
void xk_audio_stream_flush(int i) { if (i < 0) return; xk_audio_lock(); xa_voice *v = &g_v[i]; v->nq = 0; v->qhead = 0; v->rd = 0; v->pkt_pos = 0; v->blk_frames = 0; xk_audio_unlock(); }

/* ---- mixing --------------------------------------------------------------------------------- */
/* Refill v->blk with the next decoded frames from the voice's source; 0 = no more data right now. */
static int voice_refill(xa_voice *v)
{
    const uint8_t *src; uint32_t avail;
    if (v->kind == 1) {
        if (!v->data || !v->size) return 0;
        uint32_t end = v->size;
        if (v->looping && v->loop_len && v->loop_start + v->loop_len <= v->size) end = v->loop_start + v->loop_len;
        if (v->pos >= end) {
            if (!v->looping) { v->playing = 0; return 0; }
            v->pos = (v->loop_len && v->loop_start < v->size) ? v->loop_start : 0;
            if (v->pos >= end) return 0;
        }
        src = (const uint8_t *)X_G(v->data + v->pos); avail = end - v->pos;
    } else {
        xa_pkt *p;
        for (;;) {
            if (v->rd >= v->nq) return 0;                                   /* starved: nothing queued yet */
            p = &v->q[(v->qhead + v->rd) % XA_MAX_PKTS];
            if (v->pkt_pos < p->size) break;
            p->consumed = 1; v->rd++; v->pkt_pos = 0;                       /* done with this packet: the game thread pops it */
        }
        src = (const uint8_t *)X_G(p->guest + v->pkt_pos); avail = p->size - v->pkt_pos;
    }
    uint32_t used;
    if (v->adpcm) {
        if (avail < v->block_align) { used = avail; v->blk_frames = 0; }
        else { adpcm_block(src, v->channels, v->blk); v->blk_frames = 64; used = v->block_align; }
    } else {
        uint32_t fb = (uint32_t)v->channels * (uint32_t)(v->bits / 8), n = avail / fb; if (n > 64) n = 64;
        if (v->bits == 16) { for (uint32_t k = 0; k < n * (uint32_t)v->channels; ++k) v->blk[k] = (int16_t)(src[2 * k] | (src[2 * k + 1] << 8)); }
        else { for (uint32_t k = 0; k < n * (uint32_t)v->channels; ++k) v->blk[k] = (int16_t)((src[k] - 128) << 8); }
        v->blk_frames = (int)n; used = n * fb;
        if (!n) { used = avail; }
    }
    v->blk_i = 0;
    if (v->kind == 1) v->pos += used; else v->pkt_pos += used;
    return v->blk_frames > 0;
}

void xk_audio_mix(int16_t *out, int frames)
{
    static int32_t acc[XA_GRAIN * 2];
    if (frames > XA_GRAIN) frames = XA_GRAIN;
    memset(acc, 0, (size_t)frames * 2 * sizeof acc[0]);
    xk_audio_lock();
    for (int vi = 0; vi < XA_MAX_VOICES; ++vi) {
        xa_voice *v = &g_v[vi];
        if (!v->used || !v->playing) continue;
        int rate = v->freq_override ? v->freq_override : v->rate;
        uint32_t step = (uint32_t)(((uint64_t)rate << 16) / XA_OUT_RATE);      /* source frames per output frame, 16.16 */
        int vol = (int)(v->volume * 256.0f * g_master_pct / 100.0f);
        for (int f = 0; f < frames; ++f) {
            /* advance the source position by step; fetch frames as needed */
            v->frac += step;
            while (v->frac >= 0x10000u) {
                if (v->blk_i >= v->blk_frames && !voice_refill(v)) { v->frac = 0; goto next_voice_or_silence; }
                v->last[0] = v->blk[v->blk_i * v->channels];
                v->last[1] = v->channels == 2 ? v->blk[v->blk_i * v->channels + 1] : v->last[0];
                v->blk_i++; v->frac -= 0x10000u;
            }
            acc[f * 2] += (v->last[0] * vol) >> 8;
            acc[f * 2 + 1] += (v->last[1] * vol) >> 8;
            v->frames_out++;
            continue;
        next_voice_or_silence:
            break;                                   /* starved (stream) or finished (buffer): rest of this grain silent */
        }
    }
    xk_audio_unlock();
    for (int k = 0; k < frames * 2; ++k) { int32_t s = acc[k]; out[k] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }
}

/* the sink thread: mix a grain, hand it to the OS, repeat */
static void mixer_thread(void *arg)
{
    (void)arg;
    static int16_t buf[XA_GRAIN * 2];
    for (;;) { xk_audio_mix(buf, XA_GRAIN); xk_os_audio_write(buf, XA_GRAIN); }
}
int xk_audio_start(void) { return xk_os_audio_thread_start(mixer_thread, NULL); }
