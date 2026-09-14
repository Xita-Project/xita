/* Checked XDK5849 device boundary. Buffer/stream/effect APIs are not supplied
 * by this milestone. A successful device owns a real mixer/output worker. */
#include "audio_host.h"
#include "recomp/kernel/xk.h"
#include <string.h>

static h2_audio_device_snapshot device;
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
static int overlaps_device(uint32_t address, uint32_t bytes)
{
    if (!device.base) return 0;
    /* Reject aliases through another virtual page as well as direct overlap.
     * The opaque device owns the complete single-page guest allocation. */
    uint32_t owned = g_xpt[device.base >> 12];
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t p = address >> 12; p <= last; ++p)
        if (g_xpt[p] == owned) return 1;
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
static void reference(xctx *c, uint32_t ip)
{
    stack(c, ip, 1); live(c, ip, X_ARG(0), 1);
    if (ip == 0x37A14F) {
        if (device.references == UINT32_MAX) fail(c, ip, "reference overflow", device.references);
        ++device.references;
        X_M32(device.base + 4) = device.references;
    } else if (device.references > 1) {
        --device.references; X_M32(device.base + 4) = device.references;
    } else {
        if (h2_audio_backend_close() < 0) fail(c, ip, "close worker", device.base);
        if (xk_mem_free(device.base) < 0) fail(c, ip, "free device", device.base);
        device = (h2_audio_device_snapshot){.ever_created = 1};
    }
    result(c, device.references, 1);
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
void h2_audio_host_call(xctx *c, uint32_t ip)
{
    uint32_t fpscr = h2_platform_fpscr_read();
    switch (ip) {
    case 0x37D797: create(c); break;
    case 0x37A14F: case 0x37C70F: reference(c, ip); break;
    case 0x37B5AE: case 0x37B5CA: query(c, ip); break;
    case 0x37D506: case 0x37D5CD: case 0x37D52A: scalar(c, ip); break;
    case 0x37B637: mix_bin(c, ip); break;
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
