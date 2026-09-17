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
/* Bumped under progress_mutex on every change a stream completion query could observe. */
static uint32_t stream_generation;
static uint64_t gp_stream_serial;
/* Serialized retained GP grain ownership; separate from submitted progress. */
static int movie_prepared=-1,movie_draining;
static int gp_pcm_voice[2] = {-1,-1};
/* Game stream voices with packets queued: their real samples ride the PCM lane. */
static uint8_t game_voice[XA_MAX_VOICES]; static unsigned game_voices;
static unsigned gp_pcm_active, gp_pcm_queued;
static uint64_t gp_pcm_submitted[2], gp_pcm_consumed[2];
static _Alignas(64) int16_t gp_pcm_output[1024 * 2];
static uint64_t fx_submitted, fx_consumed, fx_compute_us, fx_max_compute_us;
static uint32_t fx_queued, fx_deadline_misses, fx_empty_after_compute;
static uint64_t fx_source_submitted[H2_FX_SOURCES], fx_source_consumed[H2_FX_SOURCES];
static unsigned reverb_pending, reverb_index;
static uint64_t reverb_queued_frame;
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
/* Diagnostic wall-clock accounting only (no behavioural effect): how long the game's
 * threads wait for progress_mutex and how long the worker holds it per iteration. */
static uint64_t perf_guest_wait_us, perf_guest_locks, perf_worker_hold_us, perf_worker_hold_max,
                perf_worker_compute_iters, perf_worker_idle_iters;
#ifdef H2_AUDIO_PLATFORM_TEST
#define perf_clock() ((uint64_t)0)   /* the host tests' fake clock advances per call: never perturb it */
#else
#define perf_clock() sceKernelGetProcessTimeWide()
#endif
static uint32_t perf_site_line[32], perf_site_n[32];   /* which callers lock, by source line */
static void lock_progress_at(unsigned line)
{
    uint64_t t0 = perf_clock();
    sceKernelLockMutex(progress_mutex, 1, NULL);
    perf_guest_wait_us += perf_clock() - t0; ++perf_guest_locks;   /* under the mutex */
    for (unsigned i = 0; i < 32; ++i) {
        if (perf_site_line[i] == line) { ++perf_site_n[i]; break; }
        if (!perf_site_line[i]) { perf_site_line[i] = line; perf_site_n[i] = 1; break; }
    }
}
#define lock_progress() lock_progress_at(__LINE__)
#if H2_AUDIO_DSP
/* The worker renders a GP grain in 32-frame steps and yields progress_mutex between
 * them (the original GP runs asynchronously; the CPU never waits a whole grain for
 * it). Calls that change what a grain contains - the FX source set, the filter
 * configuration the PCM lane is validated against, the GP PCM lanes - and the source
 * parameters and effect state the GP reads (routes, mutes, attenuation, effect words)
 * take effect at a grain boundary exactly as before: they wait for the step loop to
 * finish, so every submitted grain is computed from one parameter set. Reads, packet
 * queueing/completion and the movie voice controls run between GP frames. */
static volatile unsigned grain_active;
static void lock_progress_boundary_at(unsigned line)
{
    for (;;) {
        lock_progress_at(line);
        if (!grain_active) return;
        sceKernelUnlockMutex(progress_mutex, 1);
        sceKernelDelayThread(1000);
    }
}
#define lock_progress_boundary() lock_progress_boundary_at(__LINE__)
#endif
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
#if H2_AUDIO_DSP && !defined(H2_AUDIO_PLATFORM_TEST)
    { extern void h2_audio_fx_set_clock(uint64_t (*clock)(void)); h2_audio_fx_set_clock(sceKernelGetProcessTimeWide); }
#endif
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
        if (previous != (uint32_t)remaining) __atomic_add_fetch(&stream_generation, 1, __ATOMIC_RELEASE);
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
    lock_progress();
    h2_audio_progress next = progress;
    int ok = h2_audio_backend_health() == 0 && h2_audio_progress_play(&next, voice, bytes, rate);
#if H2_AUDIO_DSP
    if (fx.engine) {
        int zero[4];for(unsigned i=0;i<4;++i)zero[i]=gp_streams[i].voice;
        uint8_t bins[32];h2_audio_bins_snapshot(bins);int unity=1;
        for(unsigned i=0;i<11;++i)if(bins[i])unity=0;
        ok=ok && bytes==106496 && rate==44100 && fx.playing==0x7fff && fx.filtered==3 && gp_pcm_active==3 &&
           unity && h2_audio_movie_contract(voice,gp_pcm_voice,zero,0,game_voice);
    }
#endif
    if (ok) { xk_audio_voice_play(voice, 1); progress = next; }
    sceKernelUnlockMutex(progress_mutex, 1);
    return ok ? 0 : -1;
}
int h2_audio_backend_cursor(int voice, uint32_t *play, uint32_t *write)
{
    if (h2_audio_backend_health() < 0 || progress_mutex < 0) return -1;
    lock_progress();
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
    lock_progress();
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
    lock_progress();
#if H2_AUDIO_DSP
    if(fx.engine){
        if(progress.voice!=voice || movie_draining){sceKernelUnlockMutex(progress_mutex,1);return -1;}
        movie_draining=1;xk_audio_voice_stop(voice);
        sceKernelUnlockMutex(progress_mutex,1);
        uint64_t began=sceKernelGetProcessTimeWide();
        /* The worker must submit its already-computed movie grain. Let it
         * run while waiting; new computations exclude the stopped voice. */
        for(;;){
            lock_progress();
            int rc=observe_rest();
            if(rc>=0 && sceKernelGetProcessTimeWide()-began>2000000)rc=-1008;
            if(rc<0)__atomic_store_n(&error,(uint32_t)rc,__ATOMIC_RELEASE);
            if(rc<0 || h2_audio_backend_health()<0){sceKernelUnlockMutex(progress_mutex,1);return -1;}
            if(movie_prepared!=voice && (!progress.pending || progress.queued_voice!=voice)){
                int ok=h2_audio_progress_stop(&progress,voice);if(ok)movie_draining=0;
                sceKernelUnlockMutex(progress_mutex,1);return ok?0:-1;
            }
            sceKernelUnlockMutex(progress_mutex,1);sceKernelDelayThread(1000);
        }
    }
#endif
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
    lock_progress();
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
    lock_progress();
    h2_audio_progress next = progress;
    int ok = h2_audio_backend_health() == 0 && h2_audio_progress_rewind(&next, voice);
    if (ok) { xk_audio_voice_set_pos(voice, 0); progress = next; }
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_forget(int voice)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress();
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
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && fx_inputs_ready() && h2_audio_fx_bind(&fx, engine, bin);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_bind_spatial(h2_dsp_engine *engine, unsigned bin, const int8_t taps[31])
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    uint8_t bins[32]; h2_audio_bins_snapshot(bins); int zero = 1;
    for (unsigned i = 0; i <= 10; ++i) if (bins[i]) zero = 0;
    int ok = h2_audio_backend_health() == 0 && zero && fx_inputs_ready() &&
        h2_audio_fx_bind_spatial(&fx, engine, bin, taps);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_route(unsigned bin, unsigned routes)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_route(&fx, bin, routes);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_route_mask(unsigned bin, unsigned output_mask)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_route_mask(&fx, bin, output_mask);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_filter(unsigned key)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_filter(&fx, key);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_mute(unsigned key)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_mute(&fx, key);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_attenuate(unsigned key, unsigned attenuation)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_attenuate(&fx, key, attenuation);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_forget(unsigned bin)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && h2_audio_fx_forget(&fx, bin);
    sceKernelUnlockMutex(progress_mutex, 1); return ok ? 0 : -1;
}
int h2_audio_backend_fx_play(unsigned bin)
{
    if (h2_audio_backend_health() < 0) return -1;
    lock_progress_boundary();
    unsigned mask = h2_audio_fx_mask(bin), index = mask ? (unsigned)__builtin_ctz(mask) : 0;
    uint64_t before = fx_source_submitted[index];
    int ok = h2_audio_backend_health() == 0 && fx_inputs_ready() && h2_audio_fx_play(&fx, bin);
    sceKernelUnlockMutex(progress_mutex, 1);
    if (!ok) return -1;
    uint64_t start = sceKernelGetProcessTimeWide();
    for (;;) {
        lock_progress();
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
    lock_progress_boundary();
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
        lock_progress();
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
    lock_progress();
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
        __atomic_add_fetch(&stream_generation, 1, __ATOMIC_RELEASE);
    }
    sceKernelUnlockMutex(progress_mutex,1);return ok ? 0 : -1;
}
int h2_audio_backend_stream_complete(int voice,uint64_t *ticket)
{
    if (!ticket || h2_audio_backend_health()<0) return -1;
    lock_progress();
    gp_stream_state *s=NULL;
    for(unsigned i=0;i<4;++i)if(gp_streams[i].voice==voice)s=&gp_streams[i];
    int result=s?0:-1;
    if (s && s->count && s->packets[0].fence && fx_consumed>=s->packets[0].fence) {
        *ticket=s->packets[0].ticket;s->packets[0]=s->packets[1];memset(&s->packets[1],0,sizeof s->packets[1]);
        --s->count;++gp_stream_completed;result=1;
        __atomic_add_fetch(&stream_generation, 1, __ATOMIC_RELEASE);
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
            __atomic_add_fetch(&stream_generation, 1, __ATOMIC_RELEASE);
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
        if(!h2_audio_movie_contract(progress.voice,gp_pcm_voice,zero,!progress.stopped && !movie_draining,game_voice))return 0;
    } else {
        /* No movie: only the muted GP PCM voices, the GP stream voices and the
         * registered game stream voices may be playing. */
        for (unsigned i=0;i<XA_MAX_VOICES;++i) if (xk_audio_voice_playing((int)i) && !game_voice[i]) {
            int known=0;
            for (unsigned k=0;k<2;++k) if (gp_pcm_voice[k]==(int)i) known=1;
            for (unsigned k=0;k<4;++k) if (gp_streams[k].voice==(int)i) known=1;
            if (!known) return 0;
        }
    }
    xk_audio_mix(gp_pcm_output,XA_GRAIN);
    /* Without a movie or a registered game voice the lane must stay silent. */
    if((progress.voice<0 || progress.stopped || movie_draining) && !game_voices)
        for (unsigned i=0;i<XA_GRAIN*2;++i) if (gp_pcm_output[i]) return 0;
    int ok=1; xk_audio_lock();
    for (unsigned i=0;i<2;++i) if ((gp_pcm_active & (1u<<i)) && !xk_audio_voice_playing(gp_pcm_voice[i])) ok=0;
    xk_audio_unlock(); return ok;
}
int h2_audio_backend_effect_read(h2_dsp_engine *engine, unsigned index,
                                 unsigned offset, void *out, unsigned bytes)
{
    if (h2_audio_backend_health() < 0) return 0;
    lock_progress();
    int ok = h2_audio_backend_health() == 0 && (!fx.engine || fx.engine == engine) &&
             h2_dsp_read_effect(engine, index, offset, out, bytes);
    sceKernelUnlockMutex(progress_mutex, 1); return ok;
}
int h2_audio_backend_effect_write_pair(h2_dsp_engine *engine, unsigned index,
                                       unsigned offset, uint32_t first, uint32_t second)
{
    if (h2_audio_backend_health() < 0 || progress_mutex < 0) return 0;
    lock_progress_boundary();
    int ok = h2_audio_backend_health() == 0 && (!fx.engine || fx.engine == engine) &&
             h2_dsp_write_effect_pair(engine, index, offset, first, second);
    sceKernelUnlockMutex(progress_mutex, 1); return ok;
}
static int backend_queue_reverb(h2_dsp_engine *engine, unsigned index, uint32_t flags,
                                   const uint32_t parameters[66])
{
    if ((index != 8 && index != 9) || h2_audio_backend_health() < 0 || progress_mutex < 0) return 0;
    lock_progress();
    /* The previous monitor command is consumed by the GP within its next frames. The
     * whole-grain lock used to make a following queue wait for that implicitly; with
     * per-frame yields the wait is explicit and bounded, never a fabricated success. */
    uint64_t began = sceKernelGetProcessTimeWide();
    while (reverb_pending && fx.engine == engine && fx.playing && h2_audio_backend_health() == 0 &&
           sceKernelGetProcessTimeWide() - began < 200000) {
        sceKernelUnlockMutex(progress_mutex, 1); sceKernelDelayThread(1000); lock_progress();
    }
    int ok = h2_audio_backend_health() == 0 && fx.engine == engine && fx.playing && !reverb_pending &&
             (index==8 ? h2_dsp_queue_reverb8(engine,flags,parameters) : h2_dsp_queue_reverb9(engine,flags,parameters));
    if(ok){reverb_pending=1;reverb_index=index;reverb_queued_frame=fx.frames;}
    sceKernelUnlockMutex(progress_mutex, 1); return ok;
}
int h2_audio_backend_stream_voice_active(int voice, int active)
{
    if (voice < 0 || voice >= XA_MAX_VOICES || progress_mutex < 0) return -1;
    lock_progress();
    if (!!game_voice[voice] != !!active) { game_voice[voice] = !!active; if (active) ++game_voices; else --game_voices; }
    sceKernelUnlockMutex(progress_mutex, 1); return 0;
}
int h2_audio_backend_fixed_commit_ready(h2_dsp_engine *engine)
{
    if (h2_audio_backend_health()<0 || progress_mutex<0) return 0;
    lock_progress();
    int ok=h2_audio_backend_health()==0 && engine && fx.engine==engine && h2_audio_fx_fixed_commit_ready(&fx);
    if(ok){h2_dsp_status state;h2_dsp_snapshot(engine,&state);ok=!state.fault;}
    sceKernelUnlockMutex(progress_mutex,1);return ok;
}
int h2_audio_backend_queue_reverb8(h2_dsp_engine *engine,uint32_t flags,const uint32_t parameters[66])
{ return backend_queue_reverb(engine,8,flags,parameters); }
int h2_audio_backend_queue_reverb9(h2_dsp_engine *engine,uint32_t flags,const uint32_t parameters[66])
{ return backend_queue_reverb(engine,9,flags,parameters); }
#endif

static int mix_worker(SceSize bytes, void *arg)
{
    (void)bytes; (void)arg;
    unsigned slot = 0, first = 1;
#if H2_AUDIO_DSP
    unsigned fx_prepared = 0, pcm_prepared = 0;
    uint32_t movie_frontier=0;
#endif
    while (__atomic_load_n(&running, __ATOMIC_ACQUIRE)) {
        sceKernelLockMutex(progress_mutex, 1, NULL);
        uint64_t hold_t0 = perf_clock();
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
            movie_prepared=(progress.stopped || movie_draining)?-1:progress.voice;
            if(movie_prepared>=0){xk_audio_lock();movie_frontier=xk_audio_voice_pos(movie_prepared);xk_audio_unlock();}
            /* The PCM lane (movie voice and registered game stream voices, mixed by
             * the shared mixer at unity into FL/FR) joins the GP output once the fixed
             * FX configuration is complete. */
            int lane = (movie_prepared>=0 || game_voices) && fx.playing==0x7fff && fx.filtered==3;
            int rendered = stream_decoded(fx.frames*32+XA_GRAIN), aborted = 0, reverb_fault = 0;
            uint64_t yielded = 0;
            if (rendered) {
                grain_active = 1;
                for (unsigned at = 0; at < XA_GRAIN; at += 32) {
                    if (at) {   /* GP frame boundary: let the game's calls in */
                        uint64_t y0 = perf_clock(), held = y0 - hold_t0;
                        perf_worker_hold_us += held; if (held > perf_worker_hold_max) perf_worker_hold_max = held;
                        sceKernelUnlockMutex(progress_mutex, 1);
                        sceKernelLockMutex(progress_mutex, 1, NULL);
                        hold_t0 = perf_clock(); yielded += hold_t0 - y0;
                        if (__atomic_load_n(&error, __ATOMIC_ACQUIRE)) { aborted = 1; break; }
                    }
                    if (!(lane ? h2_audio_fx_render_pcm(&fx, output[slot] + at * 2, 32, gp_pcm_output + at * 2) :
                          h2_audio_fx_render(&fx, output[slot] + at * 2, 32))) { rendered = 0; break; }
                    /* A queued monitor command2 is consumed by the GP program within the
                     * following frames; the queue reopens as soon as the real effect state
                     * shows it consumed, and a command still pending 32 frames later is a
                     * fault, exactly the bound the whole-grain check enforced. */
                    if (reverb_pending && fx.frames > reverb_queued_frame) {
                        h2_dsp_status state; uint32_t flags = 0;
                        h2_dsp_snapshot(fx.engine, &state);
                        int consumed = !state.fault && !state.command &&
                                       h2_dsp_read_effect(fx.engine, reverb_index, 16, &flags, 4) && !(flags & 4);
                        if (state.fault || (!consumed && fx.frames >= reverb_queued_frame + 32)) { reverb_fault = 1; break; }
                        if (consumed) {
                            xv_logf("[h2/reverb-worker] original monitor command2 consumed effect%u flags=%08X real_GP_frames=%llu->%llu; grain not yet submitted\n",
                                    reverb_index, flags, (unsigned long long)reverb_queued_frame, (unsigned long long)fx.frames);
                            reverb_pending = 0;
                        }
                    }
                }
                grain_active = 0;
            }
            if (aborted) { sceKernelUnlockMutex(progress_mutex, 1); break; }
            if (reverb_fault) {
                __atomic_store_n(&error, (uint32_t)-1010, __ATOMIC_RELEASE);
                sceKernelUnlockMutex(progress_mutex, 1); break;
            }
            if (!rendered) {
                __atomic_store_n(&error, (uint32_t)-1004, __ATOMIC_RELEASE);
                sceKernelUnlockMutex(progress_mutex, 1); break;
            }
            uint64_t duration = sceKernelGetProcessTimeWide() - begin - yielded;
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
            perf_worker_hold_us += perf_clock() - hold_t0; ++perf_worker_idle_iters;
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
            fx_prepared = pcm_prepared = 0;movie_prepared=-1;
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
        {   uint64_t held = perf_clock() - hold_t0;
            perf_worker_hold_us += held; if (held > perf_worker_hold_max) perf_worker_hold_max = held;
            ++perf_worker_compute_iters; }
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
    movie_prepared=-1;movie_draining=0;
    gp_pcm_voice[0]=gp_pcm_voice[1]=-1; gp_pcm_active=gp_pcm_queued=0;
    memset(gp_pcm_submitted,0,sizeof gp_pcm_submitted); memset(gp_pcm_consumed,0,sizeof gp_pcm_consumed);
    fx = (h2_audio_fx){0}; fx_submitted = fx_consumed = fx_compute_us = fx_max_compute_us = 0;
    reverb_pending=reverb_index=0;reverb_queued_frame=0;
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

/* Cumulative diagnostic counters for the channel's per-60-flip [h2/perf] line. Read
 * without the mutex: approximate by design, never used for any decision. */
void h2_audio_backend_perf(uint64_t out[24])
{
    out[0] = grains; out[1] = nonzero_grains;
#if H2_AUDIO_DSP
    out[2] = fx_compute_us; out[3] = fx_max_compute_us; out[4] = fx_deadline_misses;
    { extern void h2_audio_fx_perf(uint64_t out[4]); h2_audio_fx_perf(out + 12); }
    if (fx.engine) { h2_dsp_status st; h2_dsp_snapshot(fx.engine, &st); out[15] = st.instructions; }
#else
    out[2] = out[3] = out[4] = 0; out[12] = out[13] = out[14] = out[15] = 0;
#endif
    out[5] = perf_worker_hold_us; out[6] = perf_worker_hold_max; out[7] = perf_worker_compute_iters;
    out[8] = perf_worker_idle_iters; out[9] = perf_guest_wait_us; out[10] = perf_guest_locks; out[11] = error;
    /* The four busiest lock sites (source line, count), highest first. */
    unsigned order[32]; for (unsigned i = 0; i < 32; ++i) order[i] = i;
    for (unsigned i = 0; i < 32; ++i) for (unsigned j = i + 1; j < 32; ++j)
        if (perf_site_n[order[j]] > perf_site_n[order[i]]) { unsigned t = order[i]; order[i] = order[j]; order[j] = t; }
    for (unsigned i = 0; i < 4; ++i) { out[16 + i * 2] = perf_site_line[order[i]]; out[17 + i * 2] = perf_site_n[order[i]]; }
}

#if H2_AUDIO_DSP
/* Lock-free change counter for the adapter's completion poll: equal values mean
 * every stream completion query would answer exactly as it did last time (an
 * error also changes the value so a failed sink is still observed promptly). */
uint32_t h2_audio_backend_stream_generation(void)
{
    return __atomic_load_n(&stream_generation, __ATOMIC_ACQUIRE) +
           (__atomic_load_n(&error, __ATOMIC_ACQUIRE) ? 0x80000000u : 0u);
}
#endif
