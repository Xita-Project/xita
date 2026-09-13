/* Experimental H2-only replacement of hardware setup by a real synchronous
 * command consumer. Original allocation, DMA objects, RAMHT, state generation
 * and submission remain guest code. Unsupported commands and display work stop. */
#include "host_channel_runtime.h"
#include "host_channel.h"
#include "host_tiles.h"
#include "gpu_bus.h"
#include "nv2a_regs.h"
#include "instance_memory.h"
#include <string.h>

#define DEVICE 0x404FE0u
#define MINIPORT (DEVICE + 0x1C28u)
#define PHYSICAL_BYTES 0x4000000u
#define BAR 0xFD000000u
static h2_host_channel channel;
static h2_host_tiles tiles;
static xctx *active_context;
static int miniport_ready, channel_ready;
extern void xv_logf(const char *, ...);
extern uint32_t xk_mem_arena_size(void);
extern void f_003FE165(xctx *), f_003FE190(xctx *);
extern void f_00401C33(xctx *), f_00401D96(xctx *);

static void reject(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{
    h2_graphics_stop(c, ip, address, value, 1, H2_NV2A_UNSUPPORTED_OPERATION);
}
static int guest_span_valid(uint32_t address, uint32_t bytes)
{
    uint32_t arena = xk_mem_arena_size();
    if (!bytes || arena < 4096 || (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t page = address >> 12; page <= last; ++page) {
        uint32_t offset = g_xpt[page];
        if ((offset & 4095) || (uint64_t)offset + 4096 > arena - 4096) return 0;
    }
    return 1;
}
static void check_stack(xctx *c, uint32_t ip, unsigned args)
{
    if ((c->r[4] & 3) || !guest_span_valid(c->r[4], 4 + args * 4))
        reject(c, ip, c->r[4], args);
}
static void call_guest(xctx *c, void (*function)(xctx *), uint32_t ip)
{
    uint32_t stack = c->r[4];
    if (stack < 256 || !guest_span_valid(stack - 256, 256)) reject(c, ip, stack, 256);
    X_PUSH32(ip);
    function(c);
    if (c->r[4] != stack) reject(c, ip, c->r[4], stack);
}
static void *map_physical_raw(uint32_t address, uint32_t bytes)
{
    if (!bytes || address >= PHYSICAL_BYTES || bytes > PHYSICAL_BYTES - address) return NULL;
    /* Verify the entire cached physical alias and its host contiguity. This
     * excludes the separately mapped XBE image and the unmapped trash page. */
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t page = address >> 12; page <= last; ++page)
        if (g_xpt[0x80000u + page] != page * 4096u) return NULL;
    return g_xram + address;
}
static void *map_physical(void *opaque, uint32_t address, uint32_t bytes)
{
    (void)opaque;
    if (!h2_host_tiles_span(&tiles, address, bytes)) return NULL;
    return map_physical_raw(address, bytes);
}
static int read_physical(void *opaque, uint32_t address, uint32_t *word)
{
    void *pointer = map_physical(opaque, address, 4);
    if (!pointer) return 0;
    memcpy(word, pointer, 4);
    return 1;
}
static int check_attachment(void *opaque, uint32_t address, uint32_t bytes,
                              uint32_t pitch, int zeta, uint32_t format)
{
    (void)opaque;
    return h2_host_tiles_attachment(&tiles, address, bytes, pitch, zeta, format);
}
static int read_instance(void *opaque, uint32_t offset, uint32_t *word)
{
    (void)opaque;
    if ((offset & 3) || offset < 0x10000u ||
        (uint64_t)offset + 4 > 0x10000u + h2_instance_bytes()) return 0;
    uint32_t physical = (PHYSICAL_BYTES - 64 - (offset & ~63u)) | (offset & 63u);
    if (!map_physical_raw(physical, 4)) return 0;
    *word = h2_bus_read32(active_context, 0, BAR + 0x700000u + offset);
    return 1;
}
static void instance_write(xctx *c, uint32_t offset, uint32_t value)
{ h2_bus_write32(c, 0x4026CEu, BAR + 0x700000u + offset, value); }

void h2_host_miniport_init(xctx *c)
{
    check_stack(c, 0x3FE005u, 0);
    uint32_t mini = c->r[0], saved[8];
    memcpy(saved, c->r, sizeof saved);
    if (miniport_ready || mini != MINIPORT || X_M32(0x407488u) != DEVICE)
        reject(c, 0x3FE005u, mini, miniport_ready);
    /* Keep the driver's software DPC/list/gamma state. This backend executes
     * commands synchronously; it does not register physical NV2A interrupts. */
    X_M32(mini + 0x88) = 0x3FEBE0u;
    X_M32(mini + 0x8C) = mini;
    X_M32(mini + 0x1AC) = X_M32(mini + 0x1B0) = mini + 0x1AC;
    X_M32(mini + 0x19C) = X_M32(mini + 0x1A0) = mini + 0x19C;
    X_M8(mini + 0x1A4) = X_M8(mini + 0x194) = 0;
    X_M8(mini + 0x1A6) = X_M8(mini + 0x196) = 4;
    X_M32(mini + 0x1A8) = X_M32(mini + 0x198) = 1;
    c->r[0] = mini; call_guest(c, f_003FE165, 0x3FE070u);
    if (!c->r[0]) reject(c, 0x3FE005u, mini, 0);
    c->r[0] = mini; call_guest(c, f_003FE190, 0x3FE082u);
    if (!c->r[0]) reject(c, 0x3FE005u, mini, 0);
    X_M32(mini + 0x164) = 0x3FF490u;
    X_M32(mini + 0x168) = 0;
    h2_bus_write32(c, 0x3FE1D9u, BAR + 0x1830, 0);
    h2_bus_write32(c, 0x3FE1E3u, BAR + 0x180C, 0xF800);
    c->r[6] = mini; call_guest(c, f_00401C33, 0x3FE1F2u);
    X_M32(mini + 0xB4) = 1;
    h2_bus_write32(c, 0x3FE1FCu, BAR + 0x9200, 0xDE86);
    h2_bus_write32(c, 0x3FE206u, BAR + 0x9210, 0x1DCD);
    h2_bus_write32(c, 0x3FE210u, BAR + 0x9420, UINT32_MAX);
    c->r[6] = mini; call_guest(c, f_00401D96, 0x3FE21Fu);
    if (h2_instance_bytes() != 0x5000 || X_M32(mini + 0x130) != 0x710000 ||
        X_M32(mini + 0x128) != 0x711000 || X_M32(mini + 0x140) != 0x110A)
        reject(c, 0x3FE005u, mini, h2_instance_bytes());
    X_M32(mini + 0x134) = 2;
    instance_write(c, X_M32(mini + 0x140) * 16, 0);
    instance_write(c, X_M32(mini + 0x140) * 16 + 4, 0);
    X_M32(mini + 0x120) = 0xFF;
    X_M32(mini + 0x124) = 0x800000;
    X_M32(mini + 0x11C) = 0x1111111;
    for (unsigned table = 0; table < 6; ++table)
        for (unsigned i = 0; i < 256; ++i)
            X_M8(mini + 0x1DC + table * 256 + i) = (uint8_t)i;
    /* The virtual device has a command consumer but no active display/IRQ. */
    X_M32(mini + 0xA0) = 1;
    miniport_ready = 1;
    xv_logf("[h2/channel] virtual miniport initialized, instance=%08X; display/IRQ unsupported\n",
            h2_instance_bytes());
    memcpy(c->r, saved, sizeof saved);
    c->r[0] = 1;
    X_RET(0);
}

void h2_host_channel_configure(xctx *c)
{
    check_stack(c, 0x4026CEu, 3);
    uint32_t mini = c->r[2], descriptor = X_ARG(2);
    if (!miniport_ready || channel_ready || mini != MINIPORT || X_M32(mini + 0x100))
        reject(c, 0x4026CEu, mini, channel_ready);
    if ((descriptor & 3) || !guest_span_valid(descriptor, 16)) reject(c, 0x4026CEu, descriptor, 16);
    active_context = c;
    uint32_t context = X_M32(mini + 0x10C), dma_instance = X_M32(descriptor + 12);
    uint32_t ring = X_M32(DEVICE + 0x24), end = X_M32(DEVICE + 0x28);
    h2_dma_object dma;
    if (ring < 0x80000000u || end <= ring || end > 0x84000000u ||
        context < 0x1112u || (uint64_t)context * 16 + 0x37F0 > 0x15000u ||
        X_M32(mini + 0x160) != context + 0x37Fu ||
        X_M32(mini + 0x128) != 0x711000u || X_M32(mini + 0x140) != 0x110Au ||
        dma_instance < 0x1112u || dma_instance >= context ||
        !map_physical_raw(ring - 0x80000000u, end - ring) ||
        !map_physical_raw(PHYSICAL_BYTES - 0x10000u - h2_instance_bytes(), h2_instance_bytes()) ||
        !h2_dma_load(read_instance, NULL, dma_instance * 16, &dma) ||
        !h2_host_channel_init(&channel, &dma, ring - 0x80000000u, end - ring,
                              read_physical, read_instance, map_physical, NULL, PHYSICAL_BYTES))
        reject(c, 0x4026CEu, descriptor, dma_instance);
    for (uint32_t i = 0; i < 0x37F0; i += 4) instance_write(c, context * 16 + i, 0);
    instance_write(c, X_M32(mini + 0x140) * 16, context);
    X_M32(mini + 0x138) = context;
    uint32_t ramfc = X_M32(mini + 0x128) - 0x700000u;
    for (unsigned i = 0; i < 64; i += 4) instance_write(c, ramfc + i, 0);
    instance_write(c, ramfc + 12, dma_instance);
    uint32_t burst = X_ARG(0), time = X_ARG(1), priority = c->r[0];
    if (burst < 8) burst = 8;
    if (burst > 256) burst = 256;
    if (time < 32) time = 32;
    if (time > 256) time = 256;
    if (priority > 15) priority = 15;
    uint32_t schedule = ((((priority << 3) | ((time >> 5) - 1)) << 10) | ((burst >> 3) - 1)) << 3;
    instance_write(c, ramfc + 20, schedule);
    X_M32(mini + 0x104) |= 1;
    X_M32(mini + 0x108) |= 1;
    channel_ready = 1;
    channel.clear.check_attachment = check_attachment;
    xv_logf("[h2/channel] configured channel=0 DMA=%08X context=%08X ring=%08X+%X schedule=%08X\n",
            dma_instance, context, channel.stream.base, channel.stream.bytes, schedule);
    X_RET(3);
}

void h2_host_memory_barrier(xctx *c)
{
    check_stack(c, 0x3FADE0u, 0);
    if (!miniport_ready) reject(c, 0x3FADE0u, 0, 0);
    /* The synchronous CPU consumer shares normal host RAM with generated code. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    X_RET(0);
}
void __wrap_xk_AvSendTVEncoderOption(xctx *c)
{
    check_stack(c, 0, 4);
    uint32_t base = X_ARG(0), option = X_ARG(1), param = X_ARG(2), output = X_ARG(3);
    /* Explicit virtual target: NTSC-M, 60 Hz, normal aspect, HDTV 480p. This
     * reports capabilities only; applying a display mode remains unsupported. */
    if (!miniport_ready || (base && base != BAR) || option != 6 || param ||
        !output || (output & 3) || !guest_span_valid(output, 4))
        reject(c, X_M32(c->r[4]), output, option);
    X_M32(output) = 0x00480104u;
    xv_logf("[h2/av] virtual AV capabilities=00480104 output=%08X; no display mode applied\n", output);
    X_RET(4);
}
void __wrap_xk_AvSetDisplayMode(xctx *c)
{
    check_stack(c, 0, 6);
    xv_logf("[h2/av] display-mode application is unsupported\n");
    reject(c, X_M32(c->r[4]), X_ARG(0), X_ARG(2));
}

static int channel_idle(void)
{ return channel_ready && !channel.bootstrap && !channel.stream.remaining && channel.put == channel.stream.get; }
void h2_host_tile_remove(xctx *c)
{
    check_stack(c, 0x3FE86Au, 2);
    uint32_t index = c->r[3], mini = X_ARG(0), clear_zoffset = X_ARG(1);
    if (mini != MINIPORT || !channel_idle() || !h2_host_tile_disable(&tiles, index))
        reject(c, 0x3FE86Au, mini, index);
    /* The canonical representation has no physical compression offset. */
    xv_logf("[h2/tiles] disabled index=%u clear_zoffset=%08X\n", index, clear_zoffset);
    c->r[0] = 1;
    X_RET(2);
}
void h2_host_tile_configure(xctx *c)
{
    check_stack(c, 0x3FE67Fu, 7);
    uint32_t index = c->r[0], mini = X_ARG(0), address = X_ARG(1), bytes = X_ARG(2);
    uint32_t pitch = X_ARG(3), flags = X_ARG(4), zstart = X_ARG(5), zoffset = X_ARG(6);
    xv_logf("[h2/tiles] assign index=%u address=%08X bytes=%08X pitch=%u flags=%08X zstart=%08X zoffset=%08X\n",
            index, address, bytes, pitch, flags, zstart, zoffset);
    if (mini != MINIPORT || !channel_idle() ||
        !map_physical_raw(address, bytes) ||
        !h2_host_tile_assign(&tiles, index, address, bytes, pitch, flags, zstart, zoffset, PHYSICAL_BYTES))
        reject(c, 0x3FE67Fu, address, flags);
    if (flags == 0x84000001u)
        xv_logf("[h2/tiles] depth uses canonical logical Z24S8 bytes; hardware compression/tags are not emulated\n");
    c->r[0] = 1;
    X_RET(7);
}

int h2_host_channel_bus(xctx *c, uint32_t ip, uint32_t address, unsigned width,
                         uint32_t *value, int write)
{
    if (address != BAR + 0x800040 && address != BAR + 0x800044 &&
        address != BAR + 0x3240 && address != BAR + 0x3244 && address != BAR + 0x400700) return 0;
    if (!channel_ready || width != 4 || (write && address != BAR + 0x800040))
        reject(c, ip, address, *value);
    active_context = c;
    if (write) {
        h2_push_fault fault;
        enum h2_push_result result = h2_host_channel_submit(&channel, *value, 1000000, &fault);
        xv_logf("[h2/channel] PUT=%08X GET=%08X result=%d source=%08X word=%08X sub=%u method=%04X clears=%llu pixels=%llu\n",
                *value, h2_host_channel_get(&channel), result, fault.address, fault.word,
                fault.subchannel, fault.method, (unsigned long long)channel.clear.completed_clears,
                (unsigned long long)channel.clear.written_pixels);
        if (result != H2_PUSH_COMPLETE && result != H2_PUSH_NEED_DATA)
            reject(c, ip, address, *value);
    } else if (address == BAR + 0x800040 || address == BAR + 0x3240) *value = channel.put;
    else if (address == BAR + 0x800044 || address == BAR + 0x3244) *value = h2_host_channel_get(&channel);
    else *value = channel.stream.remaining != 0 || channel.put != h2_host_channel_get(&channel);
    return 1;
}
