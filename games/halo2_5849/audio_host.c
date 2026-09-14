/* Checked XDK5849 device and external PCM buffer boundaries. Unknown sound
 * methods stop; every accepted object owns real mixer/output resources. */
#include "audio_host.h"
#include "recomp/kernel/xk.h"
#include "recomp/kernel/xk_audio.h"
#include <string.h>

static h2_audio_device_snapshot device;
typedef struct {
    uint32_t base, references, mirror, mirror_bytes, source, bytes;
    uint32_t frequency, headroom;
    int32_t volume;
    int voice;
} h2_audio_buffer;
static h2_audio_buffer buffers[XA_MAX_VOICES];
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
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i)
        if (page_overlap(address, bytes, buffers[i].base, 4096) ||
            page_overlap(address, bytes, buffers[i].mirror, buffers[i].mirror_bytes)) return 1;
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
    return b;
}
static void reference(xctx *c, uint32_t ip)
{
    stack(c, ip, 1);
    if (ip != 0x37C70F && (ip != 0x37A14F || find_buffer(X_ARG(0)))) {
        h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), ip != 0x379F45);
        uint32_t count = b->references;
        if (ip == 0x37A14F) {
            if (count == UINT32_MAX) fail(c, ip, "buffer reference overflow", count);
            X_M32(b->base + 4) = ++b->references;
            count = b->references;
        } else if (--count) {
            X_M32(b->base + 4) = b->references = count;
        } else {
            /* free() takes the mixer lock, so no worker can still read the
             * mirror when its allocation is released. Caller owns source. */
            xk_audio_voice_free(b->voice);
            if (b->mirror && xk_mem_free(b->mirror) < 0) fail(c, ip, "free PCM mirror", b->mirror);
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
static void buffer_create(xctx *c)
{
    const uint32_t ip = 0x37D4BE;
    stack(c, ip, 4); live(c, ip, X_ARG(0), 0); buffer_operational(c, ip);
    uint32_t desc = X_ARG(1), out = X_ARG(2), fields[6];
    output(c, ip, out, 4);
    if (X_ARG(3) || !mapped(desc, sizeof fields)) fail(c, ip, "buffer descriptor", desc);
    x_guest_read(fields, desc, sizeof fields);
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
#if H2_AUDIO_MULTIBIN_UNAVAILABLE
/* Explicit failure probe for the observed movie's unsupported six-speaker
 * route. The real PCM voice retains its original FL/FR defaults unchanged. */
static void multibin_unavailable(xctx *c)
{
    const uint32_t ip = 0x37C5E4;
    stack(c, ip, 2); h2_audio_buffer *b = buffer_live(c, ip, X_ARG(0), 0);
    uint32_t list_address = X_ARG(1), list[2], pairs[12];
    if (X_M32(c->r[4]) != 0x3E321F || !b->mirror || !mapped(list_address, 8))
        fail(c, ip, "multibin diagnostic caller/input", list_address);
    x_guest_read(list, list_address, 8);
    if (list[0] != 6 || !mapped(list[1], sizeof pairs))
        fail(c, ip, "multibin diagnostic list", list[0]);
    x_guest_read(pairs, list[1], sizeof pairs);
    for (unsigned i = 0; i < 6; ++i)
        if (pairs[i * 2] != i || pairs[i * 2 + 1])
            fail(c, ip, "multibin diagnostic route", pairs[i * 2]);
    xv_logf("[h2/diagnostic] SetMixBins caller=%08X interface=%08X bins0..5 volume0 returns DSERR_UNSUPPORTED=80004001; real default FL/FR voice unchanged\n",
            X_M32(c->r[4]), b->base + 0x1C);
    result(c, 0x80004001, 2);
}
#endif
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
    case 0x37D797: create(c); break;
    case 0x37A14F: case 0x37C70F: case 0x379F45: case 0x37A795: reference(c, ip); break;
    case 0x37D4BE: buffer_create(c); break;
    case 0x37CC4A: buffer_data(c); break;
    case 0x37C5C8: case 0x37B66F: case 0x37B6A7: buffer_control(c, ip); break;
    case 0x37B5AE: case 0x37B5CA: query(c, ip); break;
    case 0x37D506: case 0x37D5CD: case 0x37D52A: scalar(c, ip); break;
    case 0x37B637: mix_bin(c, ip); break;
#if H2_AUDIO_MULTIBIN_UNAVAILABLE
    case 0x37C5E4: multibin_unavailable(c); break;
#endif
#if H2_AUDIO_EFFECTS_UNAVAILABLE
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
        if ((caller != 0x21EB91 && caller != 0x3E3D19) || !mapped(0x417128, 4) || X_M32(0x417128) != 0x37C70F)
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
    for (unsigned i = 0; i < XA_MAX_VOICES; ++i) {
        const h2_audio_buffer *b = &buffers[i];
        if (b->base)
            xv_logf("[h2/audio-buffer] stop interface=%08X refs=%u voice=%d external=%08X bytes=%u mirror=%08X frequency=%u volume=%d headroom=%u\n",
                    b->base + 0x1C, b->references, b->voice, b->source, b->bytes, b->mirror,
                    b->frequency, b->volume, b->headroom);
    }
    if (ip == 0x37B7B3 && !(c->r[4] & 3) && mapped(c->r[4], 36)) {
        xv_logf("[h2/audio-buffer] Lock caller=%08X interface=%08X offset=%u bytes=%u pointer1=%08X length1=%08X pointer2=%08X length2=%08X flags=%08X\n",
                X_M32(c->r[4]), X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), X_ARG(4), X_ARG(5), X_ARG(6), X_ARG(7));
        return;
    }
    if (ip == 0x37C5E4 && !(c->r[4] & 3) && mapped(c->r[4], 12)) {
        uint32_t address = X_ARG(1), list[2];
        if (!mapped(address, 8)) return;
        x_guest_read(list, address, 8);
        xv_logf("[h2/audio-buffer] SetMixBins caller=%08X list=%08X count=%u entries=%08X\n",
                X_M32(c->r[4]), address, list[0], list[1]);
        if (!list[0] || list[0] > 8 || !mapped(list[1], list[0] * 8)) return;
        uint32_t pairs[16]; x_guest_read(pairs, list[1], list[0] * 8);
        for (unsigned i = 0; i < list[0]; ++i)
            xv_logf("[h2/audio-buffer] route[%u] bin=%u volume=%d\n", i, pairs[i*2], (int32_t)pairs[i*2+1]);
        return;
    }
    if (ip != 0x37D4BE || !mapped(c->r[4], 20) || (c->r[4] & 3)) return;
    uint32_t desc = X_ARG(1), fields[6];
    if (!mapped(desc, sizeof fields)) return;
    x_guest_read(fields, desc, sizeof fields);
    xv_logf("[h2/audio-buffer] descriptor=%08X size=%08X flags=%08X bytes=%08X format=%08X mixbins=%08X inputbin=%08X output=%08X outer=%08X\n",
            desc, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], X_ARG(2), X_ARG(3));
    uint8_t format[18];
    if (!mapped(fields[3], sizeof format)) return;
    x_guest_read(format, fields[3], sizeof format);
    uint16_t tag, channels, align, bits, extra; uint32_t rate, average;
    memcpy(&tag, format, 2); memcpy(&channels, format + 2, 2);
    memcpy(&rate, format + 4, 4); memcpy(&average, format + 8, 4);
    memcpy(&align, format + 12, 2); memcpy(&bits, format + 14, 2); memcpy(&extra, format + 16, 2);
    xv_logf("[h2/audio-buffer] wave tag=%u channels=%u rate=%u average=%u align=%u bits=%u extra=%u\n",
            tag, channels, rate, average, align, bits, extra);
}
