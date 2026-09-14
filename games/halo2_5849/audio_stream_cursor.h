#pragma once
#include "kernel/xk_audio.h"
/* Position at the resampler, excluding encoded read-ahead and unplayed decoded
 * frames. Relative to the queue head; the owner adds already removed packets.
 * Only the audited mono16/8kHz stream shape is accepted. */
typedef struct { uint64_t source_frames; int drained; } h2_stream_cursor;
static inline int h2_stream_cursor_from_voice(const xa_voice *v, h2_stream_cursor *out)
{
    if (!v || !out || !v->used || v->kind!=2 || v->adpcm || v->channels!=1 || v->bits!=16 ||
        v->rate!=8000 || v->freq_override || v->volume!=1.0f || v->nq<0 || v->nq>XA_MAX_PKTS ||
        v->rd<0 || v->rd>v->nq || v->qhead<0 || v->qhead>=XA_MAX_PKTS ||
        v->blk_frames<0 || v->blk_frames>64 || v->blk_i<0 || v->blk_i>v->blk_frames ||
        v->ncarry>128 || (v->ncarry&1) || v->frac>=65536) return 0;
    uint64_t bytes=0;
    for (int i=0;i<v->rd;++i) {
        const xa_pkt *p=&v->q[(v->qhead+i)%XA_MAX_PKTS];
        if (!p->consumed || !p->size || (p->size&1)) return 0;
        bytes+=p->size;
    }
    if (v->rd<v->nq) {
        const xa_pkt *p=&v->q[(v->qhead+v->rd)%XA_MAX_PKTS];
        if (!p->size || (p->size&1) || v->pkt_pos>p->size || (v->pkt_pos&1)) return 0;
    } else if (v->pkt_pos) return 0;
    bytes+=v->pkt_pos;
    uint64_t pending=v->ncarry+(uint32_t)(v->blk_frames-v->blk_i)*2u;
    if (bytes<pending) return 0;
    *out=(h2_stream_cursor){(bytes-pending)/2,
        v->rd==v->nq && !v->ncarry && v->blk_i==v->blk_frames};
    return 1;
}
/* The private H2 mixer translation unit serializes this read. */
int h2_audio_stream_cursor_read(int voice, h2_stream_cursor *out);
