/* Halo 2 owns this output worker; the shared mixer algorithms are unchanged.
 * Its OS calls are renamed at compile time, so the CE sink is not selected. */
#include "audio_host.h"
#include "audio_bins.h"
#include "audio_progress.h"
#if H2_AUDIO_DSP
#include "audio_fx.h"
#include "audio_stream_cursor.h"
#include "audio_mixer_bridge.h"
static h2_audio_fx fx;
typedef struct {
    int voice;
    unsigned count;
    struct { uint64_t ticket, end_source, fence; } packets[2];
    uint64_t end, removed;
} gp_stream_state;
static gp_stream_state gp_streams[4];
static unsigned gp_stream_decoded,gp_stream_completed;
static uint64_t gp_stream_serial;
static int gp_pcm_voice[2] = {-1,-1};
static unsigned gp_pcm_active, gp_pcm_queued;
static uint64_t gp_pcm_submitted[2], gp_pcm_consumed[2];
static _Alignas(64) int16_t gp_pcm_output[1024 * 2];
static uint64_t fx_submitted, fx_consumed, fx_compute_us, fx_max_compute_us;
static uint32_t fx_queued, fx_deadline_misses, fx_empty_after_compute;
static uint64_t fx_source_submitted[H2_FX_SOURCES], fx_source_consumed[H2_FX_SOURCES];
#endif
#include "recomp/kernel/xk_audio.h"
#ifndef H2_AUDIO_PLATFORM_TEST
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#endif
#include <string.h>

static SceUID port = -1, mutex = -1, worker = -1, ready = -1;
static SceUID progress_mutex = -1;
static h2_audio_progress progress;
static uint64_t submitted_at;
static int started, running;
static uint32_t grains, nonzero_grains, peak, error;
static uint32_t last_peak_left, last_peak_right;
static _Alignas(64) int16_t output[2][XA_GRAIN * 2];
extern void xv_logf(const char *, ...);

int h2_audio_sink_open(int rate, int grain)
{
    if (port >= 0 || mutex >= 0 || rate != XA_OUT_RATE || grain != XA_GRAIN) return -1;
    mutex = sceKernelCreateMutex("h2_audio_mix", 0, 0, NULL);
    if (mutex < 0) return -1;
    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, grain, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return -1;
    int volume[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    if (sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volume) < 0) {
        return -1;
    }
    return 0;
}
void h2_audio_sink_lock(void) { sceKernelLockMutex(mutex, 1, NULL); }
void h2_audio_sink_unlock(void) { sceKernelUnlockMutex(mutex, 1); }

/* Called under progress_mutex. With only one submitted grain outstanding,
 * MAIN's one-grain rest-sample range is sufficient; never infer consumption
 * from wall-clock time or from the decoder's read-ahead position. */
static int observe_rest(void)
{
    if (!progress.pending) return 0;
    int remaining = sceAudioOutGetRestSample(port);
    uint32_t previous = progress.remaining;
    if (remaining < 0 || !h2_audio_progress_rest(&progress, (uint32_t)remaining))
        return remaining < 0 ? remaining : -1001;
#if H2_AUDIO_DSP
    if (fx_queued) {
        fx_consumed += previous - (uint32_t)remaining;
        for (unsigned v = 0; v < H2_FX_SOURCES; ++v)
            if (fx_queued & (1u << v)) fx_source_consumed[v] += previous - (uint32_t)remaining;
        if (!remaining) fx_queued = 0;
    }
    if (gp_pcm_queued) {
        for (unsigned i=0;i<2;++i) if (gp_pcm_queued & (1u<<i)) gp_pcm_consumed[i] += previous - (uint32_t)remaining;
        if (!remaining) gp_pcm_queued=0;
    }
#else
    (void)previous;
#endif
    if (progress.pending && sceKernelGetProcessTimeWide() - submitted_at > 1000000)
        return -1002; /* failed/stalled sink, never invented sample progress */
    return 0;
}
int h2_audio_backend_play(int voice, uint32_t bytes, uint32_t rate)
{
    if (h2_audio_backend_health() < 0 || progress_mutex < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    h2_audio_progress next = progress;
    int ok = h2_audio_backend_health() == 0 && h2_audio_progress_play(&next, voice, bytes, rate);
#if H2_AUDIO_DSP
    if (fx.engine) {
        int zero[4];for(unsigned i=0;i<4;++i)zero[i]=gp_streams[i].voice;
        uint8_t bins[32];h2_audio_bins_snapshot(bins);int unity=1;
        for(unsigned i=0;i<11;++i)if(bins[i])unity=0;
        ok=ok && bytes==106496 && rate==44100 && fx.playing==0x7fff && fx.filtered==3 && gp_pcm_active==3 &&
           unity && h2_audio_movie_contract(voice,gp_pcm_voice,zero,0);
    }
#endif
    if (ok) { xk_audio_voice_play(voice, 1); progress = next; }
    sceKernelUnlockMutex(progress_mutex, 1);
    return ok ? 0 : -1;
}
int h2_audio_backend_cursor(int voice, uint32_t *play, uint32_t *write)
{
    if (h2_audio_backend_health() < 0 || progress_mutex < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    if (voice < 0 || progress.voice != voice) {
        sceKernelUnlockMutex(progress_mutex, 1); return -1;
    }
    int rc = observe_rest();
    if (rc < 0) __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE);
    int ok = rc >= 0 && h2_audio_backend_health() == 0 &&
             h2_audio_progress_cursor(&progress, voice, play, write);
    sceKernelUnlockMutex(progress_mutex, 1);
    return ok ? 0 : -1;
}
int h2_audio_backend_repeat_play(int voice,uint32_t bytes,uint32_t rate)
{
    if(h2_audio_backend_health()<0 || progress_mutex<0)return -1;
    sceKernelLockMutex(progress_mutex,1,NULL);
    int ok=voice>=0 && voice==progress.voice && !progress.stopped && !progress.rewound &&
           bytes==progress.bytes && rate==44100 && progress.step==((44100u<<16)/XA_OUT_RATE);
    xk_audio_lock();if(ok && !xk_audio_voice_playing(voice))ok=0;xk_audio_unlock();
    /* Original active looping Play reasserts the same source/loop range; bit2
     * alone requests a current-offset reset. This supported flags1 retry
     * leaves the real decoder, queued grains and consumed position intact. */
    sceKernelUnlockMutex(progress_mutex,1);return ok?0:-1;
}

/* The sole queued grain cannot be cancelled by this sink. Stop the real mixer,
 * then observe its retained grain through completion before reporting stopped.
 * Holding progress_mutex prevents another grain from being submitted meanwhile. */
int h2_audio_backend_stop(int voice)
{
    if (h2_audio_backend_health() < 0 || voice < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int rc = -1;
    if (progress.voice == voice && h2_audio_backend_health() == 0) {
        xk_audio_voice_stop(voice);
        rc = 0;
        while (progress.pending) {
            rc = observe_rest();
            if (rc < 0) { __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE); break; }
            if (progress.pending) sceKernelDelayThread(1000);
        }
        if (rc >= 0 && !h2_audio_progress_stop(&progress, voice)) rc = -1;
    }
    sceKernelUnlockMutex(progress_mutex, 1); return rc < 0 ? -1 : 0;
}
int h2_audio_backend_status_voice(int voice, uint32_t *status)
{
    if (h2_audio_backend_health() < 0 || voice < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = progress.voice == voice && h2_audio_backend_health() == 0;
    if (ok) {
        xk_audio_lock(); int playing = xk_audio_voice_playing(voice); xk_audio_unlock();
        ok = !!playing == !progress.stopped;
        if (ok) *status = playing ? 5 : 0; /* playing + looping, or drained stop */
    }
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_rewind(int voice)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    h2_audio_progress next = progress;
    int ok = h2_audio_backend_health() == 0 && h2_audio_progress_rewind(&next, voice);
    if (ok) { xk_audio_voice_set_pos(voice, 0); progress = next; }
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_forget(int voice)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    h2_audio_progress next = progress;
    int ok = h2_audio_backend_health() == 0 && h2_audio_progress_forget(&next, voice);
    if (ok) {
        xk_audio_lock(); ok = !xk_audio_voice_playing(voice); xk_audio_unlock();
        if (ok) progress = next;
    }
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}

#if H2_AUDIO_DSP
static int fx_inputs_ready(void)
{
    uint8_t bins[32]; h2_audio_bins_snapshot(bins);
    for (unsigned i = 0; i < 6; ++i) if (bins[i]) return 0;
    int idle = 1; xk_audio_lock();
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) if (xk_audio_voice_playing(i)) { idle = 0; break; }
    xk_audio_unlock(); return idle && progress.voice < 0;
}
int h2_audio_backend_fx_bind(h2_dsp_engine *engine, unsigned bin)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && fx_inputs_ready() && h2_audio_fx_bind(&fx, engine, bin);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_bind_spatial(h2_dsp_engine *engine, unsigned bin, const int8_t taps[31])
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    uint8_t bins[32]; h2_audio_bins_snapshot(bins); int zero = 1;
    for (unsigned i = 0; i <= 10; ++i) if (bins[i]) zero = 0;
    int ok = h2_audio_backend_health() == 0 && zero && fx_inputs_ready() &&
        h2_audio_fx_bind_spatial(&fx, engine, bin, taps);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_route(unsigned bin, unsigned routes)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_route(&fx, bin, routes);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_route_mask(unsigned bin, unsigned output_mask)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_route_mask(&fx, bin, output_mask);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_filter(unsigned key)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_filter(&fx, key);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_mute(unsigned key)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_mute(&fx, key);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_forget(unsigned bin)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_forget(&fx, bin);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_play(unsigned bin)
{
    if (h2_audio_backend_health() < 0) return -1;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    unsigned mask = h2_audio_fx_mask(bin), index = mask ? (unsigned)__builtin_ctz(mask) : 0;
    uint64_t before = fx_source_submitted[index];
    int ok = h2_audio_backend_health() == 0 && fx_inputs_ready() && h2_audio_fx_play(&fx, bin);
    sceKernelUnlockMutex(progress_mutex, 1);
    if (!ok) return -1;
    uint64_t start = sceKernelGetProcessTimeWide();
    for (;;) {
        sceKernelLockMutex(progress_mutex, 1, NULL);
        /* An older already-computed grain may omit this newly started source.
         * Only a submission tagged with this source can complete its Play. */
        int accepted = fx_source_submitted[index] > before, healthy = h2_audio_backend_health() == 0;
        sceKernelUnlockMutex(progress_mutex, 1);
        if (!healthy) return -1;
        if (accepted) return 0;
        if (sceKernelGetProcessTimeWide() - start > 2000000) {
            __atomic_store_n(&error, (uint32_t)-1006, __ATOMIC_RELEASE); return -1;
        }
        sceKernelDelayThread(1000);
    }
}
int h2_audio_backend_gp_pcm_play(int voice)
{
    if (voice<0 || voice>=XA_MAX_VOICES || h2_audio_backend_health()<0) return -1;
    sceKernelLockMutex(progress_mutex,1,NULL);
    unsigned index=gp_pcm_voice[0]<0 ? 0 : 1;
    int ok=h2_audio_backend_health()==0 && fx.playing==0x7fff && fx.bound==0x7fff &&
        progress.voice<0 && gp_pcm_voice[index]<0 && gp_pcm_voice[0]!=voice &&
        (!index || gp_pcm_active==1);
    xk_audio_lock();
    if (xk_audio_voice_playing(voice)) ok=0;
    for (unsigned v=0;v<XA_MAX_VOICES;++v) if (xk_audio_voice_playing(v) &&
        (int)v!=gp_pcm_voice[0] && (int)v!=gp_pcm_voice[1]) ok=0;
    xk_audio_unlock();
    uint64_t before=gp_pcm_submitted[index];
    if (ok) {
        gp_pcm_voice[index]=voice; gp_pcm_active |= 1u<<index;
        xk_audio_voice_play(voice,1);
    }
    sceKernelUnlockMutex(progress_mutex,1);
    if (!ok) return -1;
    uint64_t start=sceKernelGetProcessTimeWide();
    for (;;) {
        sceKernelLockMutex(progress_mutex,1,NULL);
        int accepted=gp_pcm_submitted[index]>before, healthy=h2_audio_backend_health()==0;
        sceKernelUnlockMutex(progress_mutex,1);
        if (!healthy) return -1;
        if (accepted) return 0;
        if (sceKernelGetProcessTimeWide()-start>2000000) {
            __atomic_store_n(&error,(uint32_t)-1006,__ATOMIC_RELEASE); return -1;
        }
        sceKernelDelayThread(1000);
    }
}
/* Four original streams, each with two 320-byte zero PCM packets. The host validates their
 * descriptor and immutable mirror. No guest callback runs on this worker. */
int h2_audio_backend_stream_submit(int voice,uint32_t mirror,uint64_t *ticket)
{
    if (voice<0 || voice>=XA_MAX_VOICES || !mirror || !ticket || h2_audio_backend_health()<0) return -1;
    sceKernelLockMutex(progress_mutex,1,NULL);
    gp_stream_state *s=NULL;
    for(unsigned i=0;i<4;++i) if(gp_streams[i].voice==voice)s=&gp_streams[i];
    if(!s) for(unsigned i=0;i<4;++i) if(gp_streams[i].voice<0){s=&gp_streams[i];break;}
    h2_stream_cursor cursor;
    int ok=s && h2_audio_backend_health()==0 && fx.playing==0x7fff && gp_pcm_active==3 &&
        s->count<2 && gp_stream_serial<UINT64_MAX && s->end<=UINT64_MAX-160 &&
        h2_audio_stream_cursor_read(voice,&cursor);
    if (ok && xk_audio_stream_push(voice,mirror,320)<0) ok=0;
    if (ok) {
        s->voice=voice;s->end+=160;
        s->packets[s->count].ticket=++gp_stream_serial;
        s->packets[s->count].end_source=s->end;
        s->packets[s->count++].fence=0;*ticket=gp_stream_serial;
    }
    sceKernelUnlockMutex(progress_mutex,1);return ok ? 0 : -1;
}
int h2_audio_backend_stream_complete(int voice,uint64_t *ticket)
{
    if (!ticket || h2_audio_backend_health()<0) return -1;
    sceKernelLockMutex(progress_mutex,1,NULL);
    gp_stream_state *s=NULL;
    for(unsigned i=0;i<4;++i)if(gp_streams[i].voice==voice)s=&gp_streams[i];
    int result=s?0:-1;
    if (s && s->count && s->packets[0].fence && fx_consumed>=s->packets[0].fence) {
        *ticket=s->packets[0].ticket;s->packets[0]=s->packets[1];memset(&s->packets[1],0,sizeof s->packets[1]);
        --s->count;++gp_stream_completed;result=1;
    }
    sceKernelUnlockMutex(progress_mutex,1);return result;
}
static int stream_decoded(uint64_t fence)
{
    for(unsigned v=0;v<4;++v){
        gp_stream_state *s=&gp_streams[v];if(s->voice<0 || !s->count)continue;
        h2_stream_cursor cursor;if(!h2_audio_stream_cursor_read(s->voice,&cursor) ||
            s->removed>s->end || cursor.source_frames>s->end-s->removed)return 0;
        uint64_t source=s->removed+cursor.source_frames;
        for(unsigned i=0;i<s->count;++i) if(!s->packets[i].fence){
            uint64_t end=s->packets[i].end_source;
            /* Retire both interpolation lanes, or the final drained source,
             * then wait for the whole containing sink grain to be consumed. */
            if(!(source>=end && (source-end>=2 || cursor.drained)))break;
            if(!xk_audio_stream_pop_consumed(s->voice))return 0;
            s->removed+=160;s->packets[i].fence=fence;++gp_stream_decoded;
        }
    }
    return 1;
}
/* Called only under the ownership lock. These voices are already validated
 * mono8/1000Hz with volume-10000/route14, plus zero PCM stream sources into
 * bins27..30. Decode/resample normally, then prove every contribution is zero.
 * Never discard unexpected samples. The GP's zero inputs are exact for this
 * bounded state; nonzero stream samples are rejected by the guest adapter. */
static int mix_muted_gp_pcm(void)
{
    if (!gp_pcm_active) return 1;
    if(progress.voice>=0){
        int zero[4];for(unsigned i=0;i<4;++i)zero[i]=gp_streams[i].voice;
        if(!h2_audio_movie_contract(progress.voice,gp_pcm_voice,zero,!progress.stopped))return 0;
    }
    xk_audio_mix(gp_pcm_output,XA_GRAIN);
    if(progress.voice<0 || progress.stopped)
        for (unsigned i=0;i<XA_GRAIN*2;++i) if (gp_pcm_output[i]) return 0;
    int ok=1; xk_audio_lock();
    for (unsigned i=0;i<2;++i) if ((gp_pcm_active & (1u<<i)) && !xk_audio_voice_playing(gp_pcm_voice[i])) ok=0;
    xk_audio_unlock(); return ok;
}
int h2_audio_backend_effect_read(h2_dsp_engine *engine, unsigned index,
                                 unsigned offset, void *out, unsigned bytes)
{
    if (h2_audio_backend_health() < 0) return 0;
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int ok = h2_audio_backend_health() == 0 && (!fx.engine || fx.engine == engine) &&
             h2_dsp_read_effect(engine, index, offset, out, bytes);
    sceKernelUnlockMutex(progress_mutex, 1); return ok;
}
#endif

static int mix_worker(SceSize bytes, void *arg)
{
    (void)bytes; (void)arg;
    unsigned slot = 0, first = 1;
#if H2_AUDIO_DSP
    unsigned fx_prepared = 0, pcm_prepared = 0;
    int movie_prepared=-1;uint32_t movie_frontier=0;
#endif
    while (__atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        sceKernelLockMutex(progress_mutex, 1, NULL);
        if (__atomic_load_n(&error, __ATOMIC_ACQUIRE)) {
            sceKernelUnlockMutex(progress_mutex, 1); break;
        }
#if H2_AUDIO_DSP
        /* Compute into the other retained buffer while the preceding grain
         * plays. Never discard computed DSP time to catch up with wall time. */
        if (fx.playing && !fx_prepared) {
            uint64_t begin = sceKernelGetProcessTimeWide();
            if (!mix_muted_gp_pcm()) {
                __atomic_store_n(&error,(uint32_t)-1007,__ATOMIC_RELEASE);
                sceKernelUnlockMutex(progress_mutex,1); break;
            }
            movie_prepared=progress.stopped?-1:progress.voice;
            if(movie_prepared>=0){xk_audio_lock();movie_frontier=xk_audio_voice_pos(movie_prepared);xk_audio_unlock();}
            if (!stream_decoded(fx.frames*32+XA_GRAIN) ||
                !(movie_prepared>=0 ? h2_audio_fx_render_pcm(&fx,output[slot],XA_GRAIN,gp_pcm_output) :
                  h2_audio_fx_render(&fx, output[slot], XA_GRAIN))) {
                __atomic_store_n(&error, (uint32_t)-1004, __ATOMIC_RELEASE);
                sceKernelUnlockMutex(progress_mutex, 1); break;
            }
            uint64_t duration = sceKernelGetProcessTimeWide() - begin;
            fx_compute_us += duration;
            if (duration > fx_max_compute_us) fx_max_compute_us = duration;
            if (duration * XA_OUT_RATE > (uint64_t)XA_GRAIN * 1000000) ++fx_deadline_misses;
            fx_prepared = fx.playing; pcm_prepared = gp_pcm_active;
            /* This is an observed empty queue after computation, not a
             * hardware underrun interrupt or a measured duration of silence. */
            int had_fx = fx_queued;
            int rc = observe_rest();
            if (rc < 0) {
                __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE);
                sceKernelUnlockMutex(progress_mutex, 1); break;
            }
            if (had_fx && !progress.pending) ++fx_empty_after_compute;
        }
#endif
        int rc = observe_rest();
        if (rc < 0) {
            __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE);
            sceKernelUnlockMutex(progress_mutex, 1); break;
        }
        if (progress.pending) {
            sceKernelUnlockMutex(progress_mutex, 1);
            sceKernelDelayThread(1000); continue;
        }
#if H2_AUDIO_DSP
        if (!fx_prepared)
#endif
            xk_audio_mix(output[slot], XA_GRAIN);
        uint32_t decoded_position = 0;
        if (progress.voice >= 0 && !progress.stopped) {
            xk_audio_lock(); decoded_position = xk_audio_voice_pos(progress.voice); xk_audio_unlock();
        }
        rc = sceAudioOutOutput(port, output[slot]);
        if (rc >= 0 && !
#if H2_AUDIO_DSP
            (fx_prepared ? h2_audio_progress_submit_tagged(&progress,XA_GRAIN,movie_frontier,movie_prepared) :
             h2_audio_progress_submit(&progress,XA_GRAIN,decoded_position))
#else
            h2_audio_progress_submit(&progress,XA_GRAIN,decoded_position)
#endif
            ) rc=-1003;
#if H2_AUDIO_DSP
        if (rc >= 0 && fx_prepared) {
            fx_submitted += XA_GRAIN; fx_queued = fx_prepared;
            for (unsigned v = 0; v < H2_FX_SOURCES; ++v)
                if (fx_prepared & (1u << v)) fx_source_submitted[v] += XA_GRAIN;
            gp_pcm_queued=pcm_prepared;
            for (unsigned i=0;i<2;++i) if (pcm_prepared & (1u<<i)) gp_pcm_submitted[i]+=XA_GRAIN;
            fx_prepared = pcm_prepared = 0;
        }
#endif
        submitted_at = sceKernelGetProcessTimeWide();
        if (rc >= 0) {
            uint32_t maximum = 0;
            uint32_t channel_peak[2] = {0};
            for (unsigned i = 0; i < XA_GRAIN * 2; ++i) {
                int value = output[slot][i];
                uint32_t absolute = value < 0 ? (uint32_t)-value : (uint32_t)value;
                if (absolute > maximum) maximum = absolute;
                if (absolute > channel_peak[i & 1]) channel_peak[i & 1] = absolute;
            }
            __atomic_store_n(&last_peak_left, channel_peak[0], __ATOMIC_RELAXED);
            __atomic_store_n(&last_peak_right, channel_peak[1], __ATOMIC_RELAXED);
            __atomic_add_fetch(&grains, 1, __ATOMIC_RELAXED);
            if (maximum) __atomic_add_fetch(&nonzero_grains, 1, __ATOMIC_RELAXED);
            if (maximum > __atomic_load_n(&peak, __ATOMIC_RELAXED))
                __atomic_store_n(&peak, maximum, __ATOMIC_RELAXED);
        } else __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE);
#if H2_AUDIO_DSP
        unsigned has_fx = fx.playing; /* snapshot under the ownership lock */
#endif
        sceKernelUnlockMutex(progress_mutex, 1);
        if (first) { first = 0; sceKernelSignalSema(ready, 1); }
        if (rc < 0) break;
        slot ^= 1; /* Keep the last submitted grain immutable while mixing. */
#if H2_AUDIO_DSP
        /* Permit the original game/query thread to acquire the serialization
         * lock between expensive GP grains. No cursor advances during this. */
        if (has_fx) sceKernelDelayThread(1000);
#endif
    }
    /* SceAudioOutOutput(NULL) is documented as a drain, but the pinned
     * Vita3K implementation returns immediately. Explicitly observe the
     * outstanding grain before releasing this worker's retained storage. */
    for (;;) {
        sceKernelLockMutex(progress_mutex, 1, NULL);
        int rc = observe_rest(), pending = progress.pending;
        sceKernelUnlockMutex(progress_mutex, 1);
        if (rc < 0) { __atomic_store_n(&error, (uint32_t)rc, __ATOMIC_RELEASE); break; }
        if (!pending) break;
        sceKernelDelayThread(1000);
    }
    /* Complete the retained buffer before close can release port or storage. */
    if (!progress.pending) {
        int drained = sceAudioOutOutput(port, NULL);
        if (drained < 0) __atomic_store_n(&error, (uint32_t)drained, __ATOMIC_RELEASE);
    }
    return 0;
}
int h2_audio_backend_close(void)
{
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    if (worker >= 0 && started) {
        SceUInt timeout = 2000000;
        if (sceKernelWaitThreadEnd(worker, NULL, &timeout) < 0) return -1;
    }
    started = 0;
    /* An unverified drain retains the port and storage. A later close can
     * retry observation, but must never free resources still owned by a sink. */
    if (progress_mutex >= 0 && progress.pending && (observe_rest() < 0 || progress.pending)) return -1;
#if H2_AUDIO_DSP
    if (mutex>=0 && fx.engine && progress.voice>=0)xk_audio_voice_stop(progress.voice);
    if (mutex>=0) for (unsigned i=0;i<2;++i) if (gp_pcm_active & (1u<<i)) xk_audio_voice_stop(gp_pcm_voice[i]);
    if (mutex>=0) for(unsigned i=0;i<4;++i) if(gp_streams[i].voice>=0)xk_audio_voice_stop(gp_streams[i].voice);
    gp_pcm_active=0;
#endif
    int result = 0;
    if (worker >= 0) { if (sceKernelDeleteThread(worker) < 0) result = -1; else worker = -1; }
    if (ready >= 0) { if (sceKernelDeleteSema(ready) < 0) result = -1; else ready = -1; }
    if (port >= 0) { if (sceAudioOutReleasePort(port) < 0) result = -1; else port = -1; }
    if (mutex >= 0) { if (sceKernelDeleteMutex(mutex) < 0) result = -1; else mutex = -1; }
    if (progress_mutex >= 0) { if (sceKernelDeleteMutex(progress_mutex) < 0) result = -1; else progress_mutex = -1; }
    return result;
}
static int open_failed(void) { return h2_audio_backend_close() < 0 ? -2 : -1; }
int h2_audio_backend_open(void)
{
    if (port >= 0 || worker >= 0 || mutex >= 0 || ready >= 0 || progress_mutex >= 0) return -2;
    grains = nonzero_grains = peak = error = 0;
#if H2_AUDIO_DSP
    memset(gp_streams,0,sizeof gp_streams);for(unsigned i=0;i<4;++i)gp_streams[i].voice=-1;
    gp_stream_decoded=gp_stream_completed=0;gp_stream_serial=0;
    gp_pcm_voice[0]=gp_pcm_voice[1]=-1; gp_pcm_active=gp_pcm_queued=0;
    memset(gp_pcm_submitted,0,sizeof gp_pcm_submitted); memset(gp_pcm_consumed,0,sizeof gp_pcm_consumed);
    fx = (h2_audio_fx){0}; fx_submitted = fx_consumed = fx_compute_us = fx_max_compute_us = 0;
    fx_queued = fx_deadline_misses = fx_empty_after_compute = 0;
    memset(fx_source_submitted, 0, sizeof fx_source_submitted);
    memset(fx_source_consumed, 0, sizeof fx_source_consumed);
#endif
    last_peak_left = last_peak_right = 0;
    memset(output, 0, sizeof output);
    if (xk_audio_init() < 0) return open_failed();
    progress_mutex = sceKernelCreateMutex("h2_audio_progress", 0, 0, NULL);
    if (progress_mutex < 0) return open_failed();
    h2_audio_progress_reset(&progress); submitted_at = 0;
    h2_audio_bins_reset();
    ready = sceKernelCreateSema("h2_audio_ready", 0, 0, 1, NULL);
    if (ready < 0) return open_failed();
    worker = sceKernelCreateThread("h2_audio_output", mix_worker, 80, 64 * 1024, 0,
                                   SCE_KERNEL_CPU_MASK_USER_ALL, NULL);
    if (worker < 0) return open_failed();
    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
    if (sceKernelStartThread(worker, 0, NULL) < 0) return open_failed();
    started = 1;
    SceUInt timeout = 2000000;
    if (sceKernelWaitSema(ready, 1, &timeout) < 0 || __atomic_load_n(&error, __ATOMIC_ACQUIRE)) {
        return open_failed();
    }
    xv_logf("[h2/audio] Vita sink accepted first grain: port=%d thread=%d rate=%d stereo grain=%d\n",
            port, worker, XA_OUT_RATE, XA_GRAIN);
    return 0;
}
int h2_audio_backend_health(void)
{ return port >= 0 && started && !__atomic_load_n(&error, __ATOMIC_ACQUIRE) ? 0 : -1; }
uint32_t h2_audio_backend_free_voices(void) { return xk_audio_free_voices(); }
int h2_audio_backend_set_headroom(uint32_t bin, uint32_t amount)
{
    if (h2_audio_backend_health() < 0) return -1;
#if H2_AUDIO_DSP
    sceKernelLockMutex(progress_mutex, 1, NULL);
    int result = h2_audio_backend_health() < 0 || (fx.engine && bin < ((fx.bound & 0x54) ? 11u : 6u) && amount)
        ? -1 : h2_audio_bins_set(bin, amount);
    sceKernelUnlockMutex(progress_mutex, 1); return result;
#else
    return h2_audio_bins_set(bin, amount);
#endif
}
void h2_audio_backend_snapshot(h2_audio_backend_status *out)
{
#if H2_AUDIO_DSP
    if (progress_mutex >= 0) sceKernelLockMutex(progress_mutex, 1, NULL);
#endif
    *out = (h2_audio_backend_status){
        .grains = __atomic_load_n(&grains, __ATOMIC_RELAXED),
        .nonzero_grains = __atomic_load_n(&nonzero_grains, __ATOMIC_RELAXED),
        .peak = __atomic_load_n(&peak, __ATOMIC_RELAXED),
        .error = __atomic_load_n(&error, __ATOMIC_ACQUIRE), .port = port, .thread = worker,
        .last_peak_left = __atomic_load_n(&last_peak_left, __ATOMIC_RELAXED),
        .last_peak_right = __atomic_load_n(&last_peak_right, __ATOMIC_RELAXED)
    };
#if H2_AUDIO_DSP
    out->fx_computed_frames = fx.frames * 32;
    out->fx_submitted_frames = fx_submitted; out->fx_consumed_frames = fx_consumed;
    out->fx_compute_us = fx_compute_us; out->fx_max_compute_us = fx_max_compute_us;
    out->fx_deadline_misses = fx_deadline_misses; out->fx_empty_after_compute = fx_empty_after_compute;
    out->fx_bound_mask = fx.bound; out->fx_playing_mask = fx.playing;
    memcpy(out->fx_source_submitted, fx_source_submitted, sizeof fx_source_submitted);
    memcpy(out->fx_source_consumed, fx_source_consumed, sizeof fx_source_consumed);
    out->gp_stream_packets=0;for(unsigned i=0;i<4;++i)out->gp_stream_packets+=gp_streams[i].count;
    out->gp_stream_decoded=gp_stream_decoded;out->gp_stream_completed=gp_stream_completed;
    out->gp_pcm_playing_mask=gp_pcm_active;
    out->movie_voice=progress.voice;
    out->movie_submitted_frames=progress.completed_frames+(progress.voice>=0 && progress.pending && progress.queued_voice==progress.voice?progress.queued_frames:0);
    out->movie_consumed_frames=progress.completed_frames+(progress.voice>=0 && progress.pending && progress.queued_voice==progress.voice?progress.queued_frames-progress.remaining:0);
    memcpy(out->gp_pcm_submitted,gp_pcm_submitted,sizeof gp_pcm_submitted);
    memcpy(out->gp_pcm_consumed,gp_pcm_consumed,sizeof gp_pcm_consumed);
    if (progress_mutex >= 0) sceKernelUnlockMutex(progress_mutex, 1);
#endif
}
