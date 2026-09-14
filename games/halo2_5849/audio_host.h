#pragma once
#include "recomp/xv_x86rt.h"

typedef struct {
    uint32_t base, references, ever_created, children;
    uint32_t distance, rolloff, doppler;
    uint32_t pending_distance, pending_rolloff, pending_doppler, dirty;
    /* Valid only when dirty bit 1/4 is set. Spatial commit is unsupported. */
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
int h2_audio_backend_cursor(int voice, uint32_t *play, uint32_t *write);
int h2_audio_backend_stop(int voice);
int h2_audio_backend_status_voice(int voice, uint32_t *status);
int h2_audio_backend_rewind(int voice);
int h2_audio_backend_forget(int voice);
void h2_audio_backend_snapshot(h2_audio_backend_status *out);
#if H2_AUDIO_DSP
#include "dsp_engine.h"
/* Single verified FXIN2/bin13 source; no coexistence with playing PCM/stream
 * voices. Play waits for an actual complete GP grain accepted by the sink.
 * Engine lifetime remains with the device, which joins the worker first. */
int h2_audio_backend_fx_bind(h2_dsp_engine *engine, unsigned bin);
int h2_audio_backend_fx_route(unsigned routes);
int h2_audio_backend_fx_play(void);
int h2_audio_backend_fx_forget(void);
int h2_audio_backend_effect_read(h2_dsp_engine *engine, unsigned index,
                                 unsigned offset, void *out, unsigned bytes);
#endif
_Noreturn void h2_audio_stop(xctx *c, uint32_t entry, const char *reason, uint32_t value);
