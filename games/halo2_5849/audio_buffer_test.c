/* Synthetic guest ABI plus the actual shared PCM mixer. No owned game bytes.
 * A scripted sink supplies actual consumption observations for the Play ABI;
 * the worker/sink protocol has a separate concurrent platform test. */
#include "audio_host.c"
#include "recomp/kernel/xk_audio.c"
#include "audio_progress.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf stopped;
static unsigned locked, healthy, opens, closes, allocs, frees, lock_calls, unlock_calls;
static int allocation_failure;
static uint32_t native_fp = 0xA5A55A5A;
static uint32_t pool[256];
static h2_audio_progress test_progress;
uint32_t h2_platform_fpscr_read(void) { return native_fp; }
void h2_platform_fpscr_write(uint32_t value) { native_fp = value; }
void xv_logf(const char *format, ...) { (void)format; native_fp ^= 0x12345678; }
void xk_os_log(const char *format, ...) { (void)format; }
_Noreturn void h2_audio_stop(xctx *c, uint32_t ip, const char *reason, uint32_t value)
{ (void)c; (void)ip; (void)reason; (void)value; longjmp(stopped, 1); }
uint32_t xk_mem_arena_size(void) { return 0x200000; }
uint32_t xk_mem_alloc(uint32_t bytes, uint32_t align, uint32_t low, uint32_t high, int down)
{
    assert(bytes && !(bytes & 4095) && align == 4096 && !low && !high && !down);
    ++allocs; if (allocation_failure) return 0;
    unsigned count = bytes >> 12;
    for (unsigned i = 0; i + count <= 256; ++i) {
        unsigned j = 0; while (j < count && !pool[i + j]) ++j;
        if (j < count) continue;
        pool[i] = count;
        for (j = 0; j < count; ++j) {
            if (j) pool[i + j] = UINT32_MAX;
            g_xpt[0x100 + i + j] = 0x10000 + (i + j) * 4096;
        }
        return (0x100 + i) << 12;
    }
    return 0;
}
int xk_mem_free(uint32_t base)
{
    assert(!(base & 4095) && base >= 0x100000 && base < 0x200000);
    unsigned i = (base - 0x100000) >> 12, count = pool[i];
    assert(count && count != UINT32_MAX && !locked);
    /* Every real mixer reference must be gone before freeing guest backing. */
    for (unsigned v = 0; v < XA_MAX_VOICES; ++v)
        assert(!g_v[v].used || g_v[v].data != base);
    for (unsigned j = 0; j < count; ++j) { pool[i + j] = 0; g_xpt[0x100 + i + j] = 0x1FF000; }
    ++frees; return 0;
}
uint32_t xk_mem_size(uint32_t base) { return pool[(base - 0x100000) >> 12] * 4096; }
int xk_os_audio_open(int rate, int grain) { assert(rate == XA_OUT_RATE && grain == XA_GRAIN); return 0; }
void xk_os_audio_mutex_lock(void) { assert(!locked && healthy); locked = 1; ++lock_calls; }
void xk_os_audio_mutex_unlock(void) { assert(locked); locked = 0; ++unlock_calls; }
uint64_t xk_os_monotonic_us(void) { return 42; }
int h2_audio_backend_open(void) { assert(!healthy); h2_audio_progress_reset(&test_progress); healthy = 1; ++opens; return xk_audio_init(); }
int h2_audio_backend_close(void) { assert(healthy && xk_audio_free_voices() == XA_MAX_VOICES); healthy = 0; ++closes; return 0; }
int h2_audio_backend_health(void) { return healthy ? 0 : -1; }
uint32_t h2_audio_backend_free_voices(void) { return xk_audio_free_voices(); }
int h2_audio_backend_set_headroom(uint32_t bin, uint32_t amount) { (void)bin; (void)amount; assert(0); return -1; }
int h2_audio_backend_play(int voice, uint32_t bytes, uint32_t rate)
{
    h2_audio_progress next = test_progress;
    if (!healthy || !h2_audio_progress_play(&next, voice, bytes, rate)) return -1;
    xk_audio_voice_play(voice, 1); test_progress = next; return 0;
}
int h2_audio_backend_repeat_play(int voice,uint32_t bytes,uint32_t rate)
{return healthy && test_progress.voice==voice && test_progress.bytes==bytes && rate==44100 && !test_progress.stopped && xk_audio_voice_playing(voice)?0:-1;}
int h2_audio_backend_cursor(int voice, uint32_t *play, uint32_t *write)
{ return healthy && h2_audio_progress_cursor(&test_progress, voice, play, write) ? 0 : -1; }
int h2_audio_backend_stop(int voice)
{
    if (!healthy || test_progress.voice != voice) return -1;
    xk_audio_voice_stop(voice);
    if (test_progress.pending) assert(h2_audio_progress_rest(&test_progress, 0));
    return h2_audio_progress_stop(&test_progress, voice) ? 0 : -1;
}
int h2_audio_backend_status_voice(int voice, uint32_t *status)
{
    if (!healthy || test_progress.voice != voice) return -1;
    *status = xk_audio_voice_playing(voice) ? 5 : 0; return 0;
}
int h2_audio_backend_rewind(int voice)
{
    if (!healthy || !h2_audio_progress_rewind(&test_progress, voice)) return -1;
    xk_audio_voice_set_pos(voice, 0); return 0;
}
int h2_audio_backend_forget(int voice)
{ return healthy && h2_audio_progress_forget(&test_progress, voice) ? 0 : -1; }
static xctx context(uint32_t a, uint32_t b, uint32_t d, uint32_t e)
{
    xctx c; memset(&c, 0x5A, sizeof c); c.r[4] = 0x1FF0;
    X_M32(c.r[4]) = 0x3E3B97;
    X_M32(c.r[4] + 4) = a; X_M32(c.r[4] + 8) = b;
    X_M32(c.r[4] + 12) = d; X_M32(c.r[4] + 16) = e;
    return c;
}
static uint32_t read32(uint32_t address) { uint32_t x; x_guest_read(&x, address, 4); return x; }
static void call(xctx *c, uint32_t ip, uint32_t ret, unsigned args)
{
    xctx expected = *c; expected.r[0] = ret; expected.r[4] += 4 + args * 4;
    uint32_t fp = native_fp; h2_audio_host_call(c, ip);
    assert(!memcmp(c, &expected, sizeof *c) && fp == native_fp && !locked);
}
static void reject(xctx *c, uint32_t ip)
{
    xctx before = *c; h2_audio_device_snapshot dev = device;
    h2_audio_buffer bs[XA_MAX_VOICES]; xa_voice vs[XA_MAX_VOICES];
    memcpy(bs, buffers, sizeof bs); memcpy(vs, g_v, sizeof vs);
    uint8_t *ram = malloc(0x200000); assert(ram); memcpy(ram, g_xram, 0x200000);
    uint32_t fp = native_fp; unsigned a = allocs, f = frees, cl = closes;
    if (!setjmp(stopped)) { h2_audio_host_call(c, ip); assert(!"expected strict stop"); }
    assert(!memcmp(c, &before, sizeof before) && !memcmp(&dev, &device, sizeof dev));
    assert(!memcmp(bs, buffers, sizeof bs) && !memcmp(vs, g_v, sizeof vs));
    assert(!memcmp(ram, g_xram, 0x200000) && fp == native_fp && a == allocs && f == frees && cl == closes && !locked);
    free(ram);
}
static xctx description(uint32_t dev)
{
    uint32_t fields[6] = {24, 0xA0, 0, 0x4FF9, 0, 0};
    const uint8_t wave[18] = {1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,0,0};
    x_guest_write(0x3FFD, fields, sizeof fields); x_guest_write(0x4FF9, wave, sizeof wave);
    return context(dev, 0x3FFD, 0x6FFE, 0);
}
static xctx lock_context(uint32_t handle, uint32_t offset, uint32_t bytes, uint32_t flags)
{
    xctx c = context(handle, offset, bytes, 0x6FFE); X_M32(c.r[4]) = 0x3E3293;
    X_M32(c.r[4] + 20) = 0x6110; X_M32(c.r[4] + 24) = 0x6120;
    X_M32(c.r[4] + 28) = 0x6130; X_M32(c.r[4] + 32) = flags;
    return c;
}
static xctx unlock_context(uint32_t handle)
{
    xctx c = context(handle, read32(0x6FFE), read32(0x6110), read32(0x6120));
    X_M32(c.r[4]) = 0x3E3309; X_M32(c.r[4] + 20) = read32(0x6130); return c;
}
static void write_commit_tests(uint32_t handle)
{
    h2_audio_buffer *b = find_buffer(handle - 0x1C); assert(b && b->bytes == 1024);
    xctx c = unlock_context(handle); reject(&c, 0x379F40);
    const uint32_t invalid[][3] = {{0,0,0},{1,4,0},{0,6,0},{1024,4,0},{0,1028,0},{0,4,1},{0,4,2}};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        c = lock_context(handle, invalid[i][0], invalid[i][1], invalid[i][2]); reject(&c, 0x37B7B3);
    }
    const uint32_t bad_output[] = {0, b->base + 0x100, b->mirror, b->source, 0x1FF8, 0x6120, 0xFFFFFFFE};
    for (unsigned i = 0; i < sizeof bad_output / sizeof *bad_output; ++i) {
        c = lock_context(handle, 0, 1024, 0); X_M32(c.r[4] + 16) = bad_output[i]; reject(&c, 0x37B7B3);
    }
    c = lock_context(handle, 0, 1024, 0); g_xpt[7] = 0x1FF000; reject(&c, 0x37B7B3); g_xpt[7] = 0x6000;
    c = lock_context(handle, 0, 1024, 0); g_xpt[8] = 0x1FF000; reject(&c, 0x37B7B3); g_xpt[8] = 0x8000;
    c = lock_context(handle, 0, 1024, 0);
    X_M32(c.r[4] + 16) = 0x6100; X_M32(c.r[4] + 24) = 0xA100; g_xpt[10] = g_xpt[6];
    reject(&c, 0x37B7B3); g_xpt[10] = 0x9000;
    uint8_t expected[1024], changed[1024], actual[1024];
    x_guest_read(expected, b->mirror, sizeof expected);
    uint32_t commits = 0; uint64_t bytes = 0;
    /* Exercise both split and unsplit ranges, exactly at and around the ring
     * end. Changes outside the committed region must remain invisible. */
    const uint32_t ranges[][2] = {{0,1024},{0,4},{4,16},{1000,64},{1020,1024},{512,512},{516,512}};
    for (unsigned r = 0; r < sizeof ranges / sizeof *ranges; ++r) {
        uint32_t off = ranges[r][0], len = ranges[r][1], n1 = len;
        if (n1 > 1024 - off) n1 = 1024 - off;
        uint32_t n2 = len - n1;
        c = lock_context(handle, off, len, 0); call(&c, 0x37B7B3, 0, 8);
        assert(read32(0x6FFE) == b->source + off && read32(0x6110) == n1);
        assert(read32(0x6120) == (n2 ? b->source : 0) && read32(0x6130) == n2 && b->locked);
        c = lock_context(handle, 0, 4, 0); reject(&c, 0x37B7B3);
        c = context(handle, 0, 0, 0); reject(&c, 0x379F45);
        for (unsigned i = 0; i < sizeof changed; ++i) changed[i] = (uint8_t)(i * 37 + r * 23);
        x_guest_write(b->source, changed, sizeof changed);
        x_guest_read(actual, b->mirror, sizeof actual); assert(!memcmp(actual, expected, sizeof actual));
        for (unsigned i = 1; i <= 4; ++i) {
            c = unlock_context(handle); X_M32(c.r[4] + 4 + i * 4) ^= 4; reject(&c, 0x379F40);
        }
        c = unlock_context(handle); healthy = 0; reject(&c, 0x379F40); healthy = 1;
        unsigned locks = lock_calls;
        c = unlock_context(handle); call(&c, 0x379F40, 0, 5);
        assert(lock_calls == locks + 1 && lock_calls == unlock_calls && !b->locked);
        for (uint32_t i = 0; i < len; ++i) { uint32_t at = (off + i) % 1024; expected[at] = changed[at]; }
        x_guest_read(actual, b->mirror, sizeof actual); assert(!memcmp(actual, expected, sizeof actual));
        x_guest_read(actual, b->source, sizeof actual); assert(!memcmp(actual, changed, sizeof actual));
        assert(b->commits == ++commits && b->committed_bytes == (bytes += len));
        c = unlock_context(handle); reject(&c, 0x379F40);
    }
    /* Restore the constant synthetic PCM through the real paired API for the
     * following mixer sample/control/lifetime assertions. */
    c = lock_context(handle, 0, 1024, 0); call(&c, 0x37B7B3, 0, 8);
    int16_t pcm[512]; for (unsigned i = 0; i < 256; ++i) { pcm[i*2] = 8000; pcm[i*2+1] = -4000; }
    x_guest_write(b->source, pcm, sizeof pcm); c = unlock_context(handle); call(&c, 0x379F40, 0, 5);
    assert(b->committed_bytes == bytes + 1024);
}
static xctx routing_context(uint32_t handle, uint32_t count)
{
    xctx c = context(handle, 0x3FFC, 0, 0); X_M32(c.r[4]) = 0x3E321F;
    uint32_t list[2] = {count, 0x4FFC}, pairs[12];
    for (unsigned i = 0; i < 6; ++i) { pairs[i*2] = i; pairs[i*2+1] = 0; }
    x_guest_write(0x3FFC, list, 8); x_guest_write(0x4FFC, pairs, sizeof pairs);
    return c;
}
static void unchanged_route(xctx *c, uint32_t ip, uint32_t expected)
{
    uint8_t *ram = malloc(0x200000); assert(ram); memcpy(ram, g_xram, 0x200000);
    h2_audio_device_snapshot dev = device; h2_audio_buffer bs[XA_MAX_VOICES]; xa_voice vs[XA_MAX_VOICES];
    memcpy(bs, buffers, sizeof bs); memcpy(vs, g_v, sizeof vs);
    unsigned al = allocs, fr = frees, cl = closes;
    call(c, ip, expected, 2);
    assert(!memcmp(ram, g_xram, 0x200000) && !memcmp(&dev, &device, sizeof dev));
    assert(!memcmp(bs, buffers, sizeof bs) && !memcmp(vs, g_v, sizeof vs));
    assert(al == allocs && fr == frees && cl == closes); free(ram);
}
static void routing_tests(uint32_t handle, uint32_t ip)
{
    xctx c = routing_context(handle, 2); unchanged_route(&c, ip, 0);
    /* This real stereo API is available independently of the diagnostic flag
     * and does not require the diagnostic-only six-bin caller address. */
    c = routing_context(handle, 2); X_M32(c.r[4]) ^= 1; unchanged_route(&c, ip, 0);
    const uint32_t bad_count[] = {0, 1, 3, 5, 7, UINT32_MAX};
    for (unsigned i = 0; i < sizeof bad_count / sizeof *bad_count; ++i) {
        c = routing_context(handle, bad_count[i]); reject(&c, ip);
    }
    c = routing_context(handle, 2); X_M32(0x4000) = 0xFFFFFFF0; reject(&c, ip);
    c = routing_context(handle, 2); g_xpt[5] = 0x1FF000; reject(&c, ip); g_xpt[5] = 0x4000;
    for (unsigned i = 0; i < 4; ++i) {
        c = routing_context(handle, 2); X_M32(0x4FFC + i * 4) ^= 1; reject(&c, ip);
    }
    c = routing_context(handle, 2); X_M32(0x5000) = (uint32_t)-600; reject(&c, ip);
    c = routing_context(handle, 2); X_M32(0x4FFC) = 1; X_M32(0x5004) = 0; reject(&c, ip);
    c = routing_context(handle, 2); X_M32(0x386B0C) = 1; reject(&c, ip); X_M32(0x386B0C) = 0;
    c = routing_context(handle, 2); healthy = 0; reject(&c, ip); healthy = 1;
    c = routing_context(handle, 6);
#if H2_AUDIO_MULTIBIN_UNAVAILABLE
    if (ip == 0x37B6C3) { reject(&c, ip); return; }
    unchanged_route(&c, ip, 0x80004001);
    c = routing_context(handle, 6); X_M32(c.r[4]) ^= 1; reject(&c, ip);
    c = routing_context(handle, 6); X_M32(0x4000) = 0xFFFFFFF0; reject(&c, ip);
    c = routing_context(handle, 6); g_xpt[5] = 0x1FF000; reject(&c, ip); g_xpt[5] = 0x4000;
    for (unsigned i = 0; i < 12; ++i) {
        c = routing_context(handle, 6); X_M32(0x4FFC + i * 4) ^= 0x100; reject(&c, ip);
    }
#else
    reject(&c, ip); /* the valid six-bin movie list is fatal by default */
#endif
}
int main(void)
{
    unsetenv("XV_VOLUME"); g_xram = calloc(1, 0x200000); g_img_base = g_xram;
    g_xpt = malloc((1u << 20) * 4); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x1FF000;
    for (unsigned p = 1; p <= 10; ++p) g_xpt[p] = (p - 1) << 12;
    g_xpt[3] = 0x3000; g_xpt[4] = 0x2000; g_xpt[8] = 0x8000; g_xpt[9] = 0x7000;
    g_xpt[0x386] = 0xA000;
    xctx c = context(0, 0x6100, 0, 0); call(&c, 0x37D797, 0, 3);
    uint32_t dev = read32(0x6100); assert(dev == 0x100008);
    c = description(dev); X_M32(0x386B0C) = 1; reject(&c, 0x37D4BE); X_M32(0x386B0C) = 0;
    c = description(dev); allocation_failure = 1; call(&c, 0x37D4BE, 0x8007000E, 4); allocation_failure = 0;
    assert(!read32(0x6FFE) && device.references == 1 && !device.children);
    for (unsigned i = 0; i < 18; ++i) {
        c = description(dev); X_M8(0x4FF9 + i) ^= 1; reject(&c, 0x37D4BE);
    }
    for (unsigned i = 0; i < 6; ++i) if (i != 3) {
        c = description(dev); uint32_t value = 1; x_guest_write(0x3FFD + i * 4, &value, 4); reject(&c, 0x37D4BE);
    }
    c = description(dev); X_M32(c.r[4] + 16) = 1; reject(&c, 0x37D4BE);
    c = description(dev); X_M32(c.r[4] + 12) = dev; reject(&c, 0x37D4BE);
    c = description(dev); g_xpt[5] = 0x1FF000; reject(&c, 0x37D4BE); g_xpt[5] = 0x4000;
    /* Actual mixer exhaustion rolls the new object back and retains neither
     * parent nor output. All 192 voices here are synthetic fixture resources. */
    c = description(dev); for (unsigned i = 0; i < XA_MAX_VOICES; ++i) assert(xk_audio_voice_new(1, 0) == (int)i);
    unsigned before = frees; call(&c, 0x37D4BE, 0x8007000E, 4);
    assert(frees == before + 1 && !read32(0x6FFE) && device.references == 1);
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) xk_audio_voice_free(i);
    c = description(dev); call(&c, 0x37D4BE, 0, 4);
    uint32_t handle = read32(0x6FFE); h2_audio_buffer *b = find_buffer(handle - 0x1C);
    assert(b && b->references == 1 && device.references == 2 && device.children == 1);
    assert(xk_audio_free_voices() == 191 && g_v[b->voice].rate == 44100 && g_v[b->voice].channels == 2 && g_v[b->voice].bits == 16);
    /* Physical aliases of both the object and mirror are protected. */
    g_xpt[0xA] = g_xpt[b->base >> 12]; c = context(dev, 0xA100, 0, 0); reject(&c, 0x37B5CA);
    c = context(handle, 0xA000, 4096, 0); reject(&c, 0x37CC4A); g_xpt[0xA] = 0x9000;
    c = context(handle, 0x8F00, 1025, 0); reject(&c, 0x37CC4A);
    c = context(handle, 0xFFFFFFFC, 8, 0); reject(&c, 0x37CC4A);
    int16_t source[512]; for (unsigned i = 0; i < 256; ++i) { source[i*2] = 8000; source[i*2+1] = -4000; }
    x_guest_write(0x8F00, source, sizeof source);
    c = context(handle, 0x8F00, sizeof source, 0); allocation_failure = 1;
    call(&c, 0x37CC4A, 0x8007000E, 3); allocation_failure = 0; assert(!b->mirror && !g_v[b->voice].data);
    c = context(handle, 0x8F00, sizeof source, 0); call(&c, 0x37CC4A, 0, 3);
    uint8_t samples[sizeof source]; x_guest_read(samples, b->mirror, sizeof samples);
    assert(!memcmp(samples, source, sizeof samples) && g_v[b->voice].data == b->mirror);
    c = context(handle, 0x8F00, sizeof source, 0); reject(&c, 0x37CC4A);
    g_xpt[0xA] = g_xpt[b->mirror >> 12]; c = context(dev, 0xA100, 0, 0); reject(&c, 0x37B5CA); g_xpt[0xA] = 0x9000;
    write_commit_tests(handle);
    c = context(handle, 48000, 0, 0); reject(&c, 0x37C5C8);
    c = context(handle, 44100, 0, 0); call(&c, 0x37C5C8, 0, 2);
    c = context(handle, 0, 0, 0); call(&c, 0x37B66F, 0, 2);
    assert(fabsf(g_v[b->voice].volume - powf(10.0f, -600.0f / 2000)) < 1e-6f);
    c = context(handle, 0, 0, 0); call(&c, 0x37B6A7, 0, 2);
    assert(g_v[b->voice].volume == 1 && !b->headroom);
    c = context(handle, 1, 0, 0); reject(&c, 0x37B66F); reject(&c, 0x37B6A7);
    c = context(handle, (uint32_t)-9001, 0, 0); reject(&c, 0x37B66F);
    /* Unknown routing, Play, streams and an empty Lock remain strict stops. */
    c = context(handle, 0, 0, 0);
    reject(&c, 0x37C5E4); reject(&c, 0x37B6DF); reject(&c, 0x37B7B3); reject(&c, 0x37B7E3);
    routing_tests(handle, 0x37C5E4); routing_tests(handle, 0x37B6C3);
    int voice = b->voice;
    xk_audio_voice_play(voice, 1);
    int16_t mixed[XA_GRAIN * 2]; xk_audio_mix(mixed, XA_GRAIN);
    c = routing_context(handle, 2); unchanged_route(&c, 0x37C5E4, 0);
    xk_audio_mix(mixed, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN; ++i) assert(mixed[i*2] == 4000 && mixed[i*2+1] == -2000);
    memset(source, 0, sizeof source); x_guest_write(0x8F00, source, sizeof source);
    xk_audio_mix(mixed, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN; ++i) assert(mixed[i*2] == 4000 && mixed[i*2+1] == -2000);
    /* Volume and headroom alter the real samples, with the existing shared
     * 50 percent master gain independently retained. */
    c = context(handle, (uint32_t)-600, 0, 0); call(&c, 0x37B66F, 0, 2);
    xk_audio_mix(mixed, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN; ++i) {
        assert(mixed[i*2] == 2000);
        assert(mixed[i*2+1] == -1000);
    }
    c = context(handle, 600, 0, 0); call(&c, 0x37B6A7, 0, 2);
    assert(b->volume == -600 && b->headroom == 600);
    c = routing_context(handle, 2); unchanged_route(&c, 0x37B6C3, 0);
    xk_audio_mix(mixed, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN; ++i) {
        assert(mixed[i*2] == 1000);
        assert(mixed[i*2+1] == -500);
    }
    c = context(b->base, 0, 0, 0); call(&c, 0x37A14F, 2, 1);
    c = context(handle, 0, 0, 0); call(&c, 0x379F45, 1, 1);
    assert(device.references == 2 && g_v[voice].used);
    c = context(dev - 8, 0, 0, 0); call(&c, 0x37C70F, 1, 1);
    assert(healthy && !closes && device.children == 1);
    c = context(dev - 8, 0, 0, 0); reject(&c, 0x37C70F);
    uint32_t mirror = b->mirror, object = b->base;
    c = context(handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
    assert(!device.base && !healthy && closes == 1 && !g_v[voice].used);
    assert(!find_buffer(object) && g_xpt[object >> 12] == 0x1FF000 && g_xpt[mirror >> 12] == 0x1FF000);
    x_guest_read(samples, 0x8F00, sizeof samples); assert(!memcmp(samples, source, sizeof samples));
    c = context(handle, 0, 0, 0); reject(&c, 0x379F45);
    /* Internal common Release follows the same lifetime after a new device. */
    c = context(0, 0x6100, 0, 0); call(&c, 0x37D797, 0, 3); dev = read32(0x6100);
    c = description(dev); call(&c, 0x37D4BE, 0, 4); handle = read32(0x6FFE);
    for (unsigned i = 0; i < 256; ++i) { source[i*2] = 8000; source[i*2+1] = -4000; }
    x_guest_write(0x8F00, source, sizeof source);
    c = context(handle, 0x8F00, sizeof source, 0); call(&c, 0x37CC4A, 0, 3);
    b = find_buffer(handle - 0x1C); xk_audio_voice_play(b->voice, 1);
    xk_audio_mix(mixed, XA_GRAIN); xk_audio_mix(mixed, XA_GRAIN); assert(mixed[100]);
    c = context(handle - 0x1C, 0, 0, 0); call(&c, 0x37A795, 0, 1);
    xk_audio_mix(mixed, XA_GRAIN);
    for (unsigned i = 0; i < XA_GRAIN * 2; ++i) assert(!mixed[i]);
    assert(device.references == 1 && !device.children && xk_audio_free_voices() == 192);
    c = context(dev - 8, 0, 0, 0); call(&c, 0x37C70F, 0, 1);
    assert(opens == 2 && closes == 2);
    for (unsigned i = 0; i < 256; ++i) assert(!pool[i]);
    /* Original guest first Play drives the real mixer. Distinguish the
     * consumed source position from its independently captured read frontier. */
    c = context(0, 0x6100, 0, 0); call(&c, 0x37D797, 0, 3); dev = read32(0x6100);
    c = description(dev); call(&c, 0x37D4BE, 0, 4); handle = read32(0x6FFE);
    b = find_buffer(handle - 0x1C);
    c = context(handle, 0x8F00, sizeof source, 0); call(&c, 0x37CC4A, 0, 3);
    c = context(handle, 0x6FFE, 0x6120, 0); reject(&c, 0x37B777);
    for (unsigned argument = 1; argument < 4; ++argument) {
        c = context(handle, 0, 0, 1); X_M32(c.r[4] + 4 + argument * 4) ^= 2; reject(&c, 0x37B6DF);
    }
    c = context(handle, 0, 0, 1); call(&c, 0x37B6DF, 0, 4);
    assert(b->started && g_v[b->voice].playing && g_v[b->voice].looping);
    c = context(handle, 0, 0, 1); reject(&c, 0x37B6DF);
    c=context(handle,0,0,1);X_M32(c.r[4])=0x3E3639;
    xa_voice repeat_voice=g_v[b->voice];h2_audio_progress repeat_progress=test_progress;h2_audio_buffer repeat_buffer=*b;
    call(&c,0x37B6DF,0,4);
    assert(!memcmp(&repeat_voice,&g_v[b->voice],sizeof repeat_voice) && !memcmp(&repeat_progress,&test_progress,sizeof repeat_progress) && !memcmp(&repeat_buffer,b,sizeof *b));
    c = context(handle, 0, 0, 0); reject(&c, 0x379F45); /* active release needs Stop */
    c = context(handle, 0x1FF8, 0x6120, 0); reject(&c, 0x37B777);
    c = context(handle, 0x6120, 0x6120, 0); reject(&c, 0x37B777);
    c = context(handle, b->mirror, 0x6120, 0); reject(&c, 0x37B777);
    c = context(handle, 0x6FFE, 0x6120, 0); g_xpt[7] = 0x1FF000; reject(&c, 0x37B777); g_xpt[7] = 0x6000;
    c = context(handle, 0x6FFE, 0x6120, 0); call(&c, 0x37B777, 0, 3);
    assert(!read32(0x6FFE) && !read32(0x6120));
    for (unsigned grain = 0; grain < 12; ++grain) {
        xk_audio_mix(mixed, XA_GRAIN);
        assert(g_v[b->voice].frames_out == (grain + 1ull) * XA_GRAIN);
        uint32_t frontier = xk_audio_voice_pos(b->voice);
        assert(h2_audio_progress_submit(&test_progress, XA_GRAIN, frontier));
        for (unsigned remaining = XA_GRAIN;; remaining -= 256) {
            assert(h2_audio_progress_rest(&test_progress, remaining));
            c = context(handle, 0x6FFE, 0x6120, 0); call(&c, 0x37B777, 0, 3);
            uint64_t frames = (grain + 1ull) * XA_GRAIN - remaining;
            uint32_t expected = (uint32_t)(((frames * ((44100ull << 16) / 48000)) >> 16) % (sizeof source / 4)) * 4;
            assert(read32(0x6FFE) == expected && read32(0x6120) == frontier % sizeof source);
            if (!remaining) break;
        }
    }
    c = context(handle, 0, 0x6120, 0); call(&c, 0x37B777, 0, 3);
    c = context(handle, 0x6FFE, 0, 0); call(&c, 0x37B777, 0, 3);
    c = context(handle, 0, 0, 0); call(&c, 0x37B777, 0, 3);
    c = context(handle, 0, 0, 0); reject(&c, 0x37B797); /* running seek rejected */
    c = context(handle, 0x6120, 0, 0); call(&c, 0x37B75B, 0, 2); assert(read32(0x6120) == 5);
    c = context(handle, b->mirror, 0, 0); reject(&c, 0x37B75B);
    c = context(handle, 0x1FF8, 0, 0); reject(&c, 0x37B75B);
    c = context(handle, 0, 0, 0); reject(&c, 0x37B75B);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        c = context(handle, 0, 0, 0); call(&c, 0x37B703, 0, 1);
        assert(b->stopped && !g_v[b->voice].playing && !test_progress.pending);
        c = context(handle, 0, 0, 0); call(&c, 0x37B703, 0, 1); /* original idempotence */
        c = context(handle, 0x6120, 0, 0); call(&c, 0x37B75B, 0, 2); assert(!read32(0x6120));
        c = context(handle, 0x6FFE, 0x6120, 0); call(&c, 0x37B777, 0, 3);
        assert(read32(0x6FFE) == read32(0x6120));
        xk_audio_mix(mixed, XA_GRAIN); for (unsigned i = 0; i < XA_GRAIN * 2; ++i) assert(!mixed[i]);
        assert(h2_audio_progress_submit(&test_progress, XA_GRAIN, 0)); /* real quiet grain */
        c = context(handle, 0, 0, 1); reject(&c, 0x37B6DF); /* no rewind yet */
        c = context(handle, 4, 0, 0); reject(&c, 0x37B797); /* arbitrary seek not supported */
        c = context(handle, 0, 0, 0); call(&c, 0x37B797, 0, 2);
        assert(b->rewound && !g_v[b->voice].pos && !g_v[b->voice].blk_frames);
        c = context(handle, 0x6FFE, 0x6120, 0); call(&c, 0x37B777, 0, 3);
        assert(!read32(0x6FFE) && !read32(0x6120));
        c = context(handle, 0, 0, 1); call(&c, 0x37B6DF, 0, 4);
        assert(h2_audio_progress_rest(&test_progress, 0) && !test_progress.completed_frames);
        xk_audio_mix(mixed, XA_GRAIN); assert(mixed[100] && g_v[b->voice].frames_out == XA_GRAIN);
        assert(h2_audio_progress_submit(&test_progress, XA_GRAIN, xk_audio_voice_pos(b->voice)));
        assert(h2_audio_progress_rest(&test_progress, 0));
    }
    c = context(handle, 0, 0, 0); call(&c, 0x37B703, 0, 1);
    c = context(handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
    assert(test_progress.voice == -1 && !device.children);
    /* A subsequent object may use the released sole-voice slot safely. */
    c = description(dev); call(&c, 0x37D4BE, 0, 4); handle = read32(0x6FFE);
    c = context(handle, 0x8F00, sizeof source, 0); call(&c, 0x37CC4A, 0, 3);
    c = context(handle, 0, 0, 1); call(&c, 0x37B6DF, 0, 4);
    c = context(handle, 0, 0, 0); call(&c, 0x37B703, 0, 1);
    c = context(handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
    c = context(dev - 8, 0, 0, 0); call(&c, 0x37C70F, 0, 1);
    assert(opens == 3 && closes == 3 && !healthy);
    for (unsigned i = 0; i < 256; ++i) assert(!pool[i]);
    free(g_xpt); free(g_xram);
    puts("Halo 2 real PCM voice, bounded controls, mirror ownership, rollback, aliases and parent lifetime passed");
    return 0;
}
