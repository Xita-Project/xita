/* Checked XDK5849 device and external PCM buffer boundaries. Unknown sound
 * methods stop; every accepted object owns real mixer/output resources. */
#include "audio_host.h"
#include "recomp/kernel/xk.h"
#include "recomp/kernel/xk_audio.h"
#include <string.h>
#if H2_AUDIO_DSP
#include "dsp_asset.h"
static h2_dsp_engine *effects;
static uint32_t effects_guest, effects_guest_bytes;
#endif

static h2_audio_device_snapshot device;
typedef struct {
    uint32_t base, references, mirror, mirror_bytes, source, bytes;
    uint32_t frequency, headroom;
    int32_t volume;
    int voice;
    uint32_t locked, lock_offset, lock_first, lock_second, commits;
    uint32_t started, stopped, rewound, cursor_queries;
    uint64_t committed_bytes;
    /* MIXIN owns a mono 32-sample, signed-24-in-32 bus, never a PCM voice.
     * Only inactive ownership and deferred parameters are supported. */
    uint32_t submix, spatial[41];
    uint8_t route_bins[6];
    uint32_t route_count;
    int32_t route_gains[6];
    uint32_t fx_bin;
} h2_audio_buffer;
static h2_audio_buffer buffers[XA_MAX_VOICES];
typedef struct {
    uint32_t base, references, callback, context, packet_limit, headroom;
    int voice;
} h2_audio_stream;
static h2_audio_stream streams[XA_MAX_VOICES];
extern uint32_t h2_platform_fpscr_read(void);
extern void h2_platform_fpscr_write(uint32_t);
extern void xv_logf(const char *, ...);

static void fail(xctx *c, uint32_t ip, const char *reason, uint32_t value)
{ h2_audio_stop(c, ip, reason, value); }
static int mapped(uint32_t address, uint32_t bytes)
{
    uint32_t arena = xk_mem_arena_size();
    if (!address || !bytes || arena < 4096 ||
        (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t p = address >> 12; p <= last; ++p)
        if ((g_xpt[p] & 4095) || (uint64_t)g_xpt[p] + 4096 > arena - 4096) return 0;
    return 1;
}
static void stack(xctx *c, uint32_t ip, unsigned args)
{
    if ((c->r[4] & 3) || !mapped(c->r[4], 4 + 4 * args))
        fail(c, ip, "stack", c->r[4]);
}
static int page_overlap(uint32_t address, uint32_t bytes, uint32_t owned, uint32_t size)
{
    if (!owned || !size) return 0;
    uint32_t last = (address + bytes - 1) >> 12;
    uint32_t end = (owned + size - 1) >> 12;
    for (uint32_t p = address >> 12; p <= last; ++p)
        for (uint32_t q = owned >> 12; q <= end; ++q)
            if (g_xpt[p] == g_xpt[q]) return 1;
    return 0;
}
static int overlaps_device(uint32_t address, uint32_t bytes)
{
    /* Opaque objects and unexposed sample mirrors own full guest pages.
     * Include physical aliases through distinct virtual mappings. */
    if (page_overlap(address, bytes, device.base, 4096)) return 1;
#if H2_AUDIO_DSP
    if (page_overlap(address, bytes, effects_guest, effects_guest_bytes)) return 1;
#endif
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i)
        if (page_overlap(address, bytes, buffers[i].base, 4096) ||
            page_overlap(address, bytes, buffers[i].mirror, buffers[i].mirror_bytes)) return 1;
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i)
        if (page_overlap(address, bytes, streams[i].base, 4096)) return 1;
    return 0;
}
static void output(xctx *c, uint32_t ip, uint32_t address, uint32_t bytes)
{
    if (!mapped(address, bytes) || overlaps_device(address, bytes))
        fail(c, ip, "output", address);
}
static void live(xctx *c, uint32_t ip, uint32_t object, int internal)
{
    uint32_t expected = device.base + (internal ? 0 : 8);
    if (!device.base || !device.references || object != expected ||
        !mapped(device.base, 4096) || X_M32(device.base) != 0x417120 ||
        X_M32(device.base + 4) != device.references)
        fail(c, ip, "device identity", object);
    if (h2_audio_backend_health() < 0) fail(c, ip, "output worker", object);
}
static void result(xctx *c, uint32_t value, unsigned args)
{ c->r[0] = value; c->r[4] += 4 + 4 * args; }
static void create(xctx *c)
{
    const uint32_t ip = 0x37D797;
    stack(c, ip, 3);
    uint32_t out = X_ARG(1), caller = X_M32(c->r[4]);
    if (X_ARG(0) || X_ARG(2)) fail(c, ip, "creation parameters", X_ARG(0) | X_ARG(2));
    output(c, ip, out, 4);
    if (device.base) {
        live(c, ip, device.base + 8, 0);
        if (device.references == UINT32_MAX) fail(c, ip, "reference overflow", device.references);
        ++device.references;
        X_M32(device.base + 4) = device.references;
    } else {
        int opened = h2_audio_backend_open();
        if (opened < -1) fail(c, ip, "output rollback", (uint32_t)opened);
        if (opened < 0) { result(c, 0x88780078, 3); return; }
        uint32_t base = xk_mem_alloc(4096, 4096, 0, 0, 0);
        if (!base) {
            if (h2_audio_backend_close() < 0) fail(c, ip, "allocation rollback", 0);
            result(c, 0x8007000E, 3); return;
        }
        if ((base & 4095) || !mapped(base, 4096)) fail(c, ip, "allocation mapping", base);
        device = (h2_audio_device_snapshot){
            .base = base, .references = 1, .ever_created = 1,
            .distance = 0x3F800000, .rolloff = 0x3F800000, .doppler = 0x3F800000,
            .pending_distance = 0x3F800000, .pending_rolloff = 0x3F800000,
            .pending_doppler = 0x3F800000
        };
        memset(device.headroom, 1, 31);
        /* Match only the independently audited common vtable/refcount header.
         * Public wrappers expose base+8. All remaining original DSOUND object
         * methods are guarded; no fake DSP/listener/voice pointers are stored. */
        X_M32(base) = 0x417120; X_M32(base + 4) = 1;
    }
    uint32_t handle = device.base + 8;
    x_guest_write(out, &handle, 4);
    xv_logf("[h2/audio] real device create caller=%08X interface=%08X references=%u\n",
            caller, handle, device.references);
    result(c, 0, 3);
}
static void drop_device(xctx *c, uint32_t ip)
{
    if (device.references > 1) {
        --device.references; X_M32(device.base + 4) = device.references;
    } else {
        uint32_t released = device.base;
        if (device.children) fail(c, ip, "live device children", device.children);
        if (h2_audio_backend_close() < 0) fail(c, ip, "close worker", device.base);
#if H2_AUDIO_DSP
        if (effects) {
            if (xk_mem_free(effects_guest) < 0) fail(c, ip, "free DSP views", effects_guest);
            h2_dsp_destroy(effects); effects = NULL; effects_guest = effects_guest_bytes = 0;
        }
#endif
        if (xk_mem_free(device.base) < 0) fail(c, ip, "free device", device.base);
        device = (h2_audio_device_snapshot){.ever_created = 1};
        xv_logf("[h2/audio] final Release caller=%08X device=%08X worker/port closed, guest allocation freed\n",
                X_M32(c->r[4]), released);
    }
}
static h2_audio_buffer *find_buffer(uint32_t base)
{
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i)
        if (buffers[i].base && buffers[i].base == base) return &buffers[i];
    return NULL;
}
static void buffer_operational(xctx *c, uint32_t ip)
{
    if (!mapped(0x386B0C, 4) || X_M32(0x386B0C))
        fail(c, ip, "unsupported sound shutdown state", 0x386B0C);
}
static h2_audio_buffer *buffer_live(xctx *c, uint32_t ip, uint32_t object, int internal)
{
    buffer_operational(c, ip);
    h2_audio_buffer *b = find_buffer(object - (internal ? 0 : 0x1C));
    if (!b || !b->references || !mapped(b->base, 4096) ||
        X_M32(b->base) != 0x417150 || X_M32(b->base + 4) != b->references ||
        !device.children || device.references < device.children)
        fail(c, ip, "buffer identity", object);
    live(c, ip, device.base, 1);
    if (b->submix == 1 && ip != 0x37D4BE && ip != 0x37A14F && ip != 0x379F45 &&
        ip != 0x37A795 && ip != 0x37C5E4 && ip != 0x37C620 && ip != 0x37C644 &&
        ip != 0x37C6C1 && ip != 0x37C69D && ip != 0x37C600)
        fail(c, ip, "submix activation/data/spatial processing is unsupported", b->base);
    if (b->submix == 2 && ip != 0x37A14F && ip != 0x379F45 && ip != 0x37A795 &&
        ip != 0x37B66F && ip != 0x37C5E4 && ip != 0x37B6DF &&
        ip != 0x37C620 && ip != 0x37C644 && ip != 0x37C6E5)
        fail(c, ip, "unsupported FXIN2 control/data/spatial method", b->base);
    return b;
}
static int aliases(uint32_t a, unsigned an, uint32_t b, unsigned bn);
static void reference(xctx *c, uint32_t ip)
{
    stack(c, ip, 1);
    if (ip != 0x37C70F && (ip != 0x37A14F || find_buffer(X_ARG(0)))) {
        h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), ip != 0x379F45);
        uint32_t count = b->references;
        if (ip != 0x37A14F && count == 1 && b->locked)
            fail(c, ip, "release locked buffer", b->base);
        if (ip != 0x37A14F && count == 1 && b->started && !b->stopped)
            fail(c, ip, "release active PCM voice requires completed Stop", b->base);
        if (ip == 0x37A14F) {
            if (count == UINT32_MAX) fail(c, ip, "buffer reference overflow", count);
            X_M32(b->base + 4) = ++b->references;
            count = b->references;
        } else if (--count) {
            X_M32(b->base + 4) = b->references = count;
        } else {
            /* free() takes the mixer lock, so no worker can still read the
             * mirror when its allocation is released. Caller owns source. */
            if (!b->submix && b->started && h2_audio_backend_forget(b->voice) < 0)
                fail(c, ip, "release PCM progress ownership", b->voice);
#if H2_AUDIO_DSP
            if (b->submix == 2 && h2_audio_backend_fx_forget(b->fx_bin) < 0)
                fail(c, ip, "release FXIN2 source ownership", b->base);
#endif
            if (!b->submix) xk_audio_voice_free(b->voice);
            if (!b->submix && b->mirror && xk_mem_free(b->mirror) < 0) fail(c, ip, "free PCM mirror", b->mirror);
            if (xk_mem_free(b->base) < 0) fail(c, ip, "free buffer", b->base);
            *b = (h2_audio_buffer){0};
            --device.children; drop_device(c, ip);
        }
        result(c, count, 1); return;
    }
    live(c, ip, X_ARG(0), 1);
    if (ip == 0x37A14F) {
        if (device.references == UINT32_MAX) fail(c, ip, "reference overflow", device.references);
        ++device.references; X_M32(device.base + 4) = device.references;
    } else {
        if (device.references <= device.children) fail(c, ip, "release child-owned device", device.references);
        drop_device(c, ip);
    }
    result(c, device.references, 1);
}
static void spatial_defaults(uint32_t state[41])
{
    memset(state, 0, 41 * sizeof *state);
    /* Named XDK defaults: dirty parameters, 360-degree cones, +Z cone vector,
     * min/max 1/1e9, unit distance/rolloff/Doppler, I3DL2 occlusion LF ratio. */
    state[0] = 0x07FF0000;
    state[0x20 / 4] = state[0x24 / 4] = 360;
    state[0x30 / 4] = state[0x38 / 4] = 0x3F800000;
    state[0x3C / 4] = 0x4E6E6B28;
    state[0x44 / 4] = state[0x48 / 4] = state[0x4C / 4] = 0x3F800000;
    state[0x7C / 4] = 0x007F0000;
    state[0xA0 / 4] = 0x3E800000;
}
static void submix_create(xctx *c, uint32_t desc, uint32_t out, const uint32_t fields[6])
{
    const uint32_t ip = 0x37D4BE;
    if (X_M32(c->r[4]) != 0x220AC8 || fields[0] != 24 || fields[1] != 0x2010 ||
        fields[2] || fields[3] || fields[4] || fields[5])
        fail(c, ip, "unsupported submix description/caller", fields[1]);
    if (aliases(out, 4, c->r[4], 20) || aliases(out, 4, desc, 24))
        fail(c, ip, "submix output alias", out);
    if (device.references == UINT32_MAX) fail(c, ip, "submix parent overflow", device.references);
    unsigned index;
    for (index = 0; index < XA_MAX_VOICES && buffers[index].base; ++index) {}
    if (index == XA_MAX_VOICES) { result(c, 0x8007000E, 4); return; }
    /* One allocation owns both the opaque header and a separate mapped bus
     * page. No permissive WAVE parser or PCM voice sees the MIXIN format. */
    uint32_t base = xk_mem_alloc(8192, 4096, 0, 0, 0);
    if (!base) { result(c, 0x8007000E, 4); return; }
    if ((base & 4095) || !mapped(base, 8192) || overlaps_device(base, 8192) ||
        page_overlap(base, 8192, c->r[4], 20) || page_overlap(base, 8192, out, 4) ||
        page_overlap(base, 8192, desc, 24)) fail(c, ip, "submix allocation mapping/alias", base);
    h2_audio_buffer candidate = {.base = base, .references = 1, .voice = -1,
        .submix = 1, .mirror = base + 4096, .mirror_bytes = 4096, .bytes = 128,
        .frequency = 48000, .route_bins = {6, 8, 7, 9, 10}};
    spatial_defaults(candidate.spatial);
    uint32_t silence[32] = {0}; x_guest_write(candidate.mirror, silence, sizeof silence);
    buffers[index] = candidate;
    X_M32(base) = 0x417150; X_M32(base + 4) = 1;
    ++device.children; X_M32(device.base + 4) = ++device.references;
    uint32_t handle = base + 0x1C; x_guest_write(out, &handle, 4);
    xv_logf("[h2/submix] create caller=%08X interface=%08X bus=%08X signed24 mono48000 samples=32 input_bin=31 routes=6,8,7,9,10 headroom=0 parent_refs=%u; inactive, DSP/HRTF activation unsupported\n",
            X_M32(c->r[4]), handle, candidate.mirror, device.references);
    result(c, 0, 4);
}
#if H2_AUDIO_DSP
static void fx_create(xctx *c, uint32_t desc, uint32_t out, const uint32_t fields[6])
{
    const uint32_t ip = 0x37D4BE;
    uint32_t caller = X_M32(c->r[4]), bin = fields[5];
    int spatial = 0; int8_t taps[32] = {0};
#if H2_AUDIO_SPATIAL_MODEL
    spatial = fields[1] == 0x100010;
    if (spatial) {
        if (caller != 0x21E88A || bin < 23 || bin > 25 || !mapped(0x386958, 64) ||
            !mapped(0x3871F0, 4) || X_M32(0x3871F0) != 2)
            fail(c, ip, "unsupported spatial FXIN2 caller/HRTF mode", caller);
        int8_t right[32]; x_guest_read(taps, 0x386958, 32); x_guest_read(right, 0x386978, 32);
        uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned i = 0; i < 32; ++i) hash = (hash ^ (uint8_t)taps[i]) * UINT64_C(1099511628211);
        if (memcmp(taps, right, 32) || taps[31] || hash != UINT64_C(0xE3399E65D9A92FE8))
            fail(c, ip, "changed symmetric zero-delay HRTF coefficients", 0x386958);
    }
#endif
    if (spatial && (aliases(out, 4, 0x386958, 64) || aliases(out, 4, 0x3871F0, 4)))
        fail(c, ip, "spatial FXIN2 output/filter-control alias", out);
    uint32_t source_key = spatial ? 0x10000u | bin : bin;
    if (!((caller == 0x220C26 && bin == 13) || (caller == 0x21E830 && bin >= 23 && bin <= 25) || spatial) ||
        fields[0] != 24 || fields[1] != (spatial ? 0x100010u : 0x100000u) || fields[2] || fields[3] || fields[4] || !effects)
        fail(c, ip, "unsupported FXIN2 description/caller", fields[1]);
    if (aliases(out, 4, c->r[4], 20) || aliases(out, 4, desc, 24))
        fail(c, ip, "FXIN2 output alias", out);
    if (device.references == UINT32_MAX) fail(c, ip, "FXIN2 parent overflow", device.references);
    uint32_t predecessor = spatial ? bin : bin == 23 ? 13 : (0x10000u | (bin - 1));
    int predecessor_playing = 0;
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) if (buffers[i].base && buffers[i].submix == 2) {
        if (buffers[i].fx_bin == source_key) fail(c, ip, "duplicate FXIN2 source", bin);
        if (buffers[i].fx_bin == predecessor && buffers[i].started && !buffers[i].stopped) predecessor_playing = 1;
    }
    if (bin != 13 && !predecessor_playing) fail(c, ip, "FXIN2 loop predecessor must be active", source_key);
    unsigned index;
    for (index = 0; index < XA_MAX_VOICES && buffers[index].base; ++index) {}
    if (index == XA_MAX_VOICES) { result(c, 0x8007000E, 4); return; }
    uint32_t base = xk_mem_alloc(4096, 4096, 0, 0, 0);
    if (!base) { result(c, 0x8007000E, 4); return; }
    if ((base & 4095) || !mapped(base, 4096) || overlaps_device(base, 4096) ||
        page_overlap(base, 4096, c->r[4], 20) || page_overlap(base, 4096, out, 4) ||
        page_overlap(base, 4096, desc, 24)) fail(c, ip, "FXIN2 allocation mapping/alias", base);
    if (spatial && (page_overlap(base, 4096, 0x386958, 64) || page_overlap(base, 4096, 0x3871F0, 4)))
        fail(c, ip, "spatial FXIN2 allocation/filter-control alias", base);
    if ((spatial ? h2_audio_backend_fx_bind_spatial(effects, bin, taps) : h2_audio_backend_fx_bind(effects, bin)) < 0) {
        if (xk_mem_free(base) < 0) fail(c, ip, "FXIN2 allocation rollback", base);
        fail(c, ip, "FXIN2 engine/idle voices/headroom contract", bin);
    }
    buffers[index] = (h2_audio_buffer){.base = base, .references = 1, .voice = -1,
        .submix = 2, .bytes = 128, .frequency = 48000, .route_count = 2, .route_bins = {0, 1}, .fx_bin = source_key};
    if (spatial) {
        spatial_defaults(buffers[index].spatial); buffers[index].route_count = 5;
        const uint8_t routes[5] = {6,8,7,9,10}; memcpy(buffers[index].route_bins, routes, 5);
    }
    X_M32(base) = 0x417150; X_M32(base + 4) = 1;
    ++device.children; X_M32(device.base + 4) = ++device.references;
    uint32_t handle = base + 0x1C; x_guest_write(out, &handle, 4);
    xv_logf("[h2/fxin2] create caller=%08X interface=%08X real GP scratch=%04X bin=%u signed24 mono48000 samples=32 default routes=%s headroom=0 parent_refs=%u inactive\n",
            caller, handle, 0xB000 + (bin - 11) * 128, bin, spatial ? "6,8,7,9,10; symmetric HRTF model" : "0,1", device.references);
    result(c, 0, 4);
}
#endif
static void buffer_create(xctx *c)
{
    const uint32_t ip = 0x37D4BE;
    stack(c, ip, 4); live(c, ip, X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t desc = X_ARG(1), out = X_ARG(2), fields[6];
    output(c, ip, out, 4);
    if (X_ARG(3) || !mapped(desc, sizeof fields)) fail(c, ip, "buffer descriptor", desc);
    x_guest_read(fields, desc, sizeof fields);
    if (fields[1] == 0x2010) { submix_create(c, desc, out, fields); return; }
#if H2_AUDIO_DSP
    if (fields[1] == 0x100000) { fx_create(c, desc, out, fields); return; }
#if H2_AUDIO_SPATIAL_MODEL
    if (fields[1] == 0x100010) { fx_create(c, desc, out, fields); return; }
#endif
#endif
    if (fields[0] != 24 || fields[1] != 0xA0 || fields[2] || fields[4] || fields[5])
        fail(c, ip, "unsupported buffer description", fields[1]);
    /* Exact observed PCM format; do not feed permissive mixer defaults an
     * unknown codec, malformed average/block alignment, or extension. */
    const uint8_t pcm[18] = {1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,0,0};
    uint8_t format[18];
    if (!mapped(fields[3], 18)) fail(c, ip, "buffer format mapping", fields[3]);
    x_guest_read(format, fields[3], 18);
    if (memcmp(format, pcm, 18)) fail(c, ip, "unsupported PCM format", fields[3]);
    if (device.references == UINT32_MAX) fail(c, ip, "reference overflow", device.references);
    unsigned index;
    for (index = 0; index < XA_MAX_VOICES && buffers[index].base; ++index) {}
    if (index == XA_MAX_VOICES) { result(c, 0x8007000E, 4); return; }
    uint32_t base = xk_mem_alloc(4096, 4096, 0, 0, 0);
    if (!base) { result(c, 0x8007000E, 4); return; }
    if ((base & 4095) || !mapped(base, 4096)) fail(c, ip, "buffer allocation mapping", base);
    x_guest_write(base + 64, format, 18);
    int voice = xk_audio_voice_new(1, base + 64);
    if (voice < 0) {
        if (xk_mem_free(base) < 0) fail(c, ip, "buffer rollback", base);
        result(c, 0x8007000E, 4); return;
    }
    buffers[index] = (h2_audio_buffer){.base = base, .references = 1, .voice = voice,
                                     .frequency = 44100, .headroom = 600};
    xk_audio_lock(); xk_audio_voice_set_volume_db100(voice, -600); xk_audio_unlock();
    X_M32(base) = 0x417150; X_M32(base + 4) = 1;
    ++device.children; X_M32(device.base + 4) = ++device.references;
    uint32_t handle = base + 0x1C; x_guest_write(out, &handle, 4);
    xv_logf("[h2/audio-buffer] create caller=%08X interface=%08X voice=%u PCM16 stereo44100 parent_refs=%u\n",
            X_M32(c->r[4]), handle, voice, device.references);
    result(c, 0, 4);
}
static h2_audio_stream *stream_live(xctx *c, uint32_t ip, uint32_t object)
{
    buffer_operational(c, ip); live(c, ip, device.base, 1);
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) {
        h2_audio_stream *s = streams + i;
        if (!s->base || s->base != object) continue;
        if (!s->references || !mapped(s->base, 4096) || X_M32(s->base) != 0x417170 ||
            X_M32(s->base + 4) != 0x417160 || X_M32(s->base + 8) != s->references ||
            !device.children || device.references < device.children)
            fail(c, ip, "stream identity", object);
        return s;
    }
    fail(c, ip, "unknown stream", object); return NULL;
}
static void stream_create(xctx *c)
{
    const uint32_t ip = 0x37D4E2;
    stack(c, ip, 4); live(c, ip, X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t desc = X_ARG(1), out = X_ARG(2), fields[6];
    output(c, ip, out, 4);
    if (X_M32(c->r[4]) != 0x2AE692 || X_ARG(3) || !mapped(desc, sizeof fields))
        fail(c, ip, "stream caller/descriptor", desc);
    x_guest_read(fields, desc, sizeof fields);
    if (fields[0] != 0x20000000 || fields[1] != 2 || fields[3] != 0x220730 || fields[5])
        fail(c, ip, "unsupported stream description", fields[0]);
    uint8_t format[20] = {0};
    if (!mapped(fields[2], 18)) fail(c, ip, "stream format mapping", fields[2]);
    x_guest_read(format, fields[2], 18);
    uint32_t format_bytes = format[0] == 0x69 ? 20 : 18;
    if (!mapped(fields[2], format_bytes)) fail(c, ip, "stream format extension", fields[2]);
    x_guest_read(format, fields[2], format_bytes);
    /* Original 21E410 builds these three exact formats. Reject everything
     * else before the shared mixer's permissive format parser can see it. */
    const uint8_t formats[3][20] = {
        {0x69,0,1,0,0x44,0xAC,0,0,0xE4,0x60,0,0,36,0,4,0,2,0,64,0},
        {0x69,0,2,0,0x44,0xAC,0,0,0xC8,0xC1,0,0,72,0,4,0,2,0,64,0},
        {1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,0,0,0,0}
    };
    unsigned kind;
    for (kind = 0; kind < 3 && memcmp(format, formats[kind], 20); ++kind) {}
    if (kind == 3) fail(c, ip, "unsupported stream format", fields[2]);
    if (aliases(out, 4, c->r[4], 20) || aliases(out, 4, desc, 24) || aliases(out, 4, fields[2], format_bytes))
        fail(c, ip, "stream output alias", out);
    if (device.references == UINT32_MAX) fail(c, ip, "stream parent reference overflow", device.references);
    unsigned index;
    for (index = 0; index < XA_MAX_VOICES && streams[index].base; ++index) {}
    if (index == XA_MAX_VOICES) { result(c, 0x8007000E, 4); return; }
    uint32_t base = xk_mem_alloc(4096, 4096, 0, 0, 0);
    if (!base) { result(c, 0x8007000E, 4); return; }
    if ((base & 4095) || !mapped(base, 4096) || overlaps_device(base, 4096) ||
        page_overlap(base, 4096, c->r[4], 20) || page_overlap(base, 4096, out, 4) ||
        page_overlap(base, 4096, desc, 24) || page_overlap(base, 4096, fields[2], format_bytes))
        fail(c, ip, "stream allocation mapping/alias", base);
    x_guest_write(base + 64, format, 20);
    int voice = xk_audio_voice_new(2, base + 64);
    if (voice < 0) {
        if (xk_mem_free(base) < 0) fail(c, ip, "stream allocation rollback", base);
        result(c, 0x8007000E, 4); return;
    }
    xk_audio_lock(); xk_audio_voice_set_volume_db100(voice, -600); xk_audio_unlock();
    streams[index] = (h2_audio_stream){base, 1, fields[3], fields[4], fields[1], 600, voice};
    X_M32(base) = 0x417170; X_M32(base + 4) = 0x417160; X_M32(base + 8) = 1;
    ++device.children; X_M32(device.base + 4) = ++device.references;
    x_guest_write(out, &base, 4);
    xv_logf("[h2/audio-stream] create caller=%08X object=%08X voice=%u format=%u channels=%u packet_limit=2 callback=%08X context=%u parent_refs=%u; empty real mixer voice, packet/DSP routing unsupported\n",
            X_M32(c->r[4]), base, voice, kind, format[2], fields[3], fields[4], device.references);
    result(c, 0, 4);
}
static void stream_reference(xctx *c, uint32_t ip)
{
    stack(c, ip, 1); h2_audio_stream *s = stream_live(c, ip, X_ARG(0));
    uint32_t count = s->references;
    if (ip == 0x37AB40) {
        if (count == UINT32_MAX) fail(c, ip, "stream reference overflow", count);
        X_M32(s->base + 8) = s->references = ++count;
    } else if (count > 1) {
        X_M32(s->base + 8) = s->references = --count;
    } else {
        /* No Process call is accepted yet; all supported streams are empty.
         * Free takes the mixer lock before releasing their guest storage. */
        xk_audio_lock(); int playing = xk_audio_voice_playing(s->voice); xk_audio_unlock();
        if (playing) fail(c, ip, "stream release requires completed packet ownership", s->base);
        xk_audio_voice_free(s->voice);
        if (xk_mem_free(s->base) < 0) fail(c, ip, "free stream", s->base);
        *s = (h2_audio_stream){0}; --device.children; drop_device(c, ip); count = 0;
    }
    result(c, count, 1);
}
static void stream_headroom(xctx *c)
{
    const uint32_t ip = 0x37B818;
    stack(c, ip, 2); h2_audio_stream *s = stream_live(c, ip, X_ARG(0));
    if (X_ARG(1)) fail(c, ip, "unsupported stream headroom", X_ARG(1));
    /* Original 37A629 replaces the stored attenuation and adjusts total gain.
     * The observed zero request removes the default 600 hundredths of dB. */
    xk_audio_lock(); xk_audio_voice_set_volume_db100(s->voice, 0); xk_audio_unlock();
    s->headroom = 0;
    xv_logf("[h2/audio-stream] headroom object=%08X caller=%08X real mixer attenuation=0dB\n", s->base, X_M32(c->r[4]));
    result(c, 0, 2);
}
static void buffer_data(xctx *c)
{
    const uint32_t ip = 0x37CC4A;
    stack(c, ip, 3); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t source = X_ARG(1), bytes = X_ARG(2);
    /* First bind only in this milestone. Playback and live rebinding remain
     * strict stops until their routing/commit/cursor contracts are supplied. */
    if (b->mirror || !bytes || bytes > 0x100000 || (bytes & 3) || (source & 3) ||
        !mapped(source, bytes) || overlaps_device(source, bytes))
        fail(c, ip, "unsupported external PCM binding", source);
    uint32_t size = (bytes + 4095) & ~4095u;
    uint32_t mirror = xk_mem_alloc(size, 4096, 0, 0, 0);
    if (!mirror) { result(c, 0x8007000E, 3); return; }
    if ((mirror & 4095) || !mapped(mirror, size)) fail(c, ip, "PCM mirror mapping", mirror);
    uint8_t chunk[4096];
    for (uint32_t at = 0; at < bytes; at += sizeof chunk) {
        uint32_t n = bytes - at; if (n > sizeof chunk) n = sizeof chunk;
        x_guest_read(chunk, source + at, n); x_guest_write(mirror + at, chunk, n);
    }
    xk_audio_voice_set_data(b->voice, mirror, bytes);
    b->source = source; b->bytes = bytes; b->mirror = mirror; b->mirror_bytes = size;
    xv_logf("[h2/audio-buffer] SetBufferData caller=%08X interface=%08X external=%08X bytes=%u mirror=%08X\n",
            X_M32(c->r[4]), b->base + 0x1C, source, bytes, mirror);
    result(c, 0, 3);
}
static void buffer_control(xctx *c, uint32_t ip)
{
    stack(c, ip, 2); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t value = X_ARG(1);
    if (b->submix == 2) {
#if H2_AUDIO_DSP
        uint32_t caller = X_M32(c->r[4]);
        uint32_t muted_key = caller == 0x2AECA6 ? H2_FX_SPATIAL23 : caller == 0x2AEDF8 ? H2_FX_SPATIAL24 : caller == 0x2AEF43 ? 25 : 0;
        if (ip == 0x37B66F && muted_key) {
            if (value != (uint32_t)-6400 || b->fx_bin != muted_key || !b->started || b->stopped ||
                b->route_count != (muted_key == 25 ? 2u : 5u) || b->headroom || (b->volume && b->volume != -6400))
                fail(c, ip, "unsupported original FX mute state/value", value);
            if (h2_audio_backend_fx_mute(muted_key) < 0) fail(c, ip, "FX mixer mute rejected", b->base);
            b->volume = -6400;
            xv_logf("[h2/fxin2] caller=%08X interface=%08X source_key=%X original volume=-6400 attenuationFFF on all routes; source/filter/GP time remains active\n", caller, b->base + 0x1C, muted_key);
            result(c, 0, 2); return;
        }
#endif
        int initial = !b->started && b->fx_bin == 13 && X_M32(c->r[4]) == 0x220C37;
        int retained = b->started && !b->stopped && !b->volume && !b->headroom &&
            ((b->fx_bin == 23 && b->route_count == 4 && X_M32(c->r[4]) == 0x2AEC95) ||
             (b->fx_bin == 24 && b->route_count == 4 && X_M32(c->r[4]) == 0x2AEDE7) ||
             (b->fx_bin == H2_FX_SPATIAL25 && b->route_count == 5 && b->route_gains[0] == -6400 && !b->route_gains[4] && X_M32(c->r[4]) == 0x2AEF32));
        if (ip != 0x37B66F || value || (!initial && !retained))
            fail(c, ip, "unsupported FXIN2 gain/state/caller", value);
        b->volume = 0;
        xv_logf("[h2/fxin2] original unity volume retained caller=%08X interface=%08X\n", X_M32(c->r[4]), b->base + 0x1C);
        result(c, 0, 2); return;
    }
    if (ip == 0x37C5C8) {
        if (value && value != 44100) fail(c, ip, "unsupported buffer frequency", value);
        xk_audio_lock(); xk_audio_voice_set_frequency(b->voice, value); xk_audio_unlock();
        b->frequency = value ? value : 44100;
    } else {
        int32_t volume = b->volume; uint32_t headroom = b->headroom;
        if (ip == 0x37B66F) {
            volume = (int32_t)value;
            if (volume > 0 || volume < -9000) fail(c, ip, "unsupported buffer volume", value);
        } else {
            if (value != 0 && value != 600) fail(c, ip, "unsupported buffer headroom", value);
            headroom = value;
        }
        xk_audio_lock(); xk_audio_voice_set_volume_db100(b->voice, volume - (int32_t)headroom); xk_audio_unlock();
        b->volume = volume; b->headroom = headroom;
    }
    xv_logf("[h2/audio-buffer] control entry=%08X value=%08X caller=%08X frequency=%u volume=%d headroom=%u\n",
            ip, value, X_M32(c->r[4]), b->frequency, b->volume, b->headroom);
    result(c, 0, 2);
}
static void buffer_lock(xctx *c)
{
    const uint32_t ip = 0x37B7B3;
    stack(c, ip, 8); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t offset = X_ARG(1), bytes = X_ARG(2);
    if (!b->mirror || b->locked || X_ARG(7) || !bytes || bytes > b->bytes ||
        offset >= b->bytes || ((offset | bytes) & 3) || !mapped(b->source, b->bytes) ||
        overlaps_device(b->source, b->bytes) || !mapped(b->mirror, b->mirror_bytes))
        fail(c, ip, "unsupported PCM lock range/state", offset);
    uint32_t out[4] = {X_ARG(3), X_ARG(4), X_ARG(5), X_ARG(6)};
    for (unsigned i = 0; i < 4; ++i) {
        output(c, ip, out[i], 4);
        if (aliases(out[i], 4, c->r[4], 36) || page_overlap(out[i], 4, b->source, b->bytes))
            fail(c, ip, "PCM lock output/source/stack alias", out[i]);
        for (unsigned j = 0; j < i; ++j)
            if (aliases(out[i], 4, out[j], 4)) fail(c, ip, "PCM lock output alias", out[i]);
    }
    uint32_t first = b->bytes - offset;
    if (first > bytes) first = bytes;
    uint32_t second = bytes - first;
    uint32_t value[4] = {b->source + offset, first, second ? b->source : 0, second};
    /* Original Lock returns caller-owned addresses, with a split only at the
     * ring end. Validate every output before writing any of them. */
    for (unsigned i = 0; i < 4; ++i) x_guest_write(out[i], &value[i], 4);
    b->locked = 1; b->lock_offset = offset; b->lock_first = first; b->lock_second = second;
    if (b->commits < 8)
        xv_logf("[h2/audio-buffer] Lock caller=%08X offset=%u bytes=%u first=%08X+%u second=%08X+%u\n",
                X_M32(c->r[4]), offset, bytes, value[0], first, value[2], second);
    result(c, 0, 8);
}
static void buffer_unlock(xctx *c)
{
    const uint32_t ip = 0x379F40;
    stack(c, ip, 5); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t second = b->lock_second ? b->source : 0;
    if (!b->locked || X_ARG(1) != b->source + b->lock_offset || X_ARG(2) != b->lock_first ||
        X_ARG(3) != second || X_ARG(4) != b->lock_second ||
        !mapped(b->source, b->bytes) || overlaps_device(b->source, b->bytes) ||
        !mapped(b->mirror, b->mirror_bytes))
        fail(c, ip, "unmatched PCM write commit", X_ARG(1));
    /* Xbox Unlock is a no-op. For the audited Bink paired-write contract,
     * commit its completed regions to the unexposed host mirror atomically
     * with respect to the real mixer. No worker reads the writable source. */
    uint8_t chunk[4096];
    uint32_t offsets[2] = {b->lock_offset, 0}, sizes[2] = {b->lock_first, b->lock_second};
    xk_audio_lock();
    for (unsigned region = 0; region < 2; ++region) {
        for (uint32_t at = 0; at < sizes[region]; at += sizeof chunk) {
            uint32_t n = sizes[region] - at; if (n > sizeof chunk) n = sizeof chunk;
            uint32_t offset = offsets[region] + at;
            x_guest_read(chunk, b->source + offset, n); x_guest_write(b->mirror + offset, chunk, n);
        }
    }
    xk_audio_unlock();
    b->locked = 0; ++b->commits; b->committed_bytes += b->lock_first + b->lock_second;
    if (b->commits <= 8)
        xv_logf("[h2/audio-buffer] Unlock caller=%08X committed=%u+%u total=%llu commits=%u\n",
                X_M32(c->r[4]), b->lock_first, b->lock_second,
                (unsigned long long)b->committed_bytes, b->commits);
    result(c, 0, 5);
}
static void buffer_routing(xctx *c, uint32_t ip)
{
    stack(c, ip, 2); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t list_address = X_ARG(1), list[2], pairs[12];
    if ((!b->mirror && b->submix != 2) || !mapped(list_address, 8))
        fail(c, ip, "unsupported PCM route input", list_address);
    x_guest_read(list, list_address, 8);
    if (b->submix == 1) {
        if (ip != 0x37C5E4 || X_M32(c->r[4]) != 0x220B29 || list[0] != 5 || !mapped(list[1], 40))
            fail(c, ip, "unsupported submix routing caller/list", list[0]);
        x_guest_read(pairs, list[1], 40);
        for (unsigned i = 0; i < 5; ++i)
            if (pairs[2 * i] != b->route_bins[i] || pairs[2 * i + 1])
                fail(c, ip, "unsupported submix route/gain", pairs[2 * i]);
        xv_logf("[h2/submix] original default five-bin unity route retained interface=%08X; inactive\n", b->base + 0x1C);
        result(c, 0, 2); return;
    }
#if H2_AUDIO_DSP
    if (b->submix == 2) {
        uint32_t caller = X_M32(c->r[4]);
        uint32_t key = caller == 0x2AEC87 ? 23 : caller == 0x2AEDD9 ? 24 : caller == 0x2AEF24 ? H2_FX_SPATIAL25 : 0;
        if (ip == 0x37C5E4 && key) {
            unsigned count = key == H2_FX_SPATIAL25 ? 5 : 4, output_mask = key == 23 ? 64 : key == 24 ? 128 : 1024;
            if (!b->started || b->stopped || b->fx_bin != key || b->volume || b->headroom ||
                list[0] != count || !mapped(list[1], count * 8)) fail(c, ip, "unsupported active FX route state/list", list[0]);
            x_guest_read(pairs, list[1], count * 8);
            const uint32_t bins[5] = {6,8,7,9,10};
            for (unsigned i = 0; i < count; ++i) {
                uint32_t gain = output_mask & (1u << bins[i]) ? 0 : (uint32_t)-6400;
                if (pairs[i * 2] != bins[i] || pairs[i * 2 + 1] != gain)
                    fail(c, ip, "unsupported active FX route/gain", list[1]);
            }
            if (h2_audio_backend_fx_route_mask(key, output_mask) < 0) fail(c, ip, "FX active mixer route rejected", b->base);
            b->route_count = count;
            for (unsigned i = 0; i < count; ++i) { b->route_bins[i] = pairs[i * 2]; b->route_gains[i] = (int32_t)pairs[i * 2 + 1]; }
            xv_logf("[h2/fxin2] SetMixBins caller=%08X interface=%08X source_key=%X count=%u unity_mask=%03X others original attenuationFFF/mute; future computed grains updated\n", caller, b->base + 0x1C, key, count, output_mask);
            result(c, 0, 2); return;
        }
        if (ip != 0x37C5E4 || b->started || b->fx_bin != 13 || X_M32(c->r[4]) != 0x220CAA ||
            list[0] != 6 || !mapped(list[1], 48)) fail(c, ip, "unsupported FXIN2 route caller/list", list[0]);
        x_guest_read(pairs, list[1], 48);
        for (unsigned i = 0; i < 6; ++i)
            if (pairs[2 * i] != i || pairs[2 * i + 1]) fail(c, ip, "unsupported FXIN2 route/gain", pairs[2 * i]);
        if (h2_audio_backend_fx_route(b->fx_bin, 6) < 0) fail(c, ip, "FXIN2 real route binding", b->base);
        b->route_count = 6; for (unsigned i = 0; i < 6; ++i) b->route_bins[i] = i;
        xv_logf("[h2/fxin2] caller=%08X interface=%08X six independent unity GP routes 0..5\n", X_M32(c->r[4]), b->base + 0x1C);
        result(c, 0, 2); return;
    }
#endif
    if (list[0] != 2
#if H2_AUDIO_MULTIBIN_UNAVAILABLE
        && !(ip == 0x37C5E4 && list[0] == 6 && X_M32(c->r[4]) == 0x3E321F)
#endif
        ) fail(c, ip, "unsupported PCM route count/caller", list[0]);
    if (!mapped(list[1], list[0] * 8)) fail(c, ip, "unmapped PCM route pairs", list[1]);
    x_guest_read(pairs, list[1], list[0] * 8);
    for (unsigned i = 0; i < list[0]; ++i)
        if (pairs[i * 2] != i || pairs[i * 2 + 1])
            fail(c, ip, "unsupported PCM route/gain", pairs[i * 2]);
    if (list[0] == 2) {
        /* The only real route supported by these stereo PCM voices is
         * channel 0 -> FL/bin 0 and channel 1 -> FR/bin 1, both unity gain.
         * Re-selecting this already-active route or its unity per-bin gains
         * needs no mixer mutation. Buffer volume/headroom remain independent. */
        xv_logf("[h2/audio-buffer] stereo entry=%08X caller=%08X interface=%08X real FL/FR unity route/gains\n",
                ip, X_M32(c->r[4]), b->base + 0x1C);
        result(c, 0, 2);
        return;
    }
#if H2_AUDIO_MULTIBIN_UNAVAILABLE
    /* Explicit failure probe for the observed unsupported six-speaker route;
     * preserve the actual stereo route, objects and caller memory. */
    xv_logf("[h2/diagnostic] SetMixBins caller=%08X interface=%08X bins0..5 volume0 returns DSERR_UNSUPPORTED=80004001; real default FL/FR voice unchanged\n",
            X_M32(c->r[4]), b->base + 0x1C);
    result(c, 0x80004001, 2);
#endif
}
static void fx_deferred_parameters(xctx *c)
{
    const uint32_t ip = 0x37C6E5;
    stack(c, ip, 3); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t address = X_ARG(1), words[9];
    if (X_M32(c->r[4]) != 0x2AEF6A || X_ARG(2) != 1 || !mapped(address, sizeof words) ||
        b->submix != 2 || b->fx_bin != H2_FX_SPATIAL25 || !b->started || b->stopped || b->volume || b->headroom ||
        b->route_count != 5 || b->route_bins[4] != 10 || b->route_gains[0] != -6400 || b->route_gains[4])
        fail(c, ip, "unsupported FX25 deferred parameter state/input", address);
    x_guest_read(words, address, sizeof words);
    /* Original 37C0E9 copies nine raw words then ORs byte[7E] with7F.
     * Flag1 returns without touching the active voice, filter or DSP. */
    memcpy(&b->spatial[0x80 / 4], words, sizeof words);
    b->spatial[0x7C / 4] |= 0x007F0000;
    xv_logf("[h2/fxin2] deferred parameters caller=002AEF6A interface=%08X input=%08X words=9 dirty=%08X; original storage only, active commit unsupported\n",
            b->base + 0x1C, address, b->spatial[0x7C / 4]);
    result(c, 0, 3);
}
static void submix_deferred(xctx *c, uint32_t ip)
{
    stack(c, ip, 3); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t value = X_ARG(1), offset, dirty, caller;
    switch (ip) {
    case 0x37C620: offset = 0x3C; dirty = 0x00200000; caller = 0x220B3E; break;
    case 0x37C644: offset = 0x38; dirty = 0x00200000; caller = 0x220B4D; break;
    case 0x37C6C1: offset = 0x48; dirty = 0x01000000; caller = 0x220B58; break;
    case 0x37C69D: offset = 0x4C; dirty = 0x02000000; caller = 0x220B63; break;
    default: offset = 0x34; dirty = 0x00100000; caller = 0x220B6E; break;
    }
    if (b->submix == 2) {
        if ((b->fx_bin < H2_FX_SPATIAL23 || b->fx_bin > H2_FX_SPATIAL25) || b->started || (ip != 0x37C620 && ip != 0x37C644))
            fail(c, ip, "unsupported FXIN2 deferred parameter", ip);
        caller = ip == 0x37C620 ? 0x21E89D : 0x21E8AC;
    }
    if (!b->submix || X_ARG(2) != 1 || X_M32(c->r[4]) != caller ||
        value != (offset == 0x38 || offset == 0x3C ? 0x7F7FFFFF : 0))
        fail(c, ip, "unsupported submix deferred parameter/caller", value);
    b->spatial[offset / 4] = value; b->spatial[0] |= dirty;
    xv_logf("[h2/submix] deferred entry=%08X caller=%08X interface=%08X offset=%X bits=%08X dirty=%08X; spatial commit unsupported\n",
            ip, caller, b->base + 0x1C, offset, value, b->spatial[0]);
    result(c, 0, 3);
}
static void buffer_play(xctx *c)
{
    const uint32_t ip = 0x37B6DF;
    stack(c, ip, 4); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
#if H2_AUDIO_DSP
    if (b->submix == 2) {
        uint32_t routes = b->fx_bin == 13 ? 6 : 2, caller = b->fx_bin == 13 ? 0x220CB5 : 0x21E842;
        if ((b->fx_bin >= H2_FX_SPATIAL23 && b->fx_bin <= H2_FX_SPATIAL25)) {
            uint32_t expected[41]; spatial_defaults(expected);
            expected[0x38/4] = expected[0x3C/4] = 0x7F7FFFFF;
            const uint32_t orientation[6] = {0x3F800000,0,0,0,0x3F800000,0}, zero[3] = {0};
            if (memcmp(expected, b->spatial, sizeof expected) || device.dirty != 0x25 ||
                device.pending_distance != 0x4043126F || device.pending_rolloff || device.pending_doppler ||
                memcmp(device.pending_position, zero, sizeof zero) ||
                memcmp(device.pending_orientation, orientation, sizeof orientation))
                fail(c, ip, "unsupported spatial FXIN2 listener/voice geometry", b->base);
            routes = 5; caller = 0x21E8BA;
        }
        if (b->started || b->route_count != routes || b->volume || b->headroom || X_ARG(1) || X_ARG(2) || X_ARG(3) ||
            X_M32(c->r[4]) != caller) fail(c, ip, "unsupported FXIN2 Play state/flags/caller", X_ARG(3));
        if (h2_audio_backend_fx_play(b->fx_bin) < 0) fail(c, ip, "FXIN2 real DSP/sink Play rejected", b->base);
        b->started = 1;
        if ((b->fx_bin >= H2_FX_SPATIAL23 && b->fx_bin <= H2_FX_SPATIAL25)) {
            /* Original 382032 / CA0A / AE57 updates at the verified Play. */
            b->spatial[0] = b->spatial[0x7C/4] = 0; b->spatial[1] = 0xF8000000;
            b->spatial[0x70/4] = 0x43340000;
            xv_logf("[h2/fxin2] fixed symmetric zero-delay HRTF model active: actual GP bins6,7,10 unity; bins8,9 muted; dynamic spatial changes unsupported\n");
        }
        xv_logf("[h2/fxin2] Play caller=%08X interface=%08X source_key=%X input_bin=%u original FX loop semantics, actual source-tagged GP grain accepted by stereo sink\n",
                X_M32(c->r[4]), b->base + 0x1C, b->fx_bin, b->fx_bin & 0xFFFF);
        result(c, 0, 4); return;
    }
    if (effects) fail(c, ip, "loaded DSP PCM voice routing is unsupported", X_M32(c->r[4]));
#endif
    if (!b->mirror || (b->started && !b->rewound) || b->locked || X_ARG(1) || X_ARG(2) || X_ARG(3) != 1 ||
        !mapped(b->mirror, b->mirror_bytes) || !mapped(b->source, b->bytes) || overlaps_device(b->source, b->bytes))
        fail(c, ip, "unsupported PCM Play state/flags", X_ARG(3));
    if (h2_audio_backend_play(b->voice, b->bytes, b->frequency) < 0)
        fail(c, ip, "real PCM sink Play rejected", b->voice);
    b->started = 1; b->stopped = b->rewound = 0;
    xv_logf("[h2/audio-buffer] real looping Play caller=%08X interface=%08X voice=%d bytes=%u frequency=%u\n",
            X_M32(c->r[4]), b->base + 0x1C, b->voice, b->bytes, b->frequency);
    result(c, 0, 4);
}
static void buffer_stop(xctx *c)
{
    const uint32_t ip = 0x37B703;
    stack(c, ip, 1); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    if (!b->started || b->locked) fail(c, ip, "unsupported PCM Stop state", b->base);
    if (h2_audio_backend_stop(b->voice) < 0) fail(c, ip, "real PCM stop/drain rejected", b->voice);
    b->stopped = 1;
    xv_logf("[h2/audio-buffer] real Stop caller=%08X interface=%08X voice=%d drained=1\n",
            X_M32(c->r[4]), b->base + 0x1C, b->voice);
    result(c, 0, 1);
}
static void buffer_status(xctx *c)
{
    const uint32_t ip = 0x37B75B;
    stack(c, ip, 2); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t out = X_ARG(1), status;
    if (!b->started) fail(c, ip, "PCM status before supported Play", b->base);
    output(c, ip, out, 4);
    if (aliases(out, 4, c->r[4], 12)) fail(c, ip, "PCM status output/stack alias", out);
    if (h2_audio_backend_status_voice(b->voice, &status) < 0 || (status != 0 && status != 5))
        fail(c, ip, "real PCM status rejected", b->voice);
    x_guest_write(out, &status, 4); result(c, 0, 2);
}
static void buffer_rewind(xctx *c)
{
    const uint32_t ip = 0x37B797;
    stack(c, ip, 2); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    if (!b->started || !b->stopped || b->locked || X_ARG(1))
        fail(c, ip, "unsupported PCM seek state/position", X_ARG(1));
    if (h2_audio_backend_rewind(b->voice) < 0) fail(c, ip, "real PCM rewind rejected", b->voice);
    b->rewound = 1;
    xv_logf("[h2/audio-buffer] stopped rewind caller=%08X interface=%08X position=0\n",
            X_M32(c->r[4]), b->base + 0x1C);
    result(c, 0, 2);
}
static void buffer_cursor(xctx *c)
{
    const uint32_t ip = 0x37B777;
    stack(c, ip, 3); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    if (!b->started) fail(c, ip, "PCM cursor before supported Play", b->base);
    uint32_t out[2] = {X_ARG(1), X_ARG(2)}, positions[2];
    for (unsigned i = 0; i < 2; ++i) if (out[i]) {
        output(c, ip, out[i], 4);
        if (aliases(out[i], 4, c->r[4], 16)) fail(c, ip, "PCM cursor output/stack alias", out[i]);
    }
    if (out[0] && out[1] && aliases(out[0], 4, out[1], 4))
        fail(c, ip, "PCM cursor output alias", out[0]);
    if (h2_audio_backend_cursor(b->voice, &positions[0], &positions[1]) < 0)
        fail(c, ip, "real PCM sink cursor rejected", b->voice);
    if ((positions[0] | positions[1]) & 3 || positions[0] >= b->bytes || positions[1] >= b->bytes)
        fail(c, ip, "invalid real PCM sink cursor", positions[0]);
    for (unsigned i = 0; i < 2; ++i) if (out[i]) x_guest_write(out[i], &positions[i], 4);
    if (b->cursor_queries++ < 8)
        xv_logf("[h2/audio-buffer] sink cursor caller=%08X played=%u decoded_frontier=%u\n",
                X_M32(c->r[4]), positions[0], positions[1]);
    result(c, 0, 3);
}
static void query(xctx *c, uint32_t ip)
{
    stack(c, ip, 2);
    uint32_t out = X_ARG(1), bytes = ip == 0x37B5AE ? 16 : 4;
    output(c, ip, out, bytes); live(c, ip, X_ARG(0), 0);
    /* Actual free software voices, no implemented 3D voices or hardware SGE
     * pool, and the device's real guest allocation. Stereo is Xbox value 0. */
    uint32_t values[4] = {0};
    if (bytes == 16) {
        values[0] = h2_audio_backend_free_voices();
        values[3] = xk_mem_size(device.base);
    }
    x_guest_write(out, values, bytes);
    xv_logf("[h2/audio] query entry=%08X output=%08X values=%u,%u,%u,%u\n",
            ip, out, values[0], values[1], values[2], values[3]);
    result(c, 0, 2);
}
static void scalar(xctx *c, uint32_t ip)
{
    stack(c, ip, 3);
    uint32_t bits = X_ARG(1), apply = X_ARG(2);
    uint32_t magnitude = bits & 0x7FFFFFFF;
    /* Integer IEEE checks retain the original payload bits and native FPSCR.
     * Unknown apply bits/nonfinite/out-of-domain parameters stop explicitly. */
    int valid = apply <= 1 && magnitude < 0x7F800000;
    if (ip == 0x37D506) valid &= !(bits >> 31) && magnitude >= 0x00800000;
    else valid &= (bits == 0x80000000 || !(bits >> 31)) && magnitude <= 0x41200000;
    if (!valid) fail(c, ip, "listener parameters", bits);
    live(c, ip, X_ARG(0), 0);
    if (ip == 0x37D52A && (!mapped(0x386B0C, 4) || X_M32(0x386B0C)))
        fail(c, ip, "unsupported sound shutdown state", 0x386B0C);
    if (!apply && (device.dirty & 5))
        fail(c, ip, "pending spatial listener commit is unsupported", device.dirty);
    if (!apply)
        for (unsigned i = 0; i < XA_MAX_VOICES; ++i)
            if (buffers[i].submix) fail(c, ip, "submix spatial listener commit is unsupported", buffers[i].base);
    if (ip == 0x37D506) { device.pending_distance = bits; device.dirty |= 8; }
    else if (ip == 0x37D5CD) { device.pending_rolloff = bits; device.dirty |= 16; }
    else { device.pending_doppler = bits; device.dirty |= 32; }
    if (!apply) {
        /* Original immediate application commits all pending listener values.
         * There are no 3D voices in this supported subset. */
        device.distance = device.pending_distance; device.rolloff = device.pending_rolloff;
        device.doppler = device.pending_doppler;
        device.dirty = 0;
    }
    xv_logf("[h2/audio] listener entry=%08X bits=%08X apply=%u active=%08X,%08X,%08X pending=%08X,%08X,%08X dirty=%X\n",
            ip, bits, apply, device.distance, device.rolloff, device.doppler,
            device.pending_distance, device.pending_rolloff, device.pending_doppler, device.dirty);
    result(c, 0, 3);
}
static void listener_vector(xctx *c, uint32_t ip)
{
    unsigned count = ip == 0x37D598 ? 3 : 6;
    stack(c, ip, count + 2);
    if (X_ARG(count + 1) != 1)
        fail(c, ip, "immediate spatial listener commit is unsupported", X_ARG(count + 1));
    uint32_t value[6] = {0};
    for (unsigned i = 0; i < count; ++i) {
        value[i] = X_ARG(i + 1);
        uint32_t magnitude = value[i] & 0x7FFFFFFF;
        /* Only normal finite values and signed zero: subnormal/nonfinite
         * x87 exception behavior is outside this pending-state boundary. */
        if (magnitude >= 0x7F800000 || (magnitude && magnitude < 0x00800000))
            fail(c, ip, "listener vector representation", value[i]);
    }
    live(c, ip, X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t *pending = count == 3 ? device.pending_position : device.pending_orientation;
    memcpy(pending, value, count * 4); device.dirty |= count == 3 ? 1 : 4;
    xv_logf("[h2/audio] deferred listener entry=%08X caller=%08X values=%08X,%08X,%08X,%08X,%08X,%08X dirty=%X; spatial commit unsupported\n",
            ip, X_M32(c->r[4]), value[0], value[1], value[2], value[3], value[4], value[5], device.dirty);
    result(c, 0, count + 2);
}
static void mix_bin(xctx *c, uint32_t ip)
{
    stack(c, ip, 3);
    uint32_t bin = X_ARG(1), amount = X_ARG(2);
    if (bin >= 32) fail(c, ip, "mix bin index", bin);
    live(c, ip, X_ARG(0), 0);
    if (h2_audio_backend_set_headroom(bin, amount) < 0)
        fail(c, ip, "mix bin output", bin);
    device.headroom[bin] = (uint8_t)amount;
    xv_logf("[h2/audio] mix bin=%u stored=%u shift=%u caller=%08X\n",
            bin, device.headroom[bin], amount & 7, X_M32(c->r[4]));
    result(c, 0, 3);
}
#if H2_AUDIO_DSP
static void effects_download(xctx *c)
{
    const uint32_t ip = 0x37B86D;
    stack(c, ip, 4);
    uint32_t name = X_ARG(0), location = X_ARG(1), flags = X_ARG(2), out = X_ARG(3);
    if (X_M32(c->r[4]) != 0x1913A9 || flags != 1 || !mapped(name, 9) || !mapped(location, 8))
        fail(c, ip, "effects download input", name);
    char label[9]; uint32_t locations[2];
    x_guest_read(label, name, sizeof label); x_guest_read(locations, location, sizeof locations);
    if (memcmp(label, "DSPImage", 9) || locations[0] != 9 || locations[1] != 10)
        fail(c, ip, "effects download image/location", location);
    output(c, ip, out, 4); live(c, ip, device.base + 8, 0); buffer_operational(c, ip);
    if (aliases(out, 4, c->r[4], 20) || effects || device.children)
        fail(c, ip, "effects download ownership/alias", out);
    /* Real initialized interpreter; no descriptor is published before the
     * owned monitor's completed command acknowledgement and frame halt. */
    h2_dsp_status state;
    h2_dsp_engine *candidate = h2_dsp_asset_open("app0:halo2-dsp.bin", &state);
    if (!candidate) {
        xv_logf("[h2/dsp] initialization failed reason=%s pc=%04X address=%08X value=%08X\n",
                state.fault ? state.fault : "allocation", state.pc, state.fault_address, state.fault_value);
        fail(c, ip, "DSP initialization", state.pc);
    }
    uint32_t bytes = 0xB000 + state.scratch_bytes;
    uint32_t base = xk_mem_alloc(bytes, 4096, 0, 0, 0);
    if (!base) { h2_dsp_destroy(candidate); result(c, 0x8007000E, 4); return; }
    if ((base & 4095) || !mapped(base, bytes) || overlaps_device(base, bytes) ||
        page_overlap(base, bytes, c->r[4], 20) || page_overlap(base, bytes, out, 4))
        fail(c, ip, "DSP guest view allocation", base);
    /* The interpreter is paused at a completed frame. These read-only views
     * contain its actual initialized banks; later DSP execution/voice routing
     * remains unsupported here. The descriptor owns this guest allocation. */
    const uint32_t sizes[4] = {0x4000, 0x2000, 0x4000, state.scratch_bytes};
    uint32_t cursor = base + 4096;
    uint8_t chunk[4096];
    for (unsigned space = 0; space < 4; ++space) {
        for (uint32_t offset = 0; offset < sizes[space]; offset += sizeof chunk) {
            uint32_t n = sizes[space] - offset;
            if (n > sizeof chunk) n = sizeof chunk;
            if (!h2_dsp_copy_space(candidate, space, offset, chunk, n)) fail(c, ip, "DSP bank export", space);
            x_guest_write(cursor + offset, chunk, n);
        }
        cursor += sizes[space];
    }
    uint32_t descriptor[2 + 15 * 8] = {15, state.scratch_bytes - 0xC000};
    if (state.effect_count != 15) fail(c, ip, "DSP effect count", state.effect_count);
    for (uint32_t i = 0; i < 15; ++i) {
        h2_dsp_effect map;
        if (!h2_dsp_effect_map(candidate, i, &map)) fail(c, ip, "DSP effect map", i);
        uint32_t *d = descriptor + 2 + i * 8;
        /* Original 37E229 relocates code after the 5CC-byte monitor, state
         * to X:80, Y to its base, and scratch after its C000-byte prefix. */
        d[0] = base + 0x7000 + 0x5CC + map.code_offset - 0x818; d[1] = map.code_bytes;
        d[2] = base + 0x1000 + 0x200 + map.state_offset - 0x3F98; d[3] = map.state_bytes;
        d[4] = base + 0x5000 + map.y_offset; d[5] = map.y_bytes;
        d[6] = base + 0xB000 + 0xC000 + map.scratch_offset; d[7] = map.scratch_bytes;
    }
    x_guest_write(base, descriptor, sizeof descriptor);
    effects = candidate; effects_guest = base; effects_guest_bytes = bytes;
    x_guest_write(out, &base, 4);
    xv_logf("[h2/dsp] original image initialized caller=%08X descriptor=%08X bytes=%u effects=%u locations=%u,%u instructions=%llu transfers=%llu command=%u state=%016llX; paused after completed zero-input frame\n",
            X_M32(c->r[4]), base, bytes, state.effect_count, locations[0], locations[1],
            (unsigned long long)state.instructions, (unsigned long long)state.transfers, state.command,
            (unsigned long long)state.state_fingerprint);
    result(c, 0, 4);
}
static void effects_query(xctx *c)
{
    const uint32_t ip = 0x37B5E6;
    stack(c, ip, 5); live(c, ip, X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t index = X_ARG(1), offset = X_ARG(2), out = X_ARG(3), bytes = X_ARG(4);
    if (!effects || !mapped(effects_guest, effects_guest_bytes)) fail(c, ip, "no initialized DSP image", index);
    if (index >= 15) { result(c, 0x88780032, 5); return; }
    output(c, ip, out, bytes);
    if (aliases(out, bytes, c->r[4], 24)) fail(c, ip, "DSP effect query stack alias", out);
    /* One serialized read cannot straddle worker frames. The engine validates
     * its exact effect range before writing this private temporary. */
    uint8_t state[0x4000];
    if (bytes > sizeof state || !h2_audio_backend_effect_read(effects, index, offset, state, bytes))
        fail(c, ip, "DSP effect query range/state", offset);
    x_guest_write(out, state, bytes);
    xv_logf("[h2/dsp] GetEffectData caller=%08X index=%u offset=%u bytes=%u actual initialized state\n",
            X_M32(c->r[4]), index, offset, bytes);
    result(c, 0, 5);
}
#endif
#if H2_AUDIO_EFFECTS_UNAVAILABLE
/* Explicit failure experiment, not an effects implementation. No output,
 * device, section reference or worker resource is changed by this call. */
static void effects_unavailable(xctx *c)
{
    const uint32_t ip = 0x37B86D;
    stack(c, ip, 4);
    uint32_t name = X_ARG(0), location = X_ARG(1), flags = X_ARG(2), out = X_ARG(3);
    if (X_M32(c->r[4]) != 0x1913A9 || flags != 1 || !mapped(name, 9) || !mapped(location, 8))
        fail(c, ip, "effects diagnostic input", name);
    char label[9]; x_guest_read(label, name, sizeof label);
    uint32_t locations[2]; x_guest_read(locations, location, sizeof locations);
    if (memcmp(label, "DSPImage", 9) || locations[0] != 9 || locations[1] != 10)
        fail(c, ip, "effects diagnostic image/location", location);
    output(c, ip, out, 4); live(c, ip, device.base + 8, 0);
    xv_logf("[h2/diagnostic] XAudioDownloadEffectsImage caller=%08X image=%08X location=%08X flags=%u output=%08X returns DSERR_UNSUPPORTED=80004001; output untouched\n",
            X_M32(c->r[4]), name, location, flags, out);
    result(c, 0x80004001, 4);
}
#endif
void h2_audio_host_call(xctx *c, uint32_t ip)
{
    uint32_t fpscr = h2_platform_fpscr_read();
    switch (ip) {
    case 0x37C620: case 0x37C644: case 0x37C6C1: case 0x37C69D: case 0x37C600: submix_deferred(c, ip); break;
    case 0x37D4E2: stream_create(c); break;
    case 0x37AB40: case 0x37AB87: stream_reference(c, ip); break;
    case 0x37B818: stream_headroom(c); break;
    case 0x37D598: case 0x37D54E: listener_vector(c, ip); break;
    case 0x37D797: create(c); break;
    case 0x37A14F: case 0x37C70F: case 0x379F45: case 0x37A795: reference(c, ip); break;
    case 0x37D4BE: buffer_create(c); break;
    case 0x37CC4A: buffer_data(c); break;
    case 0x37B7B3: buffer_lock(c); break;
    case 0x379F40: buffer_unlock(c); break;
    case 0x37C5C8: case 0x37B66F: case 0x37B6A7: buffer_control(c, ip); break;
    case 0x37B5AE: case 0x37B5CA: query(c, ip); break;
    case 0x37D506: case 0x37D5CD: case 0x37D52A: scalar(c, ip); break;
    case 0x37B637: mix_bin(c, ip); break;
    case 0x37C5E4: case 0x37B6C3: buffer_routing(c, ip); break;
    case 0x37C6E5: fx_deferred_parameters(c); break;
    case 0x37B6DF: buffer_play(c); break;
    case 0x37B703: buffer_stop(c); break;
    case 0x37B75B: buffer_status(c); break;
    case 0x37B797: buffer_rewind(c); break;
    case 0x37B777: buffer_cursor(c); break;
#if H2_AUDIO_DSP
    case 0x37B86D: effects_download(c); break;
    case 0x37B5E6: effects_query(c); break;
#elif H2_AUDIO_EFFECTS_UNAVAILABLE
    case 0x37B86D: effects_unavailable(c); break;
#endif
    default: fail(c, ip, "unimplemented adapter", ip);
    }
    h2_platform_fpscr_write(fpscr);
}
/* Small validated ranges only. Include physical aliases across distinct guest
 * pages when checking the original writer's table/control/stack footprint. */
static int aliases(uint32_t a, unsigned an, uint32_t b, unsigned bn)
{
    for (unsigned i = 0; i < an; ++i) {
        uint32_t pa = g_xpt[(a + i) >> 12] + ((a + i) & 4095);
        for (unsigned j = 0; j < bn; ++j)
            if (pa == g_xpt[(b + j) >> 12] + ((b + j) & 4095)) return 1;
    }
    return 0;
}
void h2_audio_guest_entry(xctx *c, uint32_t ip)
{
    if (!device.ever_created) return;
    if (ip == 0x379F2A) {
        /* Audited public Release wrapper: the original code adjusts base+8
         * then invokes the existing common-header Release adapter. */
        stack(c, ip, 1); live(c, ip, X_ARG(0), 0);
        uint32_t caller = X_M32(c->r[4]);
        if ((caller != 0x21EB91 && caller != 0x3E3D19 && caller != 0x3E39FD) || !mapped(0x417128, 4) || X_M32(0x417128) != 0x37C70F)
            fail(c, ip, "original sound release caller/vtable", X_M32(c->r[4]));
        if (c->r[4] < 16) fail(c, ip, "original sound release stack", c->r[4]);
        output(c, ip, c->r[4] - 16, 24);
        return;
    }
    uint32_t caller;
    switch (ip) {
    case 0x379F5B: caller = 0x21E604; break;
    case 0x379E9E: caller = 0x379F61; break;
    case 0x37E126: caller = 0x379F69; break;
    default: fail(c, ip, "unsupported original DSOUND method", ip);
    }
    uint32_t fpscr = h2_platform_fpscr_read();
    stack(c, ip, 0);
    if (X_M32(c->r[4]) != caller) fail(c, ip, "original sound configuration caller", X_M32(c->r[4]));
    live(c, ip, device.base + 8, 0);
    if (c->r[4] < 32 || c->fs_base > UINT32_MAX - 0x24 || !mapped(c->fs_base + 0x24, 1))
        fail(c, ip, "original sound configuration control", c->fs_base);
    /* Original code pushes at most 16 further bytes. Reserve/check 32 before
     * permitting it to run; this guard never initializes or writes them. */
    uint32_t low = c->r[4] - 32, irql = c->fs_base + 0x24;
    output(c, ip, low, 36); output(c, ip, 0x386B18, 28); output(c, ip, 0x3871C8, 44);
    if (aliases(0x3871C8, 44, 0x386B18, 28) || aliases(0x3871C8, 44, low, 36) ||
        aliases(0x386B18, 28, low, 36) || aliases(0x3871C8, 44, irql, 1) ||
        aliases(0x386B18, 28, irql, 1) || aliases(low, 36, irql, 1))
        fail(c, ip, "original sound configuration alias", ip);
    if (ip == 0x379F5B)
        xv_logf("[h2/audio] executing original LightHRTF4Channel configuration caller=%08X irql=%u\n",
                caller, X_M8(irql));
    h2_platform_fpscr_write(fpscr);
}
void h2_audio_host_snapshot(h2_audio_device_snapshot *out) { *out = device; }
void h2_audio_trace_buffer(xctx *c, uint32_t ip)
{
    /* Terminal read-only probes; never repair guest inputs or resume them. */
    if (ip == 0x37B68B && !(c->r[4] & 3) && mapped(c->r[4], 12)) {
        uint32_t address = X_M32(c->r[4] + 8), fields[6];
        if (mapped(address, sizeof fields)) {
            x_guest_read(fields, address, sizeof fields);
            xv_logf("[h2/audio-filter] entry=%08X caller=%08X interface=%08X descriptor=%08X words=%08X,%08X,%08X,%08X,%08X,%08X\n",
                    ip, X_M32(c->r[4]), X_M32(c->r[4] + 4), address, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]);
        }
    }

    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) {
        const h2_audio_buffer *b = &buffers[i];
        if (b->base)
            xv_logf("[h2/audio-buffer] stop interface=%08X refs=%u voice=%d external=%08X bytes=%u mirror=%08X frequency=%u volume=%d headroom=%u\n",
                    b->base + 0x1C, b->references, b->voice, b->source, b->bytes, b->mirror,
                    b->frequency, b->volume, b->headroom);
        if (b->base)
            xv_logf("[h2/audio-buffer] stop locked=%u offset=%u first=%u second=%u commits=%u committed_bytes=%llu started=%u stopped=%u rewound=%u cursor_queries=%u\n",
                    b->locked, b->lock_offset, b->lock_first, b->lock_second, b->commits,
                    (unsigned long long)b->committed_bytes, b->started, b->stopped, b->rewound, b->cursor_queries);
    }
    if (ip == 0x37B7B3 && !(c->r[4] & 3) && mapped(c->r[4], 36)) {
        xv_logf("[h2/audio-buffer] Lock caller=%08X interface=%08X offset=%u bytes=%u pointer1=%08X length1=%08X pointer2=%08X length2=%08X flags=%08X\n",
                X_M32(c->r[4]), X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), X_ARG(4), X_ARG(5), X_ARG(6), X_ARG(7));
        return;
    }
    if ((ip == 0x37C5E4 || ip == 0x37B6C3) && !(c->r[4] & 3) && mapped(c->r[4], 12)) {
        uint32_t address = X_ARG(1), list[2];
        if (!mapped(address, 8)) return;
        x_guest_read(list, address, 8);
        xv_logf("[h2/audio-buffer] route entry=%08X caller=%08X list=%08X count=%u entries=%08X\n",
                ip, X_M32(c->r[4]), address, list[0], list[1]);
        if (!list[0] || list[0] > 8 || !mapped(list[1], list[0] * 8)) return;
        uint32_t pairs[16]; x_guest_read(pairs, list[1], list[0] * 8);
        for (unsigned i = 0; i < list[0]; ++i)
            xv_logf("[h2/audio-buffer] route[%u] bin=%u volume=%d\n", i, pairs[i*2], (int32_t)pairs[i*2+1]);
        return;
    }
    if ((ip != 0x37D4BE && ip != 0x37D4E2) || !mapped(c->r[4], 20) || (c->r[4] & 3)) return;
    uint32_t desc = X_ARG(1), fields[6];
    if (!mapped(desc, sizeof fields)) return;
    x_guest_read(fields, desc, sizeof fields);
    uint32_t wfx;
    if (ip == 0x37D4E2) {
        xv_logf("[h2/audio-stream] descriptor=%08X flags=%08X packets=%u format=%08X callback=%08X context=%08X mixbins=%08X output=%08X outer=%08X\n",
                desc, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], X_ARG(2), X_ARG(3));
        wfx = fields[2];
    } else {
        xv_logf("[h2/audio-buffer] descriptor=%08X size=%08X flags=%08X bytes=%08X format=%08X mixbins=%08X inputbin=%08X output=%08X outer=%08X\n",
                desc, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], X_ARG(2), X_ARG(3));
        wfx = fields[3];
    }
    uint8_t format[18];
    if (!mapped(wfx, sizeof format)) return;
    x_guest_read(format, wfx, sizeof format);
    uint16_t tag, channels, align, bits, extra; uint32_t rate, average;
    memcpy(&tag, format, 2); memcpy(&channels, format + 2, 2);
    memcpy(&rate, format + 4, 4); memcpy(&average, format + 8, 4);
    memcpy(&align, format + 12, 2); memcpy(&bits, format + 14, 2); memcpy(&extra, format + 16, 2);
    xv_logf("[h2/audio-format] entry=%08X wave tag=%u channels=%u rate=%u average=%u align=%u bits=%u extra=%u\n",
            ip, tag, channels, rate, average, align, bits, extra);
    if (tag == 0x69 && extra == 2 && mapped(wfx, 20)) {
        uint16_t samples; x_guest_read(&samples, wfx + 18, 2);
        xv_logf("[h2/audio-format] Xbox ADPCM samples_per_block=%u\n", samples);
    }
}
