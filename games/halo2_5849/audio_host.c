/* Checked XDK5849 device and external PCM buffer boundaries. Unknown sound
 * methods stop; every accepted object owns real mixer/output resources. */
#include "audio_host.h"
#include "recomp/kernel/xk.h"
#include "recomp/kernel/xk_audio.h"
#include <string.h>
#include <stdlib.h>
#if H2_AUDIO_DSP
#include "dsp_asset.h"
static h2_dsp_engine *effects;
static uint32_t effects_guest, effects_guest_bytes;
static struct { xctx *context; uint32_t arena; } reverb_conversion;
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
    uint32_t filter[6];
    uint32_t gp_pcm; /* observed mono8/1000Hz buffer, fully muted route14 */
} h2_audio_buffer;
static h2_audio_buffer buffers[XA_MAX_VOICES];
typedef struct {
    uint32_t base, references, callback, context, packet_limit, headroom;
    uint32_t flags, route_bin;
    uint32_t route_count, route_bins[5];
    int32_t route_gains[5];
    struct { uint32_t mirror, source, context, ready; uint64_t ticket; } packets[2];
    uint32_t submitted, completed;
    int voice;
} h2_audio_stream;
static h2_audio_stream streams[XA_MAX_VOICES];
#if H2_AUDIO_DSP
static xk_thread *stream_worker;
#endif
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
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) {
        if (page_overlap(address, bytes, streams[i].base, 4096)) return 1;
        for (unsigned p=0;p<2;++p) if (page_overlap(address,bytes,streams[i].packets[p].mirror,4096)) return 1;
    }
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
        uint32_t base = xk_mem_alloc_high(4096, 4096);
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
    if (b->gp_pcm && ip != 0x37A14F && ip != 0x379F45 && ip != 0x37A795 &&
        ip != 0x37CC4A && ip != 0x37B6A7 && ip != 0x37B66F && ip != 0x37B6DF)
        fail(c, ip, "unsupported routed low-rate PCM method", b->base);
    if (b->submix == 1 && ip != 0x37D4BE && ip != 0x37A14F && ip != 0x379F45 &&
        ip != 0x37A795 && ip != 0x37C5E4 && ip != 0x37C620 && ip != 0x37C644 &&
        ip != 0x37C6C1 && ip != 0x37C69D && ip != 0x37C600)
        fail(c, ip, "submix activation/data/spatial processing is unsupported", b->base);
    if (b->submix == 2 && ip != 0x37A14F && ip != 0x379F45 && ip != 0x37A795 &&
        ip != 0x37B66F && ip != 0x37C5E4 && ip != 0x37B6DF &&
        ip != 0x37C620 && ip != 0x37C644 && ip != 0x37C6E5 && ip != 0x37B68B)
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
    uint32_t base = xk_mem_alloc_high(8192, 4096);
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
    if (!((caller == 0x220C26 && bin == 13) || (caller == 0x21E830 && bin >= 23 && bin <= 25) || (caller == 0x21E9E0 && bin >= 15 && bin <= 22) || spatial) ||
        fields[0] != 24 || fields[1] != (spatial ? 0x100010u : 0x100000u) || fields[2] || fields[3] || fields[4] || !effects)
        fail(c, ip, "unsupported FXIN2 description/caller", fields[1]);
    if (aliases(out, 4, c->r[4], 20) || aliases(out, 4, desc, 24))
        fail(c, ip, "FXIN2 output alias", out);
    if (device.references == UINT32_MAX) fail(c, ip, "FXIN2 parent overflow", device.references);
    uint32_t predecessor = spatial ? bin : bin >= 15 && bin <= 22 ? (bin == 15 ? H2_FX_SPATIAL25 : bin - 1) : bin == 23 ? 13 : (0x10000u | (bin - 1));
    int predecessor_playing = 0;
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) if (buffers[i].base && buffers[i].submix == 2) {
        if (buffers[i].fx_bin == source_key) fail(c, ip, "duplicate FXIN2 source", bin);
        if (buffers[i].fx_bin == predecessor && buffers[i].started && !buffers[i].stopped) predecessor_playing = 1;
    }
    if (bin != 13 && !predecessor_playing) fail(c, ip, "FXIN2 loop predecessor must be active", source_key);
    unsigned index;
    for (index = 0; index < XA_MAX_VOICES && buffers[index].base; ++index) {}
    if (index == XA_MAX_VOICES) { result(c, 0x8007000E, 4); return; }
    uint32_t base = xk_mem_alloc_high(4096, 4096);
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
    uint32_t base = xk_mem_alloc_high(4096, 4096);
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
static void global_buffer_create(xctx *c)
{
    const uint32_t ip = 0x37D7DE;
    stack(c,ip,2); live(c,ip,device.base,1); buffer_operational(c,ip);
#if H2_AUDIO_DSP
    uint32_t desc=X_ARG(0),out=X_ARG(1),fields[6],list[2],pair[2];
    uint8_t format[18];
    const uint8_t expected[18]={1,0,1,0,0xE8,3,0,0,0xE8,3,0,0,1,0,8,0,0,0};
    output(c,ip,out,4);
    if (!effects || X_M32(c->r[4]) != 0x22153E || !mapped(desc,24))
        fail(c,ip,"global PCM caller/descriptor",desc);
    x_guest_read(fields,desc,24);
    if (fields[0]!=24 || fields[1] || fields[2] || fields[5] || !mapped(fields[3],18) || !mapped(fields[4],8))
        fail(c,ip,"unsupported global PCM descriptor",desc);
    x_guest_read(format,fields[3],18); x_guest_read(list,fields[4],8);
    if (memcmp(format,expected,18) || list[0]!=1 || !mapped(list[1],8))
        fail(c,ip,"unsupported global PCM format/route list",fields[3]);
    x_guest_read(pair,list[1],8);
    if (pair[0]!=14 || pair[1]) fail(c,ip,"unsupported global PCM route/gain",list[1]);
    const uint32_t addresses[5]={c->r[4],desc,fields[3],fields[4],list[1]};
    const unsigned lengths[5]={12,24,18,8,8};
    for (unsigned i=0;i<5;++i) if (aliases(out,4,addresses[i],lengths[i])) fail(c,ip,"global PCM output alias",out);
    unsigned fx_sources=0,pcm_sources=0,index;
    for (unsigned i=0;i<XA_MAX_VOICES;++i) if (buffers[i].base) {
        if (buffers[i].submix==2 && buffers[i].started && !buffers[i].stopped) ++fx_sources;
        if (buffers[i].gp_pcm) {
            ++pcm_sources;
            if (!buffers[i].started || buffers[i].stopped) fail(c,ip,"global PCM predecessor is inactive",buffers[i].base);
        }
    }
    if (fx_sources!=H2_FX_SOURCES || pcm_sources>=2 || device.references>UINT32_MAX-2)
        fail(c,ip,"global PCM owner sequence",pcm_sources);
    for (index=0;index<XA_MAX_VOICES && buffers[index].base;++index) {}
    if (index==XA_MAX_VOICES) { result(c,0x8007000E,2); return; }
    uint32_t base=xk_mem_alloc_high(4096,4096);
    if (!base) { result(c,0x8007000E,2); return; }
    if ((base&4095) || !mapped(base,4096) || overlaps_device(base,4096) || page_overlap(base,4096,out,4))
        fail(c,ip,"global PCM allocation mapping",base);
    for (unsigned i=0;i<5;++i) if (page_overlap(base,4096,addresses[i],lengths[i])) fail(c,ip,"global PCM allocation alias",base);
    x_guest_write(base+64,format,18);
    int voice=xk_audio_voice_new(1,base+64);
    if (voice<0) {
        if (xk_mem_free(base)<0) fail(c,ip,"global PCM allocation rollback",base);
        result(c,0x8007000E,2); return;
    }
    /* Shared parsing limits nominal rates to >=4kHz. Its existing frequency
     * override supports1000Hz and drives the actual decoder/resampler. */
    xk_audio_lock(); xk_audio_voice_set_frequency(voice,1000); xk_audio_voice_set_volume_db100(voice,-600); xk_audio_unlock();
    buffers[index]=(h2_audio_buffer){.base=base,.references=1,.voice=voice,.frequency=1000,
        .headroom=600,.route_count=1,.route_bins={14},.gp_pcm=1};
    X_M32(base)=0x417150; X_M32(base+4)=1;
    ++device.children; X_M32(device.base+4)=++device.references;
    uint32_t handle=base+0x1C; x_guest_write(out,&handle,4);
    /* The original global wrapper takes/releases a temporary device ref.
     * On return only the real child's reference survives, including failure. */
    xv_logf("[h2/audio-global] create caller=0022153E interface=%08X real voice=%d PCM8 mono effective1000Hz route14 headroom600 parent_refs=%u inactive; only fully muted GP14 playback is supported\n",handle,voice,device.references);
    result(c,0,2);
#else
    fail(c,ip,"global routed PCM requires real DSP",ip);
#endif
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
static void stream_create(xctx *c, uint32_t ip)
{
    int global = ip == 0x37D835;
    unsigned args = global ? 2 : 4, stack_bytes = (args+1)*4;
    stack(c, ip, args); live(c, ip, global ? device.base+8 : X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t desc = X_ARG(global ? 0 : 1), out = X_ARG(global ? 1 : 2), fields[6];
    output(c, ip, out, 4);
    if (X_M32(c->r[4]) != (global ? 0x335ED8u : 0x2AE692u) || (!global && X_ARG(3)) || !mapped(desc, sizeof fields))
        fail(c, ip, "stream caller/descriptor", desc);
    x_guest_read(fields, desc, sizeof fields);
    if (fields[0] != (global ? 0x40000000u : 0x20000000u) || fields[1] != 2 ||
        (global ? (fields[3]!=0x335D82u && fields[3]!=0x335D99u) : fields[3]!=0x220730u) || (!global && fields[5]))
        fail(c, ip, "unsupported stream description", fields[0]);
    uint32_t routes[2]={0}, pair[10]={0};
    if (global) {
        if (!mapped(fields[4],28) || (uint64_t)fields[4]+24!=out || !mapped(fields[5],8))
            fail(c,ip,"global stream context/route mapping",fields[4]);
        x_guest_read(routes,fields[5],8);
        if ((routes[0]!=1 && routes[0]!=5) || !mapped(routes[1],routes[0]*8)) fail(c,ip,"global stream route list",fields[5]);
        x_guest_read(pair,routes[1],routes[0]*8);
        if (routes[0]==1) {
            if (pair[0]<27 || pair[0]>30 || pair[1]) fail(c,ip,"unsupported global stream route",pair[0]);
        } else {
            /* Original 333890: four DSP descriptor bins and center, all muted.
             * Only empty ownership is supported; Process remains a strict stop. */
            if(fields[3]!=0x335D82)fail(c,ip,"muted stream callback",fields[3]);
            for(unsigned i=0;i<5;++i)
                if(pair[i*2]!=(i<4?27+i:2) || pair[i*2+1]!=(uint32_t)-10000)
                    fail(c,ip,"unsupported muted global stream route",pair[i*2]);
        }
        if (aliases(out,4,fields[5],8) || aliases(out,4,routes[1],routes[0]*8))
            fail(c,ip,"global stream route output alias",out);
    }
    uint8_t format[20] = {0};
    if (!mapped(fields[2], 18)) fail(c, ip, "stream format mapping", fields[2]);
    x_guest_read(format, fields[2], 18);
    uint32_t format_bytes = format[0] == 0x69 ? 20 : 18;
    if (!mapped(fields[2], format_bytes)) fail(c, ip, "stream format extension", fields[2]);
    x_guest_read(format, fields[2], format_bytes);
    /* Original 21E410 builds the first three formats; 335DB0 builds mono8k.
     * Reject all others before the shared mixer's permissive format parser. */
    const uint8_t formats[4][20] = {
        {0x69,0,1,0,0x44,0xAC,0,0,0xE4,0x60,0,0,36,0,4,0,2,0,64,0},
        {0x69,0,2,0,0x44,0xAC,0,0,0xC8,0xC1,0,0,72,0,4,0,2,0,64,0},
        {1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,0,0,0,0},
        {1,0,1,0,0x40,0x1F,0,0,0x80,0x3E,0,0,2,0,16,0,0,0,0,0}
    };
    unsigned kind;
    for (kind = global ? 3 : 0; kind < (global ? 4u : 3u) && memcmp(format, formats[kind], 20); ++kind) {}
    if (kind == (global ? 4u : 3u)) fail(c, ip, "unsupported stream format", fields[2]);
    if (aliases(out, 4, c->r[4], stack_bytes) || aliases(out, 4, desc, 24) || aliases(out, 4, fields[2], format_bytes))
        fail(c, ip, "stream output alias", out);
    if (device.references == UINT32_MAX) fail(c, ip, "stream parent reference overflow", device.references);
    unsigned index;
    for (index = 0; index < XA_MAX_VOICES && streams[index].base; ++index) {}
    if (index == XA_MAX_VOICES) { result(c, 0x8007000E, args); return; }
    uint32_t base = xk_mem_alloc_high(4096, 4096);
    if (!base) { result(c, 0x8007000E, args); return; }
    if ((base & 4095) || !mapped(base, 4096) || overlaps_device(base, 4096) ||
        page_overlap(base, 4096, c->r[4], stack_bytes) || page_overlap(base, 4096, out, 4) ||
        page_overlap(base, 4096, desc, 24) || page_overlap(base, 4096, fields[2], format_bytes) ||
        (global && (page_overlap(base,4096,fields[4],28) || page_overlap(base,4096,fields[5],8) || page_overlap(base,4096,routes[1],routes[0]*8))))
        fail(c, ip, "stream allocation mapping/alias", base);
    x_guest_write(base + 64, format, 20);
    int voice = xk_audio_voice_new(2, base + 64);
    if (voice < 0) {
        if (xk_mem_free(base) < 0) fail(c, ip, "stream allocation rollback", base);
        result(c, 0x8007000E, args); return;
    }
    xk_audio_lock(); xk_audio_voice_set_volume_db100(voice, routes[0]==5 ? -10600 : -600); xk_audio_unlock();
    streams[index] = (h2_audio_stream){.base=base,.references=1,.callback=fields[3],.context=fields[4],
        .packet_limit=fields[1],.headroom=600,.flags=fields[0],.route_bin=global && routes[0]==1 ? pair[0] : UINT32_MAX,.voice=voice};
    streams[index].route_count=routes[0];
    for(unsigned i=0;i<routes[0];++i){streams[index].route_bins[i]=pair[i*2];streams[index].route_gains[i]=(int32_t)pair[i*2+1];}
    X_M32(base) = 0x417170; X_M32(base + 4) = 0x417160; X_M32(base + 8) = 1;
    ++device.children; X_M32(device.base + 4) = ++device.references;
    x_guest_write(out, &base, 4);
    xv_logf("[h2/audio-stream] create caller=%08X object=%08X voice=%u format=%u channels=%u packet_limit=2 callback=%08X context=%u parent_refs=%u; empty real mixer voice, packet/DSP routing unsupported\n",
            X_M32(c->r[4]), base, voice, kind, format[2], fields[3], fields[4], device.references);
    if (global) xv_logf("[h2/audio-stream] global mono16/8000Hz accurate-notify retained; only checked zero packets and completed-sink notifications supported\n");
    if (routes[0]==5) xv_logf("[h2/audio-stream] five exact muted routes retained, real inactive voice gain zero; Process unsupported\n");
    if (fields[3]==0x335D99) xv_logf("[h2/audio-stream] alternate original callback retained for empty ownership; Process unsupported\n");
    result(c, 0, args);
}
#if H2_AUDIO_DSP
/* Xbox DirectSound invokes stream packet-completion callbacks from
 * DirectSoundDoWork on the calling thread, not asynchronously. The worker only
 * observes the real sink fence and marks packets ready; retirement and the
 * guest callback happen when the game calls DirectSoundDoWork. Native222 showed
 * why this matters: at the intro-to-menu transition the game frees its stream
 * context records and reuses the memory before any Flush/Release, which is safe
 * on hardware only because no callback can run until it services DirectSound. */
static void stream_poll(xctx *c)
{
    for (unsigned i=0;i<XA_MAX_VOICES;++i) {
        h2_audio_stream *s=&streams[i];if (!s->base || !s->submitted) continue;
        for (unsigned n=0;n<2;++n) {
            uint64_t ticket;int ready=h2_audio_backend_stream_complete(s->voice,&ticket);
            if (ready<0) fail(c,0x37AD25,"stream sink completion failed",s->base);
            if (!ready) break;
            unsigned p;for(p=0;p<2 && s->packets[p].ticket!=ticket;++p) {}
            if (p==2 || !s->packets[p].mirror || s->packets[p].ready)
                fail(c,0x37AD25,"stream completion ownership",s->base);
            s->packets[p].ready=1;
            xv_logf("[h2/audio-packet] sink completed ticket=%llu stream=%08X context=%u; callback deferred to DirectSoundDoWork\n",
                    (unsigned long long)ticket,s->base,s->packets[p].context);
        }
    }
}
static void stream_deliver(xctx *c, uint32_t ip)
{
    for (unsigned i=0;i<XA_MAX_VOICES;++i) {
        h2_audio_stream *s=&streams[i];if (!s->base) continue;
        for (unsigned n=0;n<2;++n) {
            /* Deliver in ticket order. */
            unsigned p=2;
            for (unsigned q=0;q<2;++q)
                if (s->packets[q].ready && (p==2 || s->packets[q].ticket<s->packets[p].ticket)) p=q;
            if (p==2) break;
            if (!s->packets[p].mirror || !mapped(s->packets[p].mirror,4096) ||
                s->callback!=0x335D82 || !mapped(s->context,0x48) || c->r[4]<16 ||
                !mapped(c->r[4]-16,16) || c->fs_base>UINT32_MAX-0x24 || !mapped(c->fs_base+0x24,1))
                fail(c,ip,"stream callback ownership/stack",s->base);
            uint32_t packet_context=s->packets[p].context,mirror=s->packets[p].mirror;
            uint64_t ticket=s->packets[p].ticket;
            output(c,ip,c->r[4]-16,16);output(c,ip,c->fs_base+0x24,1);
            if (X_M8(c->fs_base+0x24)>1 || aliases(c->r[4]-16,16,c->fs_base+0x24,1) ||
                aliases(c->r[4]-16,16,s->context,0x48))
                fail(c,ip,"stream callback control alias/IRQL",c->fs_base);
            if (xk_mem_free(mirror)<0) fail(c,ip,"stream mirror retirement",mirror);
            s->packets[p].mirror=0;s->packets[p].ticket=0;s->packets[p].ready=0;++s->completed;
            xctx saved=*c;uint32_t fpscr=h2_platform_fpscr_read();uint8_t irql=X_M8(c->fs_base+0x24);
            X_M8(c->fs_base+0x24)=2;c->preempt=0x7fffffff;
            X_PUSH32(0);X_PUSH32(packet_context);X_PUSH32(s->context);X_PUSH32(0xDEAD0003u);
            xv_logf("[h2/audio-packet] consumed ticket=%llu stream=%08X callback=%08X context=%u; delivered from DirectSoundDoWork after the real sink fence\n",
                    (unsigned long long)ticket,s->base,s->callback,packet_context);
            xv_call(c,s->callback);
            if (c->r[4]!=saved.r[4]) fail(c,ip,"stream callback stack imbalance",c->r[4]);
            X_M8(saved.fs_base+0x24)=irql;*c=saved;h2_platform_fpscr_write(fpscr);
        }
    }
}
static void stream_worker_entry(xctx *c,void *unused)
{
    (void)unused;
    for (;;) { stream_poll(c);xk_sleep_us(1000); }
}
static void stream_process(xctx *c)
{
    const uint32_t ip=0x37AD25;stack(c,ip,3);
    h2_audio_stream *s=stream_live(c,ip,X_ARG(0));uint32_t address=X_ARG(1),packet[6],caller=X_M32(c->r[4]);
    if (!effects || s->flags!=0x40000000 || s->route_count!=1 || (s->route_bin<27 || s->route_bin>30) || s->callback!=0x335D82 || s->headroom ||
        (caller!=0x33610E && caller!=0x335D7B) || X_ARG(2) || !mapped(address,sizeof packet)) {
        /* Menu bring-up: the menu submits its own audio stream via the standard DSound vtable
         * (caller 0x2AE89A) with a state this GP-routed handler was not built for. The original caller
         * checks the HRESULT (setge) and takes its failure path (exits the submit loop, continues the
         * menu build) - so return a real submission FAILURE the game handles, rather than strict-stopping.
         * Menu audio is silent for now; correct routing of the menu stream is a later audio step. */
        static int menu=-1; if(menu<0){const char*e=getenv("XV_MENU_VBLANK");menu=e?atoi(e):0;}
        if (menu) { xv_logf("[h2/audio-menu] stream Process unsupported (caller %08X flags %08X cb %08X route %u) -> DSERR, game handles\n",
                            caller, s->flags, s->callback, s->route_bin); result(c,0x8007000E,3); return; }
        fail(c,ip,"unsupported stream Process state/caller",caller);
    }
    x_guest_read(packet,address,sizeof packet);
    if (packet[1]!=320 || packet[2] || packet[3] || packet[4]>1 || packet[5] ||
        (packet[0]&1) || !mapped(packet[0],320) || overlaps_device(packet[0],320))
        fail(c,ip,"unsupported stream packet",address);
    unsigned p=packet[4];
    if (s->packets[p].mirror) {result(c,0x88780032,3);return;}
    if ((s->submitted<2 && (caller!=0x33610E || p!=s->submitted)) ||
        (s->submitted>=2 && caller!=0x335D7B) ||
        (s->packets[p].source && s->packets[p].source!=packet[0]))
        fail(c,ip,"stream packet caller/order/source",packet[0]);
    uint8_t samples[320];x_guest_read(samples,packet[0],sizeof samples);
    for (unsigned i=0;i<sizeof samples;++i) if (samples[i])
        fail(c,ip,"nonzero routed stream input remains unsupported",packet[0]+i);
    uint32_t mirror=xk_mem_alloc_high(4096,4096);
    if (!mirror) {result(c,0x8007000E,3);return;}
    if ((mirror&4095) || !mapped(mirror,4096) || overlaps_device(mirror,4096) ||
        page_overlap(mirror,4096,c->r[4],16) || page_overlap(mirror,4096,address,24) ||
        page_overlap(mirror,4096,packet[0],320) || page_overlap(mirror,4096,s->context,0x48))
        fail(c,ip,"stream packet mirror allocation alias",mirror);
    x_guest_write(mirror,samples,sizeof samples);
    if (!stream_worker) stream_worker=xk_thread_create_host(stream_worker_entry,NULL);
    if (!stream_worker) {
        if (xk_mem_free(mirror)<0) fail(c,ip,"stream worker rollback",mirror);
        result(c,0x8007000E,3);return;
    }
    uint64_t ticket;
    if (h2_audio_backend_stream_submit(s->voice,mirror,&ticket)<0) {
        if (xk_mem_free(mirror)<0) fail(c,ip,"stream submission rollback",mirror);
        fail(c,ip,"stream real decoder/GP submission rejected",s->base);
    }
    s->packets[p].mirror=mirror;s->packets[p].source=packet[0];s->packets[p].context=p;s->packets[p].ticket=ticket;s->packets[p].ready=0;
    ++s->submitted;xk_thread_kick(stream_worker);
    xv_logf("[h2/audio-packet] Process caller=%08X stream=%08X context=%u source=%08X mirror=%08X ticket=%llu;320 verified zero PCM bytes queued to real decoder/GP%u, completion pending\n",
            caller,s->base,p,packet[0],mirror,(unsigned long long)ticket,s->route_bin);
    result(c,0,3);
}
#endif
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
        if (s->submitted) fail(c,ip,"stream release after Process needs audited flush",s->base);
        /* Other supported streams are empty.
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
    xk_audio_lock(); xk_audio_voice_set_volume_db100(s->voice, s->route_count==5 ? -10000 : 0); xk_audio_unlock();
    s->headroom = 0;
    xv_logf("[h2/audio-stream] headroom object=%08X caller=%08X headroom=0 retained_route_mute=%u\n", s->base, X_M32(c->r[4]),s->route_count==5);
    result(c, 0, 2);
}
static void buffer_data(xctx *c)
{
    const uint32_t ip = 0x37CC4A;
    stack(c, ip, 3); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t source = X_ARG(1), bytes = X_ARG(2);
    if (b->gp_pcm && (X_M32(c->r[4])!=0x221592 || bytes!=1000 || b->started))
        fail(c,ip,"unsupported global PCM data binding",source);
    /* First bind only in this milestone. Playback and live rebinding remain
     * strict stops until their routing/commit/cursor contracts are supplied. */
    if (b->mirror || !bytes || bytes > 0x100000 || (bytes & 3) || (source & 3) ||
        !mapped(source, bytes) || overlaps_device(source, bytes))
        fail(c, ip, "unsupported external PCM binding", source);
    uint32_t size = (bytes + 4095) & ~4095u;
    uint32_t mirror = xk_mem_alloc_high(size, 4096);
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
    if (b->gp_pcm && (b->started ||
        !((ip==0x37B6A7 && !value && X_M32(c->r[4])==0x22159D) ||
          (ip==0x37B66F && value==(uint32_t)-10000 && !b->headroom && X_M32(c->r[4])==0x2215AC))))
        fail(c,ip,"unsupported global PCM control/state",value);
    if (b->submix == 2) {
#if H2_AUDIO_DSP
        uint32_t caller = X_M32(c->r[4]);
        if (ip==0x37B66F && caller==0x21F1A0 && b->fx_bin>=15 && b->fx_bin<=22) {
            /* Original zone loop 0x21F069: 2000*log10(zone gain) clamped to
             * [-6400,0] per buffer. SetVolume (0x37A5D4) stores volume-headroom
             * and 0x381CE4/0x380B97 write each routed bin's attenuation
             * -(bin gain+volume)*64/100 saturated at FFF (1/64 dB; FFF mutes). */
            int32_t volume=(int32_t)value;
            if(volume>0 || volume<-6400 || !b->started || b->stopped || b->headroom ||
               b->route_count!=1 || b->route_bins[0]!=6+(b->fx_bin-15)%4 || b->route_gains[0] ||
               b->volume>0 || b->volume<-6400)fail(c,ip,"unsupported FX15..22 volume state/value",value);
            unsigned attenuation=(unsigned)(-volume)*64u/100u;
            if(attenuation>0xFFF)attenuation=0xFFF;
            if((attenuation==0xFFF ? h2_audio_backend_fx_mute(b->fx_bin)
                                   : h2_audio_backend_fx_attenuate(b->fx_bin,attenuation))<0)
                fail(c,ip,"FX15..22 mixer volume rejected",b->fx_bin);
            b->volume=volume;
            xv_logf("[h2/fxin2] caller=0021F1A0 interface=%08X bin=%u volume=%d original attenuation%03X; route/source/GP time retained\n",
                    b->base+0x1c,b->fx_bin,volume,attenuation);
            result(c,0,2);return;
        }
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
        int initial = !b->started && !b->stopped && !b->volume && !b->headroom &&
            ((b->fx_bin == 13 && X_M32(c->r[4]) == 0x220C37) ||
             (b->fx_bin >= 15 && b->fx_bin <= 22 && b->route_count == 2 && X_M32(c->r[4]) == 0x21E9EE));
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
            if (volume > 0 || (volume < -9000 && !(b->gp_pcm && volume == -10000))) fail(c, ip, "unsupported buffer volume", value);
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
        if (caller == 0x21EA1B && b->fx_bin >= 15 && b->fx_bin <= 22) {
            unsigned route = 6 + (b->fx_bin - 15) % 4;
            if (ip != 0x37C5E4 || b->started || b->stopped || b->volume || b->headroom ||
                list[0] != 1 || !mapped(list[1],8)) fail(c,ip,"unsupported FX15..22 route state/list",list[0]);
            x_guest_read(pairs,list[1],8);
            if (pairs[0] != route || pairs[1]) fail(c,ip,"unsupported FX15..22 route/gain",list[1]);
            if (h2_audio_backend_fx_route_mask(b->fx_bin,1u << route) < 0) fail(c,ip,"FX15..22 real route binding rejected",b->fx_bin);
            b->route_count = 1; b->route_bins[0] = route; b->route_gains[0] = 0;
            xv_logf("[h2/fxin2] caller=0021EA1B interface=%08X input_bin=%u single unity GP route=%u; inactive\n",b->base+0x1C,b->fx_bin,route);
            result(c,0,2); return;
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
static void fx_filter(xctx *c)
{
    const uint32_t ip = 0x37B68B;
    stack(c, ip, 2); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t address = X_ARG(1), words[6], caller = X_M32(c->r[4]);
#if H2_AUDIO_FILTER_MODEL
    unsigned key = caller == 0x2AEFBC ? 23 : caller == 0x2AEFCD ? 24 : 0;
    if (!key || b->submix != 2 || b->fx_bin != key || !b->started || b->stopped ||
        b->volume || b->headroom || b->route_count != 4 ||
        b->route_bins[key == 23 ? 0 : 2] != (key == 23 ? 6u : 7u) ||
        b->route_gains[key == 23 ? 0 : 2] || !mapped(address, sizeof words))
        fail(c, ip, "unsupported FX low-pass object/caller/input", address);
    x_guest_read(words, address, sizeof words);
    const uint32_t supported[6] = {1,0,0,0x8000,0,0};
    if (memcmp(words, supported, sizeof words)) fail(c, ip, "unsupported FX filter coefficients", address);
    if (h2_audio_backend_fx_filter(key) < 0) fail(c, ip, "FX real filter binding rejected", key);
    memcpy(b->filter, words, sizeof words);
    xv_logf("[h2/fxin2] filter caller=%08X interface=%08X key=%u mode=1 cutoff=0 resonance=8000; active fixed low-pass model, history retained\n",
            caller, b->base + 0x1C, key);
    result(c, 0, 2);
#else
    (void)b; (void)words; (void)caller;
    fail(c, ip, "FX low-pass model disabled", address);
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
        if (b->fx_bin >= 15 && b->fx_bin <= 22) { routes = 1; caller = 0x21EA29; }
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
    if (b->gp_pcm) {
        if (!effects || X_M32(c->r[4])!=0x2215B9 || b->started || b->stopped || b->volume!=-10000 || b->headroom ||
            b->frequency!=1000 || b->bytes!=1000 || b->route_count!=1 || b->route_bins[0]!=14 || b->route_gains[0] ||
            !b->mirror || !mapped(b->mirror,b->mirror_bytes) || !mapped(b->source,b->bytes) ||
            X_ARG(1) || X_ARG(2) || X_ARG(3)!=1)
            fail(c,ip,"unsupported muted GP PCM Play state/flags",b->base);
        if (h2_audio_backend_gp_pcm_play(b->voice)<0) fail(c,ip,"muted PCM real GP/sink Play rejected",b->base);
        b->started=1;
        xv_logf("[h2/audio-global] Play caller=002215B9 interface=%08X voice=%d loop1000 bytes effective1000Hz; actual decoder advances, verified muted GP14 contribution, source-tagged sink grain accepted\n",b->base+0x1C,b->voice);
        result(c,0,4); return;
    }
    if (effects && ((X_M32(c->r[4])!=0x3E35DB && X_M32(c->r[4])!=0x3E3639) || b->bytes!=106496 || b->frequency!=44100 || b->volume || b->headroom))
        fail(c,ip,"unsupported loaded DSP movie Play",X_M32(c->r[4]));
#endif
    int repeat=b->started && !b->stopped && !b->rewound && X_M32(c->r[4])==0x3E3639;
    if(X_M32(c->r[4])==0x3E3639 && !repeat)fail(c,ip,"movie retry requires active loop",b->base);
    if (!b->mirror || (b->started && !b->rewound && !repeat) || b->locked || X_ARG(1) || X_ARG(2) || X_ARG(3) != 1 ||
        !mapped(b->mirror, b->mirror_bytes) || !mapped(b->source, b->bytes) || overlaps_device(b->source, b->bytes))
        fail(c, ip, "unsupported PCM Play state/flags", X_ARG(3));
    if(repeat){
        if(h2_audio_backend_repeat_play(b->voice,b->bytes,b->frequency)<0)fail(c,ip,"real PCM repeat Play rejected",b->voice);
        xv_logf("[h2/audio-buffer] repeated active looping Play caller=003E3639 voice=%d; decoder and sink ownership retained\n",b->voice);
        result(c,0,4);return;
    }
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
static void deferred_commit(xctx *c)
{
    const uint32_t ip=0x37D141;
    stack(c,ip,1);live(c,ip,X_ARG(0),0);buffer_operational(c,ip);
#if H2_AUDIO_DSP && H2_AUDIO_SPATIAL_MODEL && H2_AUDIO_FILTER_MODEL
    const uint32_t zero[3]={0},orientation[6]={0x3f800000,0,0,0,0x3f800000,0};
    if(X_M32(c->r[4])!=0x21F201 || !effects || !mapped(0x386B0C,4) || X_M32(0x386B0C) ||
       (device.dirty!=0x25 && device.dirty) || device.distance!=0x4043126f || device.rolloff ||
       (device.doppler!=0x3f800000 && device.doppler) || device.pending_distance!=0x4043126f ||
       device.pending_rolloff || device.pending_doppler ||
       memcmp(device.pending_position,zero,sizeof zero) || memcmp(device.pending_orientation,orientation,sizeof orientation))
        fail(c,ip,"unsupported fixed listener commit",device.dirty);
    h2_audio_buffer *active[3]={0};
    for(unsigned i=0;i<XA_MAX_VOICES;++i){
        h2_audio_buffer*b=&buffers[i];if(!b->base || !b->submix)continue;
        if(b->submix==1){
            /* Original registered inactive voice fails status&3==3 and
             * retains every pending spatial field. No activation here. */
            if(b->started || b->stopped)fail(c,ip,"unsupported active submix commit",b->base);
            continue;
        }
        if(b->submix!=2)fail(c,ip,"unsupported spatial owner class",b->base);
        if(!(b->fx_bin&0x10000))continue; /* Original registration excludes it. */
        unsigned bin=b->fx_bin&0xffff;
        if(bin<23 || bin>25 || active[bin-23] || b->fx_bin!=(0x10000u|bin) ||
           !b->started || b->stopped || b->locked || b->headroom || b->volume!=(bin==25?0:-6400) || b->route_count!=5)
            fail(c,ip,"unsupported fixed spatial voice",b->base);
        static const uint8_t routes[5]={6,8,7,9,10};
        if(memcmp(b->route_bins,routes,sizeof routes))fail(c,ip,"changed fixed spatial route",b->base);
        for(unsigned j=0;j<5;++j)if(b->route_gains[j]!=(bin==25&&j<4?-6400:0))
            fail(c,ip,"changed fixed spatial gain",b->base);
        uint32_t expected[41];spatial_defaults(expected);
        expected[0]=expected[0x7c/4]=0;expected[1]=0xf8000000;
        expected[0x38/4]=expected[0x3c/4]=0x7f7fffff;expected[0x70/4]=0x43340000;
        if(bin==25 && b->spatial[0x7c/4]==0x007f0000)expected[0x7c/4]=0x007f0000;
        if(memcmp(expected,b->spatial,sizeof expected))fail(c,ip,"changed fixed spatial parameters",b->base);
        active[bin-23]=b;
    }
    /* The currently supported stream descriptors never register as spatial. */
    for(unsigned i=0;i<XA_MAX_VOICES;++i)if(streams[i].base && (streams[i].flags&0x10))
        fail(c,ip,"unsupported spatial stream commit",streams[i].base);
    if(!active[0] || !active[1] || !active[2] || !h2_audio_backend_fixed_commit_ready(effects))
        fail(c,ip,"fixed commit live mixer configuration",device.base);
    /* Original calls for these exact records emit no hardware/DSP writes:
     * retain all actual mixer state and commit only the pending API fields. */
    active[2]->spatial[0x7c/4]=0;
    device.distance=device.pending_distance;device.rolloff=device.pending_rolloff;
    device.doppler=device.pending_doppler;device.dirty=0;
    xv_logf("[h2/audio-commit] caller=0021F201 fixed zero-position/+X+Y listener committed, Doppler0; original FX25 dirty cleared, inactive pending records and real filters/sources/GP history/grains retained\n");
    result(c,0,1);
#else
    fail(c,ip,"fixed spatial commit model disabled",ip);
#endif
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
    uint32_t base = xk_mem_alloc_high(bytes, 4096);
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
static void effects_write(xctx *c)
{
    const uint32_t ip = 0x37B60D;
    static const uint32_t callers[4] = {0x191294, 0x1912AC, 0x1912C3, 0x1912DB};
    stack(c, ip, 6); live(c, ip, X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t index = X_ARG(1), offset = X_ARG(2), source = X_ARG(3);
    if (!effects || !mapped(effects_guest, effects_guest_bytes)) fail(c, ip, "no initialized DSP image", index);
    if (index < 4 || index > 7 || X_M32(c->r[4]) != callers[index - 4] ||
        offset != 32 || X_ARG(4) != 8 || X_ARG(5))
        fail(c, ip, "unreviewed immediate effect write", index);
    if (!mapped(source, 8) || overlaps_device(source, 8) || aliases(source, 8, c->r[4], 28))
        fail(c, ip, "effect write source/alias", source);
    uint32_t words[2]; x_guest_read(words, source, sizeof words);
    if (!h2_audio_backend_effect_write_pair(effects, index, offset, words[0], words[1]))
        fail(c, ip, "immediate effect shadow/GP write rejected", index);
    xv_logf("[h2/dsp] SetEffectData caller=%08X index=%u offset=%u bytes=8 flags=0 words=%08X,%08X immediate shadow+GP write\n",
            X_M32(c->r[4]), index, offset, words[0], words[1]);
    result(c, 0, 6);
}
static int original_reverb_entry(xctx *c, uint32_t ip)
{
    if (!reverb_conversion.context || c!=reverb_conversion.context) return 0;
    static const uint32_t entries[]={0x379D4B,0x383167,0x3831B5,0x383243,0x38327C,
        0x38329A,0x3832D2,0x38336C,0x3833E9,0x383436,0x38357F,0x3835E0,0x3837BA,0x3838A4};
    int found=0;for(unsigned i=0;i<sizeof entries/sizeof *entries;++i)found|=ip==entries[i];
    uint32_t base=reverb_conversion.arena;
    if (!found || !base || !mapped(base,4096) || (c->r[4]&3) ||
        c->r[4]<base+0x800 || c->r[4]>base+0xf00 || c->df ||
        (ip==0x3838A4 && c->r[1]!=base+0x340))
        fail(c,ip,"original reverb conversion scope",ip);
    return 1;
}
static void effects_description(xctx *c)
{
    const uint32_t ip=0x37BA6F;
    stack(c,ip,3);live(c,ip,device.base+8,0);buffer_operational(c,ip);
    uint32_t index=X_ARG(0),source=X_ARG(1),words[13],prefix[70];
    /* The I3DL2 listener description from the game's sound system (caller 0x21EE74): the
     * startup default and, since the first multiplayer level (mp286: room -1300 mB, decay
     * 1.0 s), any environment preset. The 12 fields are validated against the documented
     * DSI3DL2LISTENER ranges; the original converter (0x3838A4) then runs unchanged under
     * the caller's own x87 control word / FPSCR, exactly as the rest of the recompiled
     * float code does, so only unmasked x87 exceptions remain refused. Other effect
     * types, raw-output requests and re-entrant conversions stay strict. */
    if (X_M32(c->r[4])!=0x21EE74 || (index!=8&&index!=9) || X_ARG(2) || !effects ||
        reverb_conversion.context || !mapped(effects_guest,effects_guest_bytes) ||
        !mapped(source,sizeof words) || overlaps_device(source,sizeof words) ||
        aliases(source,sizeof words,c->r[4],16) || c->df || (c->fcw&0x3f)!=0x3f) {
        uint32_t fp=h2_platform_fpscr_read(); /* the diagnostic leaves the caller's FP state intact */
        xv_logf("[h2/reverb] refused caller=%08X index=%u arg2=%08X effects=%d nested=%d effects_mapped=%d source_mapped=%d device_overlap=%d alias=%d df=%u fsp=%u fcw=%04X\n",
                X_M32(c->r[4]), index, X_ARG(2), effects!=NULL, reverb_conversion.context!=NULL,
                mapped(effects_guest,effects_guest_bytes), mapped(source,sizeof words), overlaps_device(source,sizeof words),
                aliases(source,sizeof words,c->r[4],16), (unsigned)c->df, (unsigned)c->fsp, (unsigned)c->fcw);
        h2_platform_fpscr_write(fp);
        fail(c,ip,"unreviewed reverb description/caller/control",source);
    }
    /* A live x87 stack at the call (the level's sound code computes the environment
     * parameters in float registers) is the caller's state on hardware too; the
     * conversion below is required to leave it exactly as found. */
    x_guest_read(words,source,sizeof words);
    {
        float f[13]; memcpy(f,words,sizeof f);
        int32_t room=(int32_t)words[1], room_hf=(int32_t)words[2], reflections=(int32_t)words[6], reverb=(int32_t)words[8];
        int ranges = words[0]==12 &&
            room>=-10000 && room<=0 && room_hf>=-10000 && room_hf<=0 &&
            f[3]>=0.0f && f[3]<=10.0f &&          /* room rolloff factor */
            f[4]>=0.1f && f[4]<=20.0f &&          /* decay time (s) */
            f[5]>=0.1f && f[5]<=2.0f &&           /* decay HF ratio */
            reflections>=-10000 && reflections<=1000 && f[7]>=0.0f && f[7]<=0.3f &&
            reverb>=-10000 && reverb<=2000 && f[9]>=0.0f && f[9]<=0.1f &&
            f[10]>=0.0f && f[10]<=100.0f && f[11]>=0.0f && f[11]<=100.0f &&
            f[12]>=20.0f && f[12]<=20000.0f;
        if (!ranges) fail(c,ip,"I3DL2 description out of range",words[0]);
        uint32_t fp=h2_platform_fpscr_read(); /* the diagnostic leaves the caller's FP state intact */
        xv_logf("[h2/reverb] description effect=%u room=%d roomHF=%d rolloff=%u.%02u decay=%u.%02us hfratio=%u.%02u reflections=%d@%ums reverb=%d@%ums diffusion=%u density=%u hfref=%u fcw=%04X fpscr=%08X\n",
                index, room, room_hf, (unsigned)f[3], (unsigned)(f[3]*100)%100, (unsigned)f[4], (unsigned)(f[4]*100)%100,
                (unsigned)f[5], (unsigned)(f[5]*100)%100, reflections, (unsigned)(f[7]*1000), reverb, (unsigned)(f[9]*1000),
                (unsigned)f[10], (unsigned)f[11], (unsigned)f[12], c->fcw, fp);
        h2_platform_fpscr_write(fp);
    }
    if (!h2_audio_backend_effect_read(effects,index,0,prefix,sizeof prefix))
        fail(c,ip,"reverb current-state read",index);
    uint32_t base=xk_mem_alloc_high(4096,4096);
    if (!base) { result(c,0x8007000e,3);return; }
    if ((base&4095) || base>UINT32_MAX-4096 || !mapped(base,4096) ||
        overlaps_device(base,4096) || page_overlap(base,4096,source,sizeof words) ||
        page_overlap(base,4096,c->r[4],16)) fail(c,ip,"reverb private arena",base);
    uint8_t zero[4096]={0};x_guest_write(base,zero,sizeof zero);
    x_guest_write(base,prefix,sizeof prefix);x_guest_write(base+0x300,words,sizeof words);
    uint32_t frame[3]={0xdead0004,base+0x300,base};x_guest_write(base+0xf00,frame,sizeof frame);
    xctx converted=*c;converted.r[1]=base+0x340;converted.r[4]=base+0xf00;
    uint32_t conversion_fp=h2_platform_fpscr_read();
    reverb_conversion.context=&converted;reverb_conversion.arena=base;
    xv_call(&converted,0x3838A4);
    reverb_conversion.context=NULL;reverb_conversion.arena=0;
    h2_platform_fpscr_write(conversion_fp);
    uint32_t after[13],parameters[66],flags=X_M32(base+16)|4;
    x_guest_read(after,base+0x300,sizeof after);x_guest_read(parameters,base+280,sizeof parameters);
    int intact=converted.r[4]==base+0xf0c && !converted.df && converted.fsp==c->fsp && converted.fcw==c->fcw &&
        converted.r[3]==c->r[3] && converted.r[5]==c->r[5] && converted.r[6]==c->r[6] && converted.r[7]==c->r[7] &&
        X_M32(base+0x340)==base && !memcmp(after,words,sizeof words);
    for(unsigned a=544;a<0x300;++a)intact&=!X_M8(base+a);
    for(unsigned a=0x344;a<0x800;++a)intact&=!X_M8(base+a);
    if (xk_mem_free(base)<0)fail(c,ip,"reverb private arena release",base);
    if (!intact)fail(c,ip,"original reverb conversion ABI/footprint",index);
    int queued=index==8 ? h2_audio_backend_queue_reverb8(effects,flags,parameters) : h2_audio_backend_queue_reverb9(effects,flags,parameters);
    if (!queued)fail(c,ip,"reverb monitor queue busy/unsupported",index);
    xv_logf("[h2/reverb] caller=%08X original converter=003838A4 effect=%u flags=%08X parameters=264 bytes command=2 queued; real worker consumption pending\n",X_M32(c->r[4]),index,flags);
    result(c,0,3);
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
    case 0x37D4E2: case 0x37D835: stream_create(c, ip); break;
    case 0x37AB40: case 0x37AB87: stream_reference(c, ip); break;
    case 0x37B818: stream_headroom(c); break;
    case 0x37D598: case 0x37D54E: listener_vector(c, ip); break;
    case 0x37D141: deferred_commit(c); break;
    case 0x37D797: create(c); break;
    case 0x37A14F: case 0x37C70F: case 0x379F45: case 0x37A795: reference(c, ip); break;
    case 0x37D4BE: buffer_create(c); break;
    case 0x37D7DE: global_buffer_create(c); break;
    case 0x37CC4A: buffer_data(c); break;
    case 0x37B7B3: buffer_lock(c); break;
    case 0x379F40: buffer_unlock(c); break;
    case 0x37C5C8: case 0x37B66F: case 0x37B6A7: buffer_control(c, ip); break;
    case 0x37B5AE: case 0x37B5CA: query(c, ip); break;
    case 0x37D506: case 0x37D5CD: case 0x37D52A: scalar(c, ip); break;
    case 0x37B637: mix_bin(c, ip); break;
    case 0x37C5E4: case 0x37B6C3: buffer_routing(c, ip); break;
    case 0x37C6E5: fx_deferred_parameters(c); break;
    case 0x37B68B: fx_filter(c); break;
    case 0x37B6DF: buffer_play(c); break;
    case 0x37B703: buffer_stop(c); break;
    case 0x37B75B: buffer_status(c); break;
    case 0x37B797: buffer_rewind(c); break;
    case 0x37B777: buffer_cursor(c); break;
#if H2_AUDIO_DSP
    case 0x37BA6F: effects_description(c); break;
    case 0x37AD25: stream_process(c); break;
    case 0x37B86D: effects_download(c); break;
    case 0x37B5E6: effects_query(c); break;
    case 0x37B60D: effects_write(c); break;
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
/* The host owns the real device, so there is no original hardware singleton.
 * Permit the original void wrapper's empty low-priority path only while all
 * ordinary streams remain unsubmitted. Accurate notifications keep their
 * existing sink-gated worker; this check never retires a packet or commits
 * deferred listener state. Timed/normal-stream APIs still stop at their entries. */
static void original_empty_work(xctx *c, uint32_t ip)
{
    int helper=ip==0x379E9E;stack(c,ip,helper?2:0);
    uint32_t frame=c->r[4];
    if (helper) {
        if (X_M32(frame)!=0x37B84A || frame>UINT32_MAX-8)
            fail(c,ip,"original work helper caller",X_M32(frame));
        frame+=8;
    }
    if (X_M32(frame)!=0x21EC3C || frame<32 ||
        !mapped(0x387198,4) || !mapped(0x386B0C,4) ||
        X_M32(0x387198) || X_M32(0x386B0C))
        fail(c,ip,"original work caller/hardware owner",frame);
    live(c,ip,device.base+8,0);
    if (c->fs_base>UINT32_MAX-0x24 || !mapped(c->fs_base+0x24,1) || X_M8(c->fs_base+0x24))
        fail(c,ip,"original work passive IRQL",c->fs_base);
    uint32_t low=frame-32,irql=c->fs_base+0x24;
    output(c,ip,low,36);output(c,ip,0x386B18,28);
    if (aliases(low,36,0x386B18,28) || aliases(low,36,0x387198,4) ||
        aliases(low,36,0x386B0C,4) || aliases(low,36,irql,1) ||
        aliases(0x386B18,28,0x387198,4) || aliases(0x386B18,28,0x386B0C,4) ||
        aliases(0x386B18,28,irql,1))
        fail(c,ip,"original work control alias",frame);
    for (unsigned i=0;i<XA_MAX_VOICES;++i) {
        const h2_audio_buffer *b=&buffers[i];
        if (b->base && b->locked) fail(c,ip,"original work with uncommitted buffer lock",b->base);
        const h2_audio_stream *s=&streams[i];if (!s->base) continue;
        if (!s->references || (s->base&4095) || !mapped(s->base,4096) ||
            X_M32(s->base)!=0x417170 || X_M32(s->base+4)!=0x417160 || X_M32(s->base+8)!=s->references ||
            s->voice<0 || s->voice>=XA_MAX_VOICES || (s->flags!=0x20000000 && s->flags!=0x40000000) ||
            s->completed>s->submitted || s->submitted-s->completed>2)
            fail(c,ip,"original work stream accounting",s->base);
        unsigned pending=0;
        for (unsigned n=0;n<2;++n) {
            if (!!s->packets[n].mirror != !!s->packets[n].ticket)
                fail(c,ip,"original work packet ownership",s->base);
            pending+=s->packets[n].mirror!=0;
        }
        if (pending!=s->submitted-s->completed ||
            (s->flags==0x20000000 && (s->callback!=0x220730 || s->route_count || s->submitted || s->completed || pending)))
            fail(c,ip,"ordinary stream requires low-priority work",s->base);
        if (!s->submitted) {
            xk_audio_lock();int playing=xk_audio_voice_playing(s->voice);xk_audio_unlock();
            if (playing) fail(c,ip,"untracked active stream needs work",s->base);
        }
        if (s->submitted) {
#if H2_AUDIO_DSP
            if (!stream_worker || s->callback!=0x335D82 || s->route_count!=1 ||
                s->route_bin<27 || s->route_bin>30)
                fail(c,ip,"original work accurate worker contract",s->base);
#else
            fail(c,ip,"original work needs real stream worker",s->base);
#endif
        }
    }
}
void h2_audio_guest_entry(xctx *c, uint32_t ip)
{
#if H2_AUDIO_DSP
    if (original_reverb_entry(c,ip)) return;
#endif
    if (!device.ever_created) return;
    /* Menu bring-up (XV_MENU_VBLANK): the menu's audio init calls a large tree of original DSoundBuffer
     * buffer/config methods (closure from 0x37AD9C) that carry no APU/device I/O of their own - the
     * backend-touching methods are the separately-replaced host boundaries. Permit these audited
     * original methods to execute unchanged so the menu build proceeds; the APU boundary still traps. */
    { static int menu = -1; if (menu < 0) { const char *e = getenv("XV_MENU_VBLANK"); menu = e ? atoi(e) : 0; }
      if (menu) {
          uint32_t caller = mapped(c->r[4], 4) ? X_M32(c->r[4]) : 0;
          /* Menu bring-up: the menu's audio init drives a large set of original DSoundBuffer buffer/
           * config methods that carry no APU/device I/O of their own (backend-touching methods are the
           * separately-replaced host boundaries, and the APU MMIO boundary still traps). Permit those to
           * execute unchanged so the menu build proceeds, EXCEPT the few methods that require their host
           * handler: work delivery (0x37B844), release (0x379F2A), the config methods (0x379F5B/0x37E126),
           * and 0x379E9E when reached from its work/config callers. Each distinct pass-through is logged
           * for later per-method auditing. */
          int handler_needed =
              ip == 0x37B844 || ip == 0x379F2A || ip == 0x379F5B || ip == 0x37E126 ||
              (ip == 0x379E9E && (caller == 0x37B84A || caller == 0x379F61));
          if (!handler_needed) {
              static uint32_t seen_pt[512]; static unsigned n_pt; int known = 0;
              for (unsigned i = 0; i < n_pt; ++i) if (seen_pt[i] == ip) { known = 1; break; }
              if (!known && n_pt < 512) { seen_pt[n_pt++] = ip;
                  xv_logf("[h2/audio-menu] passthrough DSOUND %08X from %08X\n", ip, caller); }
              return;   /* run the original DSOUND method unchanged */
          }
      } }
    if (ip==0x37B844 || (ip==0x379E9E && !(c->r[4]&3) && mapped(c->r[4],4) && X_M32(c->r[4])==0x37B84A)) {
        uint32_t fpscr=h2_platform_fpscr_read();
#if H2_AUDIO_DSP
        if (ip==0x37B844) stream_deliver(c,ip);
#endif
        original_empty_work(c,ip);
        if (ip==0x37B844) xv_logf("[h2/audio-work] original void wrapper executes with no ordinary pending work; completed stream packets delivered here, listener dirty=%08X retained\n",device.dirty);
        h2_platform_fpscr_write(fpscr);return;
    }
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
#if H2_AUDIO_DSP
static void trace_effect_description(xctx *c)
{
    /* XAudioSetEffectData has three arguments, including an optional raw
     * output, not flags. This terminal probe never invokes its conversion,
     * writes a descriptor, queues a command or resumes the stopped caller. */
    if ((c->r[4]&3) || !mapped(c->r[4],16)) return;
    uint32_t fp=h2_platform_fpscr_read(), index=X_ARG(0), input=X_ARG(1);
    xv_logf("[h2/effect-description-probe] caller=%08X index=%u description=%08X raw_output=%08X\n",
            X_M32(c->r[4]),index,input,X_ARG(2));
    xv_logf("[h2/effect-description-probe] fcw=%04X fsp=%u df=%u fpscr=%08X conversion_active=%u effects=%u effects_mapped=%u input_mapped=%u owned_alias=%u frame_alias=%u\n",
            (unsigned)c->fcw,(unsigned)c->fsp,(unsigned)c->df,fp,
            reverb_conversion.context!=NULL,effects!=NULL,mapped(effects_guest,effects_guest_bytes),
            mapped(input,52),overlaps_device(input,52),aliases(input,52,c->r[4],16));
    if (mapped(input,52)) {
        uint32_t words[13];x_guest_read(words,input,sizeof words);
        if (words[0]!=12) { h2_platform_fpscr_write(fp);return; }
        for (unsigned i=0;i<13;++i)
            xv_logf("[h2/effect-description-probe] input[%u]=%08X\n",i,words[i]);
        uint32_t state[70];
        int read=effects && (index==8 || index==9) && h2_audio_backend_effect_read(effects,index,0,state,sizeof state);
        xv_logf("[h2/effect-description-probe] current_state bytes=280 complete=%u\n",read);
        if (read) for (unsigned i=0;i<70;++i)
            xv_logf("[h2/effect-description-probe] state[%u]=%08X\n",i,state[i]);
    }
    h2_platform_fpscr_write(fp);
}

static void trace_deferred_commit(xctx *c)
{
    /* Public wrapper37D141 has one interface argument and ret4. The
     * following stack word is not an output pointer. This only records
     * adapter-owned pending state at the terminal unsupported entry. */
    if ((c->r[4]&3) || !mapped(c->r[4],8)) return;
    uint32_t fp=h2_platform_fpscr_read();
    xv_logf("[h2/deferred-commit-probe] caller=%08X interface=%08X device=%08X refs=%u children=%u fcw=%04X fsp=%u df=%u fpscr=%08X dirty=%08X\n",
            X_M32(c->r[4]),X_ARG(0),device.base,device.references,device.children,
            (unsigned)c->fcw,(unsigned)c->fsp,(unsigned)c->df,fp,device.dirty);
    xv_logf("[h2/deferred-commit-probe] scalar active=%08X,%08X,%08X pending=%08X,%08X,%08X\n",
            device.distance,device.rolloff,device.doppler,
            device.pending_distance,device.pending_rolloff,device.pending_doppler);
    for(unsigned i=0;i<3;++i)xv_logf("[h2/deferred-commit-probe] pending_position[%u]=%08X\n",i,device.pending_position[i]);
    for(unsigned i=0;i<6;++i)xv_logf("[h2/deferred-commit-probe] pending_orientation[%u]=%08X\n",i,device.pending_orientation[i]);
    for(unsigned i=0;i<XA_MAX_VOICES;++i){
        const h2_audio_buffer*b=&buffers[i];if(!b->base||!b->submix)continue;
        xv_logf("[h2/deferred-commit-probe] buffer=%08X submix=%u key=%X refs=%u started=%u stopped=%u volume=%d headroom=%u routes=%u\n",
                b->base+0x1C,b->submix,b->fx_bin,b->references,b->started,b->stopped,b->volume,b->headroom,b->route_count);
        for(unsigned j=0;j<6;++j)xv_logf("[h2/deferred-commit-probe] buffer=%08X route[%u]=%u,%d filter[%u]=%08X\n",
                b->base+0x1C,j,b->route_bins[j],b->route_gains[j],j,b->filter[j]);
        for(unsigned j=0;j<41;++j)xv_logf("[h2/deferred-commit-probe] buffer=%08X spatial[%u]=%08X\n",b->base+0x1C,j,b->spatial[j]);
    }
    h2_platform_fpscr_write(fp);
}

#endif
void h2_audio_trace_buffer(xctx *c, uint32_t ip)
{
    /* Terminal read-only probes; never repair guest inputs or resume them. */
    if (ip==0x37B844) {
        if (!(c->r[4]&3) && mapped(c->r[4],4) && mapped(0x387198,4) && mapped(0x386B0C,4))
            xv_logf("[h2/audio-work-probe] caller=%08X original_singleton=%08X suspended=%08X adapter_device=%08X refs=%u children=%u dirty=%08X; zero arguments, no work serviced by this probe\n",
                    X_M32(c->r[4]),X_M32(0x387198),X_M32(0x386B0C),device.base,device.references,device.children,device.dirty);
        for (unsigned i=0;i<XA_MAX_VOICES;++i) {
            const h2_audio_stream *s=&streams[i];if (!s->base) continue;
            xv_logf("[h2/audio-work-probe] stream=%08X flags=%08X callback=%08X submitted=%u completed=%u packet0=%llu packet1=%llu route_count=%u voice=%d\n",
                    s->base,s->flags,s->callback,s->submitted,s->completed,
                    (unsigned long long)s->packets[0].ticket,(unsigned long long)s->packets[1].ticket,s->route_count,s->voice);
        }
    }
#if H2_AUDIO_DSP
    if (ip==0x37BA6F) trace_effect_description(c);
    if (ip==0x37D141) trace_deferred_commit(c);
    if (ip==0x37B60D && !(c->r[4]&3) && mapped(c->r[4],28)) {
        uint32_t index=X_ARG(1),offset=X_ARG(2),source=X_ARG(3),bytes=X_ARG(4),words[2];
        xv_logf("[h2/effect-write-probe] caller=%08X index=%u offset=%u source=%08X bytes=%u flags=%08X\n",
                X_M32(c->r[4]),index,offset,source,bytes,X_ARG(5));
        if(bytes==8 && mapped(source,8)) {
            x_guest_read(words,source,8);
            xv_logf("[h2/effect-write-probe] input=%08X,%08X\n",words[0],words[1]);
            if(effects && h2_audio_backend_effect_read(effects,index,offset,words,8))
                xv_logf("[h2/effect-write-probe] current_GP=%08X,%08X\n",words[0],words[1]);
        }
        if(effects_guest && index<15 && mapped(effects_guest+8+index*32,16)) {
            uint32_t map[4];x_guest_read(map,effects_guest+8+index*32,16);
            xv_logf("[h2/effect-write-probe] state_view=%08X state_bytes=%u code_view=%08X code_bytes=%u\n",
                    map[2],map[3],map[0],map[1]);
        }
    }
#endif
    if (ip==0x37AD25 && !(c->r[4]&3) && mapped(c->r[4],16)) {
        uint32_t input=X_ARG(1),packet[6];
        for (unsigned i=0;i<XA_MAX_VOICES;++i) if (streams[i].base==X_ARG(0) && streams[i].references)
            xv_logf("[h2/audio-packet] stream=%08X flags=%08X route=%u callback=%08X context=%08X packet_limit=%u voice=%d headroom=%u\n",
                    streams[i].base,streams[i].flags,streams[i].route_bin,streams[i].callback,streams[i].context,
                    streams[i].packet_limit,streams[i].voice,streams[i].headroom);
        if (mapped(input,sizeof packet)) {
            x_guest_read(packet,input,sizeof packet);
            xv_logf("[h2/audio-packet] caller=%08X input=%08X output=%08X buffer=%08X bytes=%u completed=%08X status=%08X context=%08X timestamp=%08X\n",
                    X_M32(c->r[4]),input,X_ARG(2),packet[0],packet[1],packet[2],packet[3],packet[4],packet[5]);
            if (packet[1] && packet[1]<=4096 && mapped(packet[0],packet[1])) {
                uint8_t samples[4096];x_guest_read(samples,packet[0],packet[1]);
                uint32_t nonzero=0,hash=2166136261u;
                for (unsigned i=0;i<packet[1];++i) {nonzero+=samples[i]!=0;hash=(hash^samples[i])*16777619u;}
                xv_logf("[h2/audio-packet] source nonzero_bytes=%u fnv1a32=%08X\n",nonzero,hash);
            }
        }
    }

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
    int global_create = ip == 0x37D7DE || ip == 0x37D835;
    int stream = ip == 0x37D4E2 || ip == 0x37D835;
    if ((!global_create && ip != 0x37D4BE && ip != 0x37D4E2) ||
        !mapped(c->r[4], global_create ? 12 : 20) || (c->r[4] & 3)) return;
    uint32_t desc = X_ARG(global_create ? 0 : 1), fields[6];
    uint32_t out = X_ARG(global_create ? 1 : 2), outer = global_create ? 0 : X_ARG(3);
    if (!mapped(desc, sizeof fields)) return;
    x_guest_read(fields, desc, sizeof fields);
    uint32_t wfx;
    if (stream) {
        xv_logf("[h2/audio-stream] descriptor=%08X flags=%08X packets=%u format=%08X callback=%08X context=%08X mixbins=%08X output=%08X outer=%08X\n",
                desc, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], out, outer);
        wfx = fields[2];
    } else {
        xv_logf("[h2/audio-buffer] descriptor=%08X size=%08X flags=%08X bytes=%08X format=%08X mixbins=%08X inputbin=%08X output=%08X outer=%08X\n",
                desc, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], out, outer);
        wfx = fields[3];
    }
    uint32_t route = fields[stream ? 5 : 4];
    if (global_create) xv_logf("[h2/audio-global] entry=%08X caller=%08X\n",ip,X_M32(c->r[4]));
    if (ip==0x37D835 && X_M32(c->r[4])==0x335ED8) {
        uint32_t frame=c->r[5];
        for(unsigned n=0;n<2 && mapped(frame,8);++n)frame=X_M32(frame);
        if(frame>=0x2c && mapped(frame-0x2c,32)) {
            uint32_t pairs[8];x_guest_read(pairs,frame-0x2c,32);
            xv_logf("[h2/audio-global] ancestor_frame=%08X raw_words_at_minus2C=%u/%d,%u/%d,%u/%d,%u/%d (layout depends on original caller)\n",
                    frame,pairs[0],(int32_t)pairs[1],pairs[2],(int32_t)pairs[3],pairs[4],(int32_t)pairs[5],pairs[6],(int32_t)pairs[7]);
        }
    }
    if (global_create && mapped(route,8)) {
        uint32_t list[2]; x_guest_read(list,route,8);
        xv_logf("[h2/audio-global] caller=%08X route_list=%08X count=%u pairs=%08X\n",X_M32(c->r[4]),route,list[0],list[1]);
        if (list[0] && list[0] <= 8 && mapped(list[1],list[0] * 8)) {
            uint32_t pairs[16]; x_guest_read(pairs,list[1],list[0] * 8);
            for (unsigned i=0;i<list[0];++i) xv_logf("[h2/audio-global] route[%u] bin=%u volume=%d\n",i,pairs[i*2],(int32_t)pairs[i*2+1]);
        }
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
