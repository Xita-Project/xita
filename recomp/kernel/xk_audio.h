/* xk_audio.h - software mixer behind the DirectSound HLE.
 *
 * Voices read their sample data straight out of guest memory (X_G), decode Xbox ADPCM (IMA, 64 samples
 * per 36-byte channel block) or PCM, resample to the output rate and mix into one stereo int16 stream
 * that the OS layer plays (Vita: sceAudioOut on its own thread; host: paced sleep + optional WAV dump).
 *
 * Two voice kinds mirror DirectSound:
 *   buffer  - one region of memory, Play/Stop/loop region/current position (sound effects)
 *   stream  - a FIFO of XMEDIAPACKETs; a packet is "consumed" once the mixer has read all of it, and
 *             the DirectSound layer completes it (status, callback) from the game thread
 * The game thread owns voice state changes; the mixer thread takes xk_audio_lock() while it reads. */
#pragma once
#include <stdint.h>

#define XA_OUT_RATE     48000         /* the Vita MAIN port only takes 48 kHz */
#define XA_GRAIN        1024          /* frames per mix step (~23 ms) */
#define XA_MAX_VOICES   192
#define XA_MAX_PKTS     8

typedef struct {
    uint32_t guest, size;             /* packet data (guest address) */
    int      consumed;                /* set by the mixer when fully read */
} xa_pkt;

typedef struct {
    int      used, kind;              /* kind: 0 free, 1 buffer, 2 stream */
    /* format */
    int      adpcm, channels, rate, bits;
    uint32_t block_align;
    /* playback */
    int      playing, looping;
    float    volume;                  /* linear 0..1 */
    int      freq_override;           /* SetFrequency, 0 = native */
    /* buffer voice */
    uint32_t data, size;              /* guest bytes */
    uint32_t loop_start, loop_len;    /* SetLoopRegion (bytes), 0 = whole buffer */
    uint32_t pos;                     /* byte cursor (block-aligned for ADPCM) */
    /* stream voice */
    xa_pkt   q[XA_MAX_PKTS]; int nq, qhead, rd;    /* rd: mixer read index (offset from qhead), <= nq */
    uint32_t pkt_pos;                 /* byte cursor inside the packet being read */
    uint8_t  carry[128]; uint32_t ncarry;   /* partial ADPCM block spanning a packet boundary */
    /* decode state: one decoded block/chunk of frames */
    int16_t  blk[64 * 2]; int blk_frames, blk_i;
    uint32_t frac;                    /* 16.16 resample position inside the current decoded frame */
    int16_t  last[2], prev[2];        /* current and previous source frame (linear interpolation) */
    uint64_t frames_out;              /* output frames produced since Play (for positions) */
} xa_voice;

int   xk_audio_init(void);                        /* opens the OS sink + mixer thread; 0 on success, <0 = no audio (mixer still runs silently) */
int   xk_audio_available(void);
void  xk_audio_lock(void);
void  xk_audio_unlock(void);

/* voices (game thread) */
int   xk_audio_voice_new(int kind, uint32_t wfx_guest);      /* parse WAVEFORMATEX at wfx_guest (0 = 16-bit stereo 48 kHz) */
void  xk_audio_voice_free(int v);
void  xk_audio_voice_set_format(int v, uint32_t wfx_guest);
void  xk_audio_voice_set_data(int v, uint32_t guest, uint32_t size);
void  xk_audio_voice_play(int v, int looping);
void  xk_audio_voice_stop(int v);
void  xk_audio_voice_set_pos(int v, uint32_t byte_pos);
uint32_t xk_audio_voice_pos(int v);                          /* current byte cursor (buffer voices) */
int   xk_audio_voice_playing(int v);
void  xk_audio_voice_set_volume_db100(int v, int32_t db100); /* DirectSound: hundredths of dB, -10000..0 */
void  xk_audio_voice_set_frequency(int v, uint32_t hz);      /* 0 = native */
void  xk_audio_voice_set_loop(int v, uint32_t start, uint32_t len);
int   xk_audio_stream_push(int v, uint32_t guest, uint32_t size);   /* 0 ok, -1 full */
int   xk_audio_stream_pop_consumed(int v);                          /* 1 if a consumed packet was popped */
void  xk_audio_stream_flush(int v);

/* mixer core (called by the OS sink thread) */
void  xk_audio_mix(int16_t *out, int frames);

/* OS sink (xk_os_vita.c / xk_os_host.c) */
int   xk_os_audio_open(int rate, int grain);                 /* <0 if no device */
void  xk_os_audio_write(const int16_t *stereo, int frames);  /* blocks until accepted */
int   xk_os_audio_thread_start(void (*fn)(void *), void *arg);
void  xk_os_audio_mutex_lock(void);
void  xk_os_audio_mutex_unlock(void);
