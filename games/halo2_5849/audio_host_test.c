/* Actual adapter with synthetic guest pages and injected backend failures. */
#include "audio_host.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf stopped;
static unsigned opens, closes, allocations, frees;
static int open_failure, alloc_failure, close_failure, healthy, allocated;
static uint32_t native_fp = 0xA5A55A5A, available = 192;
static unsigned bin_updates;
static uint32_t last_bin, last_headroom;
uint32_t h2_platform_fpscr_read(void) { return native_fp; }
void h2_platform_fpscr_write(uint32_t value) { native_fp = value; }
void xv_logf(const char *format, ...) { (void)format; native_fp ^= 0x12345678; }
_Noreturn void h2_audio_stop(xctx *c, uint32_t ip, const char *reason, uint32_t value)
{ (void)c; (void)ip; (void)reason; (void)value; longjmp(stopped, 1); }
uint32_t xk_mem_arena_size(void) { return 0x9000; }
uint32_t xk_mem_alloc(uint32_t n, uint32_t align, uint32_t low, uint32_t high, int down)
{
    assert(n == 4096 && align == 4096 && !low && !high && !down && !allocated);
    ++allocations;
    if (alloc_failure) return 0;
    allocated = 1; g_xpt[5] = 0x4000; return 0x5000;
}
int xk_mem_free(uint32_t base)
{ assert(base == 0x5000 && allocated && !healthy); ++frees; allocated = 0; g_xpt[5] = 0x8000; return 0; }
uint32_t xk_mem_size(uint32_t base) { assert(base == 0x5000 && allocated); return 4096; }
int h2_audio_backend_open(void)
{ ++opens; assert(!healthy); native_fp = 1; if (open_failure) return open_failure; healthy = 1; return 0; }
int h2_audio_backend_close(void)
{ ++closes; assert(healthy); native_fp = 2; if (close_failure) return -1; healthy = 0; return 0; }
int h2_audio_backend_health(void) { return healthy ? 0 : -1; }
uint32_t h2_audio_backend_free_voices(void) { assert(healthy); return available; }
int h2_audio_backend_set_headroom(uint32_t bin, uint32_t amount)
{ assert(healthy && bin < 32); ++bin_updates; last_bin = bin; last_headroom = amount; return 0; }
int h2_audio_backend_play(int voice, uint32_t bytes, uint32_t rate)
{ (void)voice; (void)bytes; (void)rate; assert(0); return -1; }
int h2_audio_backend_cursor(int voice, uint32_t *play, uint32_t *write)
{ (void)voice; (void)play; (void)write; assert(0); return -1; }

/* Device-only failure fixtures must not allocate or mutate a buffer voice. */
int xk_audio_voice_new(int kind, uint32_t format) { (void)kind; (void)format; assert(0); return -1; }
void xk_audio_voice_free(int v) { (void)v; assert(0); }
int xk_audio_voice_playing(int v) { (void)v; assert(0); return 0; }
void xk_audio_voice_set_data(int v, uint32_t data, uint32_t bytes) { (void)v; (void)data; (void)bytes; assert(0); }
void xk_audio_voice_set_volume_db100(int v, int32_t db) { (void)v; (void)db; assert(0); }
void xk_audio_voice_set_frequency(int v, uint32_t hz) { (void)v; (void)hz; assert(0); }
void xk_audio_lock(void) { assert(0); }
void xk_audio_unlock(void) { assert(0); }

static xctx context(uint32_t a, uint32_t b, uint32_t d)
{
    xctx c; memset(&c, 0x5A, sizeof c); c.r[4] = 0x1FF4;
    X_M32(c.r[4]) = 0x11223344;
    X_M32(c.r[4] + 4) = a; X_M32(c.r[4] + 8) = b; X_M32(c.r[4] + 12) = d;
    return c;
}
static void call(xctx *c, uint32_t ip, uint32_t value, unsigned args)
{
    xctx expected = *c; expected.r[0] = value; expected.r[4] += 4 + 4 * args;
    uint32_t before_fp = native_fp;
    h2_audio_host_call(c, ip);
    assert(!memcmp(c, &expected, sizeof *c) && native_fp == before_fp);
}
static void reject(xctx *c, uint32_t ip)
{
    xctx before = *c; h2_audio_device_snapshot state = device;
    uint8_t memory[0x9000]; memcpy(memory, g_xram, sizeof memory);
    unsigned op = opens, cl = closes, al = allocations, fr = frees, bu = bin_updates;
    uint32_t before_fp = native_fp;
    if (!setjmp(stopped)) { h2_audio_host_call(c, ip); assert(!"expected sound stop"); }
    assert(!memcmp(c, &before, sizeof before));
    assert(!memcmp(&state, &device, sizeof state) && !memcmp(memory, g_xram, sizeof memory));
    assert(op == opens && cl == closes && al == allocations && fr == frees && native_fp == before_fp);
    assert(bu == bin_updates);
}
static uint32_t read32(uint32_t address)
{ uint32_t value; x_guest_read(&value, address, 4); return value; }
static xctx vector_context(unsigned count, const uint32_t *values)
{
    xctx c = context(0x5008, 0, 0);
    for (unsigned i = 0; i < count; ++i) X_M32(c.r[4] + 8 + i * 4) = values[i];
    X_M32(c.r[4] + 8 + count * 4) = 1;
    return c;
}
static void listener_vector_tests(void)
{
    h2_audio_device_snapshot saved = device;
    const uint32_t values[] = {0, 0x80000000, 0x00800000, 0x3F800000, 0xBF800000, 0x7F7FFFFF, 0xFF7FFFFF};
    for (unsigned count = 3; count <= 6; count += 3) {
        uint32_t ip = count == 3 ? 0x37D598 : 0x37D54E, bit = count == 3 ? 1 : 4;
        for (unsigned shift = 0; shift < 7; ++shift) {
            uint32_t vector[6]; for (unsigned i = 0; i < count; ++i) vector[i] = values[(i + shift) % 7];
            device = saved;
            xctx c = vector_context(count, vector);
            uint8_t memory[0x9000]; memcpy(memory, g_xram, sizeof memory);
            call(&c, ip, 0, count + 2);
            h2_audio_device_snapshot expected = saved; expected.dirty |= bit;
            memcpy(count == 3 ? expected.pending_position : expected.pending_orientation, vector, count * 4);
            assert(!memcmp(&device, &expected, sizeof device) && !memcmp(memory, g_xram, sizeof memory));
            for (unsigned scalar_index = 0; scalar_index < 3; ++scalar_index) {
                const uint32_t entries[] = {0x37D506, 0x37D5CD, 0x37D52A};
                c = context(0x5008, 0x3F800000, 0); reject(&c, entries[scalar_index]);
            }
            c = vector_context(count, vector); X_M32(c.r[4] + 8 + count * 4) = 0; reject(&c, ip);
            c = vector_context(count, vector); X_M32(c.r[4] + 8 + count * 4) = 3; reject(&c, ip);
            for (unsigned i = 0; i < count; ++i) {
                const uint32_t invalid[] = {1, 0x807FFFFF, 0x7F800000, 0xFF800000, 0x7FC12345};
                for (unsigned j = 0; j < sizeof invalid / sizeof *invalid; ++j) {
                    c = vector_context(count, vector); X_M32(c.r[4] + 8 + i * 4) = invalid[j]; reject(&c, ip);
                }
            }
            c = vector_context(count, vector); X_M32(0x386B0C) = 1; reject(&c, ip); X_M32(0x386B0C) = 0;
            healthy = 0; reject(&c, ip); healthy = 1;
            X_M32(c.r[4] + 4) = 0x5000; reject(&c, ip);
            c = vector_context(count, vector); g_xpt[2] = 0x8000; reject(&c, ip); g_xpt[2] = 0x1000;
        }
    }
    device = saved;
    uint32_t vector[6] = {0, 0, 0x3F800000, 0, 0x3F800000, 0};
    xctx c = vector_context(3, vector); call(&c, 0x37D598, 0, 5);
    c = vector_context(6, vector); call(&c, 0x37D54E, 0, 8);
    assert(device.dirty == (saved.dirty | 5));
    device = saved;
}
static void original_entry(xctx *c, uint32_t ip, int allowed)
{
    xctx before = *c; h2_audio_device_snapshot state = device;
    uint8_t memory[0x9000]; memcpy(memory, g_xram, sizeof memory);
    unsigned op = opens, cl = closes, al = allocations, fr = frees, bu = bin_updates;
    uint32_t fp = native_fp;
    if (!setjmp(stopped)) { h2_audio_guest_entry(c, ip); assert(allowed); }
    else assert(!allowed);
    assert(!memcmp(c, &before, sizeof before) && !memcmp(&state, &device, sizeof state));
    assert(!memcmp(memory, g_xram, sizeof memory) && fp == native_fp);
    assert(op == opens && cl == closes && al == allocations && fr == frees && bu == bin_updates);
}
static void original_configuration_tests(void)
{
    g_xpt[0x387] = 0x6000; g_xpt[7] = 0x7000;
    const uint32_t entries[] = {0x379F5B, 0x379E9E, 0x37E126};
    const uint32_t callers[] = {0x21E604, 0x379F61, 0x379F69};
    for (unsigned i = 0; i < 3; ++i) {
        xctx c = context(0, 0, 0); c.fs_base = 0x7000;
        X_M32(c.r[4]) = callers[i];
        for (unsigned irql = 0; irql < 3; ++irql) {
            X_M8(0x7024) = irql; original_entry(&c, entries[i], 1);
        }
        X_M32(c.r[4]) ^= 4; original_entry(&c, entries[i], 0); X_M32(c.r[4]) = callers[i];
        c.r[4]++; original_entry(&c, entries[i], 0); c.r[4]--;
        c.fs_base = 0xFFFFFFF0; original_entry(&c, entries[i], 0);
        c.fs_base = 0x8000; original_entry(&c, entries[i], 0); c.fs_base = 0x7000;
        const unsigned pages[] = {0x386, 0x387, 1};
        for (unsigned j = 0; j < 3; ++j) {
            uint32_t saved = g_xpt[pages[j]];
            g_xpt[pages[j]] = 0x8000; original_entry(&c, entries[i], 0);
            if (pages[j] != 1) { g_xpt[pages[j]] = 0x4000; original_entry(&c, entries[i], 0); }
            g_xpt[pages[j]] = saved;
        }
        healthy = 0; original_entry(&c, entries[i], 0); healthy = 1;
        c.fs_base = 0x3871C8 - 0x24; original_entry(&c, entries[i], 0);
        c.fs_base = 0x386B18 - 0x24; original_entry(&c, entries[i], 0);
        c.fs_base = c.r[4] - 0x24; original_entry(&c, entries[i], 0);
        /* Distinct guest pages mapping overlapping original table/stack bytes. */
        c.fs_base = 0x7000; c.r[4] = 0x21E0; X_M32(c.r[4]) = callers[i];
        g_xpt[0x387] = g_xpt[2]; original_entry(&c, entries[i], 0); g_xpt[0x387] = 0x6000;
        /* A valid return word is insufficient if original pushes cross a gap. */
        c.r[4] = 0x1000; X_M32(c.r[4]) = callers[i]; original_entry(&c, entries[i], 0);
    }
    xctx c = context(0, 0, 0); original_entry(&c, 0x37DEBF, 0); /* selected HRTF body */
    original_entry(&c, 0x37B86D, 0); /* DSP image loader */
    g_xpt[0x417] = 0x7000; X_M32(0x417128) = 0x37C70F;
    c = context(0x5008, 0, 0); X_M32(c.r[4]) = 0x21EB91;
    original_entry(&c, 0x379F2A, 1);
    X_M32(c.r[4]) = 0x3E3D19; original_entry(&c, 0x379F2A, 1);
    X_M32(c.r[4]) = 0x3E39FD; original_entry(&c, 0x379F2A, 1);
    X_M32(c.r[4]) = 0x3E39FC; original_entry(&c, 0x379F2A, 0); X_M32(c.r[4]) = 0x21EB91;
    X_M32(c.r[4]) ^= 4; original_entry(&c, 0x379F2A, 0); X_M32(c.r[4]) = 0x21EB91;
    X_M32(0x417128) = 0; original_entry(&c, 0x379F2A, 0); X_M32(0x417128) = 0x37C70F;
    g_xpt[0x417] = 0x8000; original_entry(&c, 0x379F2A, 0); g_xpt[0x417] = 0x7000;
    X_M32(c.r[4] + 4) = 0x5000; original_entry(&c, 0x379F2A, 0);
}
#if H2_AUDIO_EFFECTS_UNAVAILABLE
static xctx effects_context(void)
{
    xctx c = context(0x2FFC, 0x4100, 1);
    X_M32(c.r[4]) = 0x1913A9; X_M32(c.r[4] + 16) = 0x4200;
    x_guest_write(0x2FFC, "DSPImage", 9); X_M32(0x4100) = 9; X_M32(0x4104) = 10;
    return c;
}
static void effects_failure_tests(void)
{
    xctx c = effects_context(); uint8_t memory[0x9000]; memcpy(memory, g_xram, sizeof memory);
    h2_audio_device_snapshot state = device;
    unsigned op = opens, cl = closes, al = allocations, fr = frees, bu = bin_updates;
    call(&c, 0x37B86D, 0x80004001, 4);
    assert(!memcmp(memory, g_xram, sizeof memory) && !memcmp(&state, &device, sizeof state));
    assert(op == opens && cl == closes && al == allocations && fr == frees && bu == bin_updates);
    c = effects_context(); X_M32(c.r[4] + 8) = 0x3FFE;
    uint32_t locations[2] = {9, 10}; x_guest_write(0x3FFE, locations, sizeof locations);
    memcpy(memory, g_xram, sizeof memory); call(&c, 0x37B86D, 0x80004001, 4);
    assert(!memcmp(memory, g_xram, sizeof memory));
    c = effects_context(); X_M32(c.r[4]) ^= 4; reject(&c, 0x37B86D);
    c = effects_context(); X_M32(c.r[4] + 12) = 0; reject(&c, 0x37B86D);
    c = effects_context(); X_M32(c.r[4] + 12) = 3; reject(&c, 0x37B86D);
    c = effects_context(); X_M32(c.r[4] + 16) = 0x5008; reject(&c, 0x37B86D);
    c = effects_context(); X_M32(c.r[4] + 16) = 0xFFFFFFFE; reject(&c, 0x37B86D);
    c = effects_context(); X_M8(0x3004) = 'X'; reject(&c, 0x37B86D);
    c = effects_context(); X_M32(0x4100) = 8; reject(&c, 0x37B86D);
    c = effects_context(); X_M32(0x4104) = 9; reject(&c, 0x37B86D);
    c = effects_context(); healthy = 0; reject(&c, 0x37B86D); healthy = 1;
    c = effects_context(); g_xpt[3] = 0x8000; reject(&c, 0x37B86D); g_xpt[3] = 0x3000;
    c = effects_context(); X_M32(c.r[4] + 8) = 0xFFFFFFFC; reject(&c, 0x37B86D);
}
#endif
static void only_changed(const uint8_t *before, uint32_t out, unsigned bytes, int header)
{
    uint8_t allowed[0x9000] = {0};
    for (unsigned i = 0; i < bytes; ++i)
        allowed[g_xpt[(out + i) >> 12] + ((out + i) & 4095)] = 1;
    if (header) memset(allowed + 0x4000, 1, 8);
    for (unsigned i = 0; i < sizeof allowed; ++i)
        if (!allowed[i]) assert(before[i] == g_xram[i]);
}
int main(void)
{
    g_xram = malloc(0x9000); g_img_base = g_xram; g_xpt = malloc((1u << 20) * 4);
    assert(g_xram && g_xpt); memset(g_xram, 0xCC, 0x9000);
    for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x8000;
    g_xpt[1] = 0; g_xpt[2] = 0x1000; g_xpt[3] = 0x3000; g_xpt[4] = 0x2000;
    g_xpt[0x386] = 0x5000; X_M32(0x386B0C) = 0;
    xctx c = context(0, 0x2FFE, 0);
    h2_audio_guest_entry(&c, 0x37DACA); /* original static initializers still run */
    c = context(1, 0x2FFE, 0); reject(&c, 0x37D797);
    c = context(0, 0x2FFE, 1); reject(&c, 0x37D797);
    c = context(0, 0x4FFE, 0); reject(&c, 0x37D797); /* missing second output page */
    c = context(0, 0xFFFFFFFE, 0); reject(&c, 0x37D797);
    c = context(0, 0x2FFE, 0); c.r[4]++; reject(&c, 0x37D797);
    c = context(0, 0x2FFE, 0); g_xpt[2] = 0x8000; reject(&c, 0x37D797); g_xpt[2] = 0x1000;
    c = context(0, 0x2FFE, 0); g_xpt[3] = 0x3001; reject(&c, 0x37D797); g_xpt[3] = 0x3000;
    uint8_t memory[0x9000]; memcpy(memory, g_xram, sizeof memory);
    open_failure = -1; call(&c, 0x37D797, 0x88780078, 3);
    assert(!allocated && !healthy && !memcmp(memory, g_xram, sizeof memory));
    open_failure = 0; alloc_failure = 1; c = context(0, 0x2FFE, 0);
    call(&c, 0x37D797, 0x8007000E, 3); assert(!allocated && !healthy && closes == 1);
    alloc_failure = 0; c = context(0, 0x2FFE, 0); memcpy(memory, g_xram, sizeof memory);
    call(&c, 0x37D797, 0, 3); only_changed(memory, 0x2FFE, 4, 1);
    assert(read32(0x2FFE) == 0x5008 && healthy && allocated && device.references == 1);
    assert(X_M32(0x5000) == 0x417120 && X_M32(0x5004) == 1);
    assert(device.doppler == 0x3F800000 && device.pending_doppler == 0x3F800000);
    listener_vector_tests();
    for (unsigned i = 0; i < 32; ++i) assert(device.headroom[i] == (i != 31));
    /* A second caller shares the actual device, without a second output worker. */
    unsigned op = opens; c = context(0, 0x4100, 0); call(&c, 0x37D797, 0, 3);
    assert(op == opens && read32(0x4100) == 0x5008 && device.references == 2);
    c = context(0, 0x5008, 0); reject(&c, 0x37D797);
    g_xpt[6] = 0x4000; c = context(0, 0x6100, 0); reject(&c, 0x37D797); g_xpt[6] = 0x8000;
    c = context(0x5008, 0x2FFD, 0); memcpy(memory, g_xram, sizeof memory);
    call(&c, 0x37B5AE, 0, 2); only_changed(memory, 0x2FFD, 16, 0);
    assert(read32(0x2FFD) == 192 && read32(0x3001) == 0 && read32(0x3005) == 0 && read32(0x3009) == 4096);
    available = 17; c = context(0x5008, 0x2FFD, 0); call(&c, 0x37B5AE, 0, 2);
    assert(read32(0x2FFD) == 17);
    c = context(0x5008, 0x2FFF, 0); call(&c, 0x37B5CA, 0, 2); assert(read32(0x2FFF) == 0);
    c = context(0x5000, 0x4100, 0); reject(&c, 0x37B5AE);
    c = context(0x5008, 0x5FF0, 0); reject(&c, 0x37B5AE);
    c = context(0x5008, 0x4FF1, 0); reject(&c, 0x37B5AE); /* output overlaps owned page */
    c = context(0x5008, 0x4043126F, 0); call(&c, 0x37D506, 0, 3);
    assert(device.distance == 0x4043126F && !device.dirty);
    c = context(0x5008, 0x3F000000, 1); call(&c, 0x37D506, 0, 3);
    assert(device.distance == 0x4043126F && device.pending_distance == 0x3F000000 && device.dirty == 8);
    c = context(0x5008, 0x80000000, 1); call(&c, 0x37D5CD, 0, 3);
    assert(device.rolloff == 0x3F800000 && device.pending_rolloff == 0x80000000 && device.dirty == 24);
    c = context(0x5008, 0, 1); call(&c, 0x37D52A, 0, 3);
    assert(device.doppler == 0x3F800000 && !device.pending_doppler && device.dirty == 56);
    c = context(0x5008, 0x41200000, 0); call(&c, 0x37D5CD, 0, 3);
    assert(device.distance == 0x3F000000 && device.rolloff == 0x41200000 && !device.doppler && !device.dirty);
    /* Original deferred bits survive until an immediate call of any scalar. */
    const uint32_t doppler_bits[] = {0, 0x80000000, 1, 0x007FFFFF, 0x00800000, 0x3F800000, 0x41200000};
    const uint32_t immediate_entries[] = {0x37D506, 0x37D5CD, 0x37D52A};
    for (unsigned i = 0; i < sizeof doppler_bits / sizeof *doppler_bits; ++i) {
        for (unsigned j = 0; j < 3; ++j) {
            uint32_t active = device.doppler;
            c = context(0x5008, doppler_bits[i], 1); call(&c, 0x37D52A, 0, 3);
            assert(device.doppler == active && device.pending_doppler == doppler_bits[i] && device.dirty == 32);
            c = context(0x5008, j == 2 ? doppler_bits[i] : 0x3F800000, 0);
            call(&c, immediate_entries[j], 0, 3);
            assert(device.doppler == doppler_bits[i] && !device.dirty);
        }
    }
    c = context(0x5008, 0x00800000, 0); call(&c, 0x37D506, 0, 3);
    assert(device.distance == 0x00800000);
    c = context(0x5008, 0x7F7FFFFF, 0); call(&c, 0x37D506, 0, 3);
    assert(device.distance == 0x7F7FFFFF);
    const uint32_t bad_distance[] = {0, 1, 0x007FFFFF, 0x80000000, 0xBF800000, 0x7FC00001, 0x7F800000};
    for (unsigned i = 0; i < sizeof bad_distance / sizeof *bad_distance; ++i) {
        c = context(0x5008, bad_distance[i], 0); reject(&c, 0x37D506);
    }
    const uint32_t bad_rolloff[] = {0xBF800000, 0x41200001, 0xFFC00001, 0xFF800000};
    for (unsigned i = 0; i < sizeof bad_rolloff / sizeof *bad_rolloff; ++i) {
        c = context(0x5008, bad_rolloff[i], 0); reject(&c, 0x37D5CD);
        c = context(0x5008, bad_rolloff[i], 1); reject(&c, 0x37D52A);
    }
    c = context(0x5008, 0x3F800000, 2); reject(&c, 0x37D52A);
    c = context(0x5008, 0x3F800000, 0);
    X_M32(0x386B0C) = 1; reject(&c, 0x37D52A); X_M32(0x386B0C) = 0;
    g_xpt[0x386] = 0x8000; reject(&c, 0x37D52A); g_xpt[0x386] = 0x5000;
    c = context(0x5008, 0x3F800000, 2); reject(&c, 0x37D506);
    c = context(0x5008, 0x4100, 0); healthy = 0; reject(&c, 0x37B5CA); healthy = 1;
    for (unsigned bin = 0; bin < 32; ++bin) {
        uint32_t raw = 0xFEDCBA00u + bin;
        c = context(0x5008, bin, raw); memcpy(memory, g_xram, sizeof memory);
        call(&c, 0x37B637, 0, 3);
        assert(last_bin == bin && last_headroom == raw && device.headroom[bin] == (uint8_t)raw);
        assert(!memcmp(memory, g_xram, sizeof memory));
    }
    c = context(0x5008, 32, 0); reject(&c, 0x37B637);
    c = context(0x5008, UINT32_MAX, 0); reject(&c, 0x37B637);
    c = context(0x5000, 0, 0); reject(&c, 0x37B637);
    c = context(0x5008, 0, 0); healthy = 0; reject(&c, 0x37B637); healthy = 1;
    reject(&c, 0x37B86D); /* no success fallback for the effects image loader */
    original_configuration_tests();
#if H2_AUDIO_EFFECTS_UNAVAILABLE
    effects_failure_tests();
#endif
    c = context(0x5008, 0, 0); reject(&c, 0x37A14F); /* common header expects base, not interface */
    c = context(0x5000, 0, 0); call(&c, 0x37A14F, 3, 1);
    X_M32(0x5004) = 9; c = context(0x5000, 0, 0); reject(&c, 0x37A14F); X_M32(0x5004) = 3;
    device.references = UINT32_MAX; X_M32(0x5004) = UINT32_MAX;
    c = context(0x5000, 0, 0); reject(&c, 0x37A14F);
    c = context(0, 0x4100, 0); reject(&c, 0x37D797);
    device.references = 3; X_M32(0x5004) = 3;
    c = context(0x5000, 0, 0); call(&c, 0x37C70F, 2, 1); assert(healthy && allocated);
    c = context(0x5000, 0, 0); call(&c, 0x37C70F, 1, 1); assert(healthy && allocated);
    c = context(0x5000, 0, 0); xctx before_close = c;
    h2_audio_device_snapshot before_device = device; memcpy(memory, g_xram, sizeof memory);
    close_failure = 1;
    if (!setjmp(stopped)) { h2_audio_host_call(&c, 0x37C70F); assert(!"close failure must be terminal"); }
    assert(!memcmp(&c, &before_close, sizeof c) && !memcmp(&device, &before_device, sizeof device));
    assert(healthy && allocated && !frees && !memcmp(memory, g_xram, sizeof memory));
    close_failure = 0;
    c = context(0x5000, 0, 0); call(&c, 0x37C70F, 0, 1); assert(!healthy && !allocated && frees == 1);
    c = context(0x5000, 0, 0); reject(&c, 0x37C70F);
    c = context(0x5008, 0x4100, 0); reject(&c, 0x37B5CA);
    if (!setjmp(stopped)) { h2_audio_guest_entry(&c, 0x37B637); assert(!"unknown original method must stop after host creation"); }
    c = context(0, 0x4100, 0); call(&c, 0x37D797, 0, 3); assert(device.distance == 0x3F800000);
    assert(device.doppler == 0x3F800000 && device.pending_doppler == 0x3F800000);
    for (unsigned i = 0; i < 32; ++i) assert(device.headroom[i] == (i != 31));
    c = context(0x5000, 0, 0); call(&c, 0x37C70F, 0, 1); assert(frees == 2);
    free(g_xpt); free(g_xram); puts("Halo 2 audio device ABI/lifetime tests passed"); return 0;
}

int h2_audio_backend_stop(int v) { (void)v; assert(0); return -1; }
int h2_audio_backend_status_voice(int v, uint32_t *s) { (void)v; (void)s; assert(0); return -1; }
int h2_audio_backend_rewind(int v) { (void)v; assert(0); return -1; }
int h2_audio_backend_forget(int v) { (void)v; assert(0); return -1; }
