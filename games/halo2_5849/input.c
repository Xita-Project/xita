/* Pinned XPP API adapter for Vita's built-in controller. Original query/change
 * methods consume the populated XPP records. No Xbox USB device is fabricated. */
#include "input.h"
#include "gpu_bus.h"
#include "nv2a_regs.h"
#include <string.h>
static int initialized;
static uint32_t handle, next_handle = 0xE2000001, packet;
static h2_pad_sample previous;
extern uint32_t xk_mem_arena_size(void);
extern void xv_logf(const char *, ...);
static const uint32_t types[] = {0x4086F8, 0x4088B0, 0x4088BC, 0x4087A8};
static void fail(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{ h2_graphics_stop(c, ip, address, value, 0, H2_NV2A_UNSUPPORTED_OPERATION); }
static int mapped(uint32_t address, uint32_t bytes)
{
    uint32_t arena = xk_mem_arena_size();
    if (!address || !bytes || arena < 4096 || (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t p = address >> 12; p <= last; ++p)
        if ((g_xpt[p] & 4095) || (uint64_t)g_xpt[p] + 4096 > arena - 4096) return 0;
    return 1;
}
static void stack(xctx *c, uint32_t ip, unsigned args)
{ if ((c->r[4] & 3) || !mapped(c->r[4], 4 + 4 * args)) fail(c, ip, c->r[4], args); }
static void output(xctx *c, uint32_t ip, uint32_t address, unsigned bytes)
{ if (!mapped(address, bytes)) fail(c, ip, address, bytes); }
static void put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i); }
static void sample_bytes(uint8_t *p, const h2_pad_sample *s)
{
    put16(p, s->buttons); memcpy(p + 2, s->analog, 8);
    for (unsigned i = 0; i < 4; ++i) put16(p + 10 + i * 2, (uint16_t)s->axes[i]);
}
void h2_input_init(xctx *c)
{
    const uint32_t ip = 0x4098C0; stack(c, ip, 2);
    uint32_t count = X_ARG(0), address = X_ARG(1), entries[6];
    if (initialized || count != 3 || !mapped(address, sizeof entries)) fail(c, ip, address, count);
    x_guest_read(entries, address, sizeof entries);
    for (unsigned i = 0; i < 3; ++i)
        if (entries[i * 2] != types[i] || entries[i * 2 + 1] != 4) fail(c, ip, address, i);
    for (unsigned i = 0; i < 4; ++i) output(c, ip, types[i], 12);
    h2_pad_sample current = {0};
    int result = h2_platform_pad(&current, 1);
    if (result < 0) fail(c, ip, address, (uint32_t)result);
    uint32_t masks[3] = {1, 1, 0}; /* real built-in pad inserted on port zero */
    x_guest_write(types[0], masks, sizeof masks);
    memset(masks, 0, sizeof masks);
    for (unsigned i = 1; i < 4; ++i) x_guest_write(types[i], masks, sizeof masks);
    previous = current; packet = 1; initialized = 1;
    xv_logf("[h2/input] real Vita pad initialized on port0; MU/voice absent, rumble unsupported\n");
    X_RET(2); /* XInitDevices is void; preserve EAX and all other CPU state. */
}
void h2_input_open(xctx *c)
{
    const uint32_t ip = 0x409932; stack(c, ip, 4);
    uint32_t type = X_ARG(0), port = X_ARG(1), slot = X_ARG(2), polling = X_ARG(3);
    if (!initialized || polling) fail(c, ip, polling, type);
    int known = 0; for (unsigned i = 0; i < 4; ++i) known |= type == types[i];
    if (!known) fail(c, ip, type, port);
    if (type != types[0] || port || slot) { c->r[0] = 0; X_RET(4); }
    if (handle || next_handle == UINT32_MAX) fail(c, ip, handle, next_handle);
    handle = next_handle++; c->r[0] = handle;
    xv_logf("[h2/input] opened built-in pad handle=%08X\n", handle);
    X_RET(4);
}
void h2_input_close(xctx *c)
{
    const uint32_t ip = 0x409988; stack(c, ip, 1);
    if (!initialized || !handle || X_ARG(0) != handle) fail(c, ip, X_ARG(0), handle);
    handle = 0; X_RET(1);
}
uint32_t h2_newfn_window;   /* polls left in the first-entry function trace window (boot.c) */
void h2_input_state(xctx *c)
{
    const uint32_t ip = 0x409B6C; stack(c, ip, 2);
    uint32_t destination = X_ARG(1); output(c, ip, destination, 22);
    if (!initialized || !handle || X_ARG(0) != handle) { c->r[0] = 1167; X_RET(2); }
    h2_pad_sample current = {0};
    int result = h2_platform_pad(&current, 0);
    if (result < 0) fail(c, ip, destination, (uint32_t)result);
    uint8_t bytes[22], old[18]; sample_bytes(bytes + 4, &current); sample_bytes(old, &previous);
    static unsigned polls;                       /* poll cadence + button evidence for the menu */
    if ((current.buttons & 0x0010u) && !(previous.buttons & 0x0010u) && !h2_newfn_window) {
        h2_newfn_window = 300;                   /* START edge: trace first-entry functions for ~300 polls */
        xv_logf("[h2/newfn] window opened at poll #%u gtime=%u\n", polls + 1, X_M32(0x54D5B8u));
    } else if (h2_newfn_window && !--h2_newfn_window) {
        xv_logf("[h2/newfn] window closed at poll #%u gtime=%u\n", polls + 1, X_M32(0x54D5B8u));
    }
    if (memcmp(bytes + 4, old, sizeof old)) { ++packet; previous = current; }
    put32(bytes, packet); x_guest_write(destination, bytes, sizeof bytes);
    /* gtime = the game's millisecond clock 0x54D5B8 (the attract/idle logic at 0x2239C0 measures idle
     * time against threshold 0x4701BC with it); against the emulator log's wall clock this shows how
     * fast game time advances per rendered frame. Read-only. */
    if (!(++polls % 64) || (current.buttons && !(polls % 4)))
        xv_logf("[h2/input] poll #%u buttons=%04X packet=%u gtime=%u idle_flag=%u idle_threshold=%u\n",
                polls, current.buttons, packet, X_M32(0x54D5B8u), X_M8(0x4701B8u), X_M32(0x4701BCu));
    c->r[0] = 0; X_RET(2);
}
void h2_input_capabilities(xctx *c)
{
    const uint32_t ip = 0x409994; stack(c, ip, 2);
    uint32_t destination = X_ARG(1); output(c, ip, destination, 25);
    if (!initialized || !handle || X_ARG(0) != handle) { c->r[0] = 1167; X_RET(2); }
    uint8_t bytes[25] = {1}; /* gamepad subtype; reserved and rumble remain zero */
    put16(bytes + 3, 0x3F); /* D-pad, start, back; no physical stick clicks */
    for (unsigned i = 0; i < 8; ++i) bytes[5 + i] = (i == 4 || i == 5) ? 0 : 255;
    memset(bytes + 13, 255, 8); /* all bits of four analog axes available */
    x_guest_write(destination, bytes, sizeof bytes); c->r[0] = 0; X_RET(2);
}
void h2_input_feedback(xctx *c)
{
    const uint32_t ip = 0x409BDF; stack(c, ip, 2);
    uint32_t destination = X_ARG(1); output(c, ip, destination, 70);
    if (!initialized || !handle || X_ARG(0) != handle) { c->r[0] = 1167; X_RET(2); }
    /* Same immediate error as the original driver's no-output-endpoint path.
     * No asynchronous completion or event signal is promised. */
    uint32_t status = 50; x_guest_write(destination, &status, 4);
    c->r[0] = status; X_RET(2);
}
