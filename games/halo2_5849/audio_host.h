#pragma once
#include "recomp/xv_x86rt.h"
#include "audio_fx_limits.h"

typedef struct {
    uint32_t base, references, ever_created, children;
    uint32_t distance, rolloff, doppler;
    uint32_t pending_distance, pending_rolloff, pending_doppler, dirty;
    /* Pending vectors remain stored after the admitted fixed-geometry commit.
     * Dynamic spatial commits remain unsupported. */
    uint32_t pending_position[3], pending_orientation[6];
    uint8_t headroom[32];
} h2_audio_device_snapshot;

typedef struct {
    uint32_t grains, nonzero_grains, peak, error;
    uint32_t last_peak_left, last_peak_right;
    int32_t port, thread;
    uint64_t fx_computed_frames, fx_submitted_frames, fx_consumed_frames;
    uint64_t fx_compute_us, fx_max_compute_us;
    uint32_t fx_deadline_misses, fx_empty_after_compute;
    uint32_t fx_bound_mask, fx_playing_mask;
    uint64_t fx_source_submitted[H2_FX_SOURCES], fx_source_consumed[H2_FX_SOURCES];
    uint32_t gp_stream_packets, gp_stream_decoded, gp_stream_completed;
    uint32_t gp_pcm_playing_mask;
    uint64_t gp_pcm_submitted[2], gp_pcm_consumed[2];
    int32_t movie_voice;
    uint64_t movie_submitted_frames,movie_consumed_frames;
} h2_audio_backend_status;

/* Bounded device + external PCM buffer creation/binding/controls and paired
 * write commits, exact FL/FR unity routing/gains and first looping Play with
 * sink-backed cursors, drained Stop and stopped rewind to zero. Other play modes,
 * running/arbitrary seek and surround are unsupported. */
void h2_audio_host_call(xctx *c, uint32_t entry);
/* Original DSOUND constructors can run before the first host device. Once a
 * host device exists, unknown original DSOUND methods must never read its
 * deliberately incomplete XDK object representation. Only the fingerprinted
 * LightHRTF4Channel configuration wrapper/helper/writer and compatible public
 * Release wrapper may execute afterward, with checked callers and memory;
 * this does not implement HRTF processing. */
void h2_audio_guest_entry(xctx *c, uint32_t entry);
void h2_audio_host_snapshot(h2_audio_device_snapshot *out);
void h2_audio_trace_buffer(xctx *c, uint32_t entry);

/* open returns only after the real sink accepts its first mixed grain.
 * Failed open (-1) rolls back all host resources; -2 means resources were
 * already live or cleanup failed, and must be terminal. close joins the worker
 * before releasing its sink/buffers. No guest memory is accessed by these
 * operations. */
int h2_audio_backend_open(void);
int h2_audio_backend_close(void);
int h2_audio_backend_health(void);
uint32_t h2_audio_backend_free_voices(void);
int h2_audio_backend_set_headroom(uint32_t bin, uint32_t headroom);
/* First Play of the sole supported looping PCM voice. Cursor observations
 * follow actual sink consumption and the independent mixer read frontier. */
int h2_audio_backend_play(int voice, uint32_t bytes, uint32_t rate);
int h2_audio_backend_repeat_play(int voice,uint32_t bytes,uint32_t rate);
int h2_audio_backend_cursor(int voice, uint32_t *play, uint32_t *write);
int h2_audio_backend_stop(int voice);
int h2_audio_backend_status_voice(int voice, uint32_t *status);
int h2_audio_backend_rewind(int voice);
int h2_audio_backend_forget(int voice);
void h2_audio_backend_snapshot(h2_audio_backend_status *out);
#if H2_AUDIO_DSP
#include "dsp_engine.h"
/* Verified FXIN2/bin13, paired bin23..25 and single-route15..22 sources; no playing PCM/stream
 * voices. Play waits for an actual complete GP grain accepted by the sink.
 * Engine lifetime remains with the device, which joins the worker first. */
int h2_audio_backend_fx_bind(h2_dsp_engine *engine, unsigned bin);
int h2_audio_backend_fx_bind_spatial(h2_dsp_engine *engine, unsigned bin, const int8_t taps[31]);
int h2_audio_backend_fx_route(unsigned bin, unsigned routes);
int h2_audio_backend_fx_route_mask(unsigned bin, unsigned output_mask);
int h2_audio_backend_fx_mute(unsigned key);
/* Active FX15..22 mix-bin attenuation (1/64 dB units below the FFF mute). */
int h2_audio_backend_fx_attenuate(unsigned key, unsigned attenuation);
int h2_audio_backend_fx_filter(unsigned key);
/* Validate the actual fixed FX configuration under its mixer mutex. No
 * processing, history reset, pending DSP update or grain mutation. */
int h2_audio_backend_fixed_commit_ready(h2_dsp_engine *engine);
/* Only the validated looping, fully muted mono8/1000Hz voices into GP14. */
int h2_audio_backend_gp_pcm_play(int voice);
int h2_audio_backend_stream_submit(int voice, uint32_t mirror, uint64_t *ticket);
int h2_audio_backend_stream_complete(int voice, uint64_t *ticket);
int h2_audio_backend_fx_play(unsigned bin);
int h2_audio_backend_fx_forget(unsigned bin);
int h2_audio_backend_effect_read(h2_dsp_engine *engine, unsigned index,
                                 unsigned offset, void *out, unsigned bytes);
int h2_audio_backend_effect_write_pair(h2_dsp_engine *engine, unsigned index,
                                       unsigned offset, uint32_t first, uint32_t second);
int h2_audio_backend_queue_reverb9(h2_dsp_engine *engine, uint32_t flags,
                                   const uint32_t parameters[66]);
int h2_audio_backend_queue_reverb8(h2_dsp_engine *engine, uint32_t flags,
                                   const uint32_t parameters[66]);
#endif
_Noreturn void h2_audio_stop(xctx *c, uint32_t entry, const char *reason, uint32_t value);
