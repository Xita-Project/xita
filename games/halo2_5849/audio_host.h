#pragma once
#include "recomp/xv_x86rt.h"

typedef struct {
    uint32_t base, references, ever_created;
    uint32_t distance, rolloff, doppler;
    uint32_t pending_distance, pending_rolloff, pending_doppler, dirty;
    uint8_t headroom[32];
} h2_audio_device_snapshot;

typedef struct {
    uint32_t grains, nonzero_grains, peak, error;
    uint32_t last_peak_left, last_peak_right;
    int32_t port, thread;
} h2_audio_backend_status;

void h2_audio_host_call(xctx *c, uint32_t entry);
/* Original DSOUND constructors can run before the first host device. Once a
 * host device exists, unknown original DSOUND methods must never read its
 * deliberately incomplete XDK object representation. Only the fingerprinted
 * LightHRTF4Channel configuration wrapper/helper/writer may execute afterward,
 * with checked callers and memory; this does not implement HRTF processing. */
void h2_audio_guest_entry(xctx *c, uint32_t entry);
void h2_audio_host_snapshot(h2_audio_device_snapshot *out);

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
void h2_audio_backend_snapshot(h2_audio_backend_status *out);
_Noreturn void h2_audio_stop(xctx *c, uint32_t entry, const char *reason, uint32_t value);
