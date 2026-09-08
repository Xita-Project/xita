/* Host-only regression harness: real HLE query entry points, fake clock/memory.
 * cc -std=gnu11 -O1 -ffunction-sections -fdata-sections -Irecomp -Irecomp/kernel \
 *    tests/dsound_state_test.c -Wl,--gc-sections -lm -o /tmp/dsound_state_test
 */
#include <assert.h>
#include "../recomp/kernel/xd3d.c"
static uint8_t ram[4096];
static uint32_t pages[1];
uint8_t *g_xram = ram;
uint32_t *g_xpt = pages;
xk_thread *xk_cur;
static uint64_t now;
int xk_audio_available(void) { return 0; }
int xk_audio_voice_playing(int v) { (void)v; return 0; }
uint32_t xk_audio_voice_pos(int v) { (void)v; return 0; }
static int plays, stops;
void xk_audio_voice_play(int v, int loop) { (void)v; (void)loop; ++plays; }
void xk_audio_voice_stop(int v) { (void)v; ++stops; }
void xk_audio_voice_set_data(int v, uint32_t data, uint32_t size) { (void)v; (void)data; (void)size; }
void xk_audio_voice_set_pos(int v, uint32_t pos) { (void)v; (void)pos; }
void xk_audio_voice_set_frequency(int v, uint32_t hz) { (void)v; (void)hz; }
uint64_t xk_os_monotonic_us(void) { return now; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
static uint32_t query(uint32_t obj)
{
    xctx c = {0}; c.r[4] = 128; X_M32(132) = obj;
    xv_hle_DSoundVoiceIsPlaying(&c); assert(c.r[4] == 136);
    return c.r[0];
}
int main(void)
{
    ds_buffer *b = &g_ds_buffers[0]; b->obj = 256;
    ds_state *s = &b->state; ds_state_format(s, 0); s->stopped = 1; s->size = 192000;
    assert(!query(256)); now = 100; ds_state_play(s, now, 0);
    assert(s->end_us == 1000100 && query(256) == 1);
    X_M32(512 + 0x24) = 256; assert(query(512) == 1);
    now = 1000099; assert(query(256) == 1);
    now++; assert(!query(256)); assert(!query(0)); assert(!query(768));
    ds_state_play(s, now, 1); now += 100000000; assert(query(256) == 1);
    s->paused = 1; assert(!query(256)); s->paused = 0;
    xctx c = {0}; c.r[4] = 128; X_M32(132) = 512;
    xv_hle_DSoundVoiceStop(&c); assert(!query(256) && c.r[0] == 0 && c.r[4] == 136);
    ds_state_play(s, now, 0); assert(query(256) == 1);
    now += 250000; ds_state_stop(s, now); now += 9000000; ds_state_play(s, now, 0);
    assert(s->end_us - now == 750000);
    uint64_t old = ds_state_duration(s, s->size); s->frequency = 96000; ds_state_retime(s, now, old);
    assert(s->end_us - now == 375000);
    memset(s, 0, sizeof *s); s->tag = 0x69; s->align = 36; s->rate = 22050; s->size = 36 * 22050;
    assert(ds_state_duration(s, s->size) == 64000000);
    s->align = 72; s->size *= 2; assert(ds_state_duration(s, s->size) == 64000000);
    s->size = 0; ds_state_play(s, now, 0); assert(!query(256));
    ds_stream *stream = &g_ds_streams[0]; stream->obj = 1024;
    ds_state_format(&stream->state, 0); stream->state.size = 19200;
    ds_state_play(&stream->state, now, 0); stream->nq = 1; stream->q[0].report_due_us = now + 100000;
    assert(query(1024) == 1);
    stream->nq = 0; now += 99999; assert(query(1024) == 1); /* callback before final drain */
    stream->nq = 1; now++; assert(!query(1024)); /* stale callback cannot hang a busy poll */
    ds_state_play(&stream->state, now, 0); stream->q[0].report_due_us = now + 100000;
    c.r[4] = 128; X_M32(132) = 1024; xv_hle_DSoundVoiceStop(&c); assert(!query(1024));
    c.r[4] = 128; X_M32(132) = 256; X_M32(136) = 800;
    ds_state_play(s, now, 1); xv_hle_IDirectSoundBuffer_GetStatus(&c);
    assert(c.r[0] == 0 && X_M32(800) == 5 && c.r[4] == 140);
    /* Exercise actual mutators as well as pure timing: output calls stay on
     * their original surfaces; the pinned helper never stops the mixer. */
    memset(b, 0, sizeof *b); b->obj = 256; b->voice = -1;
    ds_state_format(&b->state, 0); b->state.stopped = 1;
    c.r[4] = 128; X_M32(132) = 256; X_M32(136) = 2048; X_M32(140) = 192000;
    xv_hle_IDirectSoundBuffer_SetBufferData(&c); assert(!query(256));
    c.r[4] = 128; X_M32(144) = 0; xv_hle_IDirectSoundBuffer_Play(&c);
    assert(query(256) == 1 && plays == 1);
    now += 250000;
    c.r[4] = 128; X_M32(136) = 96000; xv_hle_IDirectSoundBuffer_SetFrequency(&c);
    assert(b->state.end_us - now == 375000);
    c.r[4] = 128; X_M32(136) = 96000; xv_hle_IDirectSoundBuffer_SetCurrentPosition(&c);
    assert(b->state.end_us - now == 250000);
    c.r[4] = 128; xv_hle_DSoundVoiceStop(&c); assert(!query(256) && stops == 0);
    c.r[4] = 128; xv_hle_IDirectSoundBuffer_Play(&c); assert(query(256) == 1 && plays == 2);
    c.r[4] = 128; xv_hle_IDirectSoundBuffer_Stop(&c); assert(!query(256) && stops == 1);
    ds_state_play(&b->state, now, 0); now = b->state.end_us;
    c.r[4] = 128; xv_hle_IDirectSoundBuffer_Play(&c);
    assert(b->state.end_us - now == 500000); /* natural completion restarts from zero */
    puts("dsound state/BOOL/status tests passed");
}
