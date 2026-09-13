/* Isolated native startup harness: executes the owned XBE entry and scheduler.
 * No title screen substitute, success-return API fallbacks or CE game adapter.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include "xv_x86rt.h"
#include "kernel/xk.h"
#include "cache_volume.h"
#include "gpu_bus.h"
#include "instance_memory.h"
#include "scanout.h"
#include "input.h"
#include "command_snapshot.h"
#include "kernel_stack.h"
#include "kernel_timer.h"
#include "fp_environment.h"
extern const h2_host_channel *h2_host_channel_current(void) __attribute__((weak));

unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;
uint8_t *g_xram;
volatile uint32_t xv_cur_fn;
int xv_trace_enabled = 1, xv_trace_funcs = 1, xv_watch_n;
extern const uint32_t xv_game_tls_dir;
static SceUID log_fd = -1;
static const char disk_header_path[] = "ux0:data/xita-halo2/save/disk-header.bin";
static unsigned presented_frames;
static SceDisplayFrameBuf active_display;

static int prepare_disk_header(void)
{
    /* The observed XAPI startup reads/writes its cache allocation table at
     * raw-disk offset 0x800. Persist real bytes in a private header area; a
     * newly provisioned virtual disk is zeroed and the guest formats the table.
     * Access beyond this header area remains unsupported (ordinary EOF).
     */
    FILE *disk = fopen(disk_header_path, "rb");
    if (disk) { fclose(disk); return 0; }
    disk = fopen(disk_header_path, "wb");
    if (!disk) return -1;
    const unsigned char zero[512] = {0};
    for (unsigned i = 0; i < 2048; ++i) {
        if (fwrite(zero, 1, sizeof zero, disk) != sizeof zero) { fclose(disk); return -1; }
    }
    return fclose(disk);
}

void xv_logf(const char *fmt, ...)
{
    char buffer[1024];
    va_list ap; va_start(ap, fmt);
    int length = vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    if (length <= 0) return;
    if ((unsigned)length >= sizeof buffer) length = sizeof buffer - 1;
    sceClibPrintf("%s", buffer);
    if (log_fd >= 0) sceIoWrite(log_fd, buffer, length);
}
void xv_log_flush(void) { if (log_fd >= 0) sceIoSyncByFd(log_fd, 0); }
uint64_t h2_graphics_time_us(void) { return sceKernelGetSystemTimeWide(); }
static int16_t pad_axis(uint8_t value, int invert)
{
    int32_t axis = value < 128 ? ((int32_t)value - 128) * 256 :
                              ((int32_t)value - 128) * 32767 / 127;
    if (invert) axis = -axis;
    if (axis > 32767) axis = 32767;
    return (int16_t)axis;
}
int h2_platform_pad(h2_pad_sample *sample, int initialize)
{
    if (initialize) {
        int result = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
        if (result < 0) return result;
    }
    SceCtrlData pad = {0};
    int result = sceCtrlPeekBufferPositive(0, &pad, 1);
    if (result < 1) return result < 0 ? result : -1;
    memset(sample, 0, sizeof *sample);
    const uint32_t digital[] = {SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT,
                               SCE_CTRL_RIGHT, SCE_CTRL_START, SCE_CTRL_SELECT};
    const uint32_t analog[] = {SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE,
                              SCE_CTRL_TRIANGLE, 0, 0, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER};
    for (unsigned i = 0; i < 6; ++i) if (pad.buttons & digital[i]) sample->buttons |= 1u << i;
    for (unsigned i = 0; i < 8; ++i) if (pad.buttons & analog[i]) sample->analog[i] = 255;
    sample->axes[0] = pad_axis(pad.lx, 0); sample->axes[1] = pad_axis(pad.ly, 1);
    sample->axes[2] = pad_axis(pad.rx, 0); sample->axes[3] = pad_axis(pad.ry, 1);
    return 0;
}
int h2_platform_wait_vblank(uint32_t *before, uint32_t *after)
{
    *before = (uint32_t)sceDisplayGetVcount();
    int result = sceDisplayWaitVblankStart();
    *after = (uint32_t)sceDisplayGetVcount();
    return result;
}
int h2_platform_present(const uint8_t *pixels, size_t bytes,
                          const uint8_t *rgb_gamma, uint32_t *vcount)
{
    static SceUID blocks[2] = {-1, -1};
    static uint8_t *buffers[2];
    static unsigned back;
    if (!buffers[0]) {
        for (unsigned i = 0; i < 2; ++i) {
            blocks[i] = sceKernelAllocMemBlock("h2_scanout", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x200000, NULL);
            if (blocks[i] < 0 || sceKernelGetMemBlockBase(blocks[i], (void **)&buffers[i]) < 0) {
                for (unsigned j = 0; j <= i; ++j) {
                    if (blocks[j] >= 0) sceKernelFreeMemBlock(blocks[j]);
                    blocks[j] = -1; buffers[j] = NULL;
                }
                return -1;
            }
        }
    }
    if (!h2_scanout_convert(buffers[back], 0x200000, pixels, bytes, rgb_gamma, 768)) return -1;
    SceDisplayFrameBuf frame = {0};
    frame.size = sizeof frame; frame.base = buffers[back];
    frame.pitch = frame.width = H2_DISPLAY_WIDTH; frame.height = H2_DISPLAY_HEIGHT;
    frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    int status = sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (status < 0) return status;
    status = sceDisplayWaitVblankStart();
    if (status < 0) return status;
    *vcount = (uint32_t)sceDisplayGetVcount();
    active_display = frame;
    ++presented_frames;
    FILE *snapshot = presented_frames == 1 ? fopen("ux0:data/xita-halo2/scanout-last.bin", "wb") : NULL;
    if (snapshot) {
        uint32_t header[4] = {H2_DISPLAY_WIDTH, H2_DISPLAY_HEIGHT, H2_DISPLAY_WIDTH * 4, presented_frames};
        int complete = fwrite(header, 1, sizeof header, snapshot) == sizeof header &&
                       fwrite(buffers[back], 1, H2_DISPLAY_BYTES, snapshot) == H2_DISPLAY_BYTES;
        int closed = fclose(snapshot);
        xv_logf("[h2/display] private scanout snapshot frame=%u complete=%d\n", presented_frames, complete && !closed);
    }
    back ^= 1;
    return 0;
}
int h2_platform_blank(int blank)
{
    if (!active_display.base) return -1;
    int result = sceDisplaySetFrameBuf(blank ? NULL : &active_display, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (result < 0) return result;
    return sceDisplayWaitVblankStart();
}
static void graphics_snapshot(void)
{
    /* Stop-only capture: keep the latest successfully presented buffer without
     * adding file I/O to recurring presentation. It can remain stored while
     * the display is blanked; this records the last presented frame, not an
     * assertion about current scanout enable state. */
    if (presented_frames && active_display.base) {
        FILE *frame = fopen("ux0:data/xita-halo2/last-presented-at-stop.bin", "wb");
        if (frame) {
            uint32_t header[4] = {active_display.width, active_display.height,
                                  active_display.pitch * 4, presented_frames};
            int complete = fwrite(header, 1, sizeof header, frame) == sizeof header &&
                           fwrite(active_display.base, 1, H2_DISPLAY_BYTES, frame) == H2_DISPLAY_BYTES;
            int closed = fclose(frame);
            xv_logf("[h2/display] private last-presented snapshot frame=%u complete=%d\n",
                    presented_frames, complete && !closed);
        }
    }
    const h2_host_channel *channel = h2_host_channel_current ? h2_host_channel_current() : NULL;
    if (channel) {
        FILE *state = fopen("ux0:data/xita-halo2/channel-at-stop.json", "wb");
        if (state) {
            int complete = h2_command_snapshot(state, channel);
            int closed = fclose(state);
            xv_logf("[h2/graphics] private decoded channel snapshot complete=%d\n", complete && !closed);
        }
    }
    /* Diagnostic state from the pinned image's static device. This file may
     * contain owned game data and belongs only in the private emulator lab. */
    static uint8_t device[0x24A0];
    x_guest_read(device, 0x404FE0u, sizeof device);
    FILE *snapshot = fopen("ux0:data/xita-halo2/device-at-stop.bin", "wb");
    if (!snapshot) return;
    size_t written = fwrite(device, 1, sizeof device, snapshot);
    int closed = fclose(snapshot);
    xv_logf("[h2/graphics] private device snapshot base=00404FE0 bytes=%u complete=%d\n",
            (unsigned)written, written == sizeof device && closed == 0);
    uint32_t ring = X_M32(0x404FE0u + 0x24), end = X_M32(0x404FE0u + 0x28);
    if (ring < 0x80000000u || end <= ring || end > 0x84000000u || end - ring > 0x100000u) return;
    uint32_t header[4] = {ring - 0x80000000u, end - ring, X_M32(0x404FE0u) & 0x0FFFFFFFu, 0};
    snapshot = fopen("ux0:data/xita-halo2/push-at-stop.bin", "wb");
    if (!snapshot) return;
    /* Raw physical host RAM, bounded independently of guest page mappings.
     * Header: physical base, allocation bytes, guest cursor, reserved zero. */
    int complete = fwrite(header, 1, sizeof header, snapshot) == sizeof header &&
                   fwrite(g_xram + header[0], 1, header[1], snapshot) == header[1];
    closed = fclose(snapshot);
    xv_logf("[h2/graphics] private push snapshot base=%08X bytes=%u cursor=%08X complete=%d\n",
            header[0], header[1], header[2], complete && closed == 0);
}
void h2_graphics_stop(xctx *context, uint32_t instruction, uint32_t address,
                      uint32_t value, int write, int reason)
{
    (void)context;
    h2_gpu_bus_report();
    graphics_snapshot();
    xv_logf("[h2/blocked] NV2A %s eip=%08X address=%08X value=%08X reason=%d\n",
            write ? "write" : "read", instruction, address, value, reason);
    xv_log_flush();
    if (presented_frames) sceKernelDelayThread(3000000); /* retain the actual stopped image for capture */
    sceKernelExitProcess(25);
    for (;;) sceKernelDelayThread(1000);
}
void xv_check_guest_address(uint32_t address)
{
    if (h2_kernel_stack_unmapped(address))
        h2_kernel_stack_fault(xk_cur ? &xk_cur->ctx : NULL, "unmapped stack window", address, 0);
    /* Accesses that bypass the explicit bus adapter must not alias the
     * runtime's shared unmapped-memory trash page. */
    int ohci = address >= 0xFED00000u && address < 0xFED01000u;
    int apu = address >= 0xFE800000u && address < 0xFE880000u;
    if (ohci || apu || (address >= 0xFD000000u && address < 0xFE000000u)) {
        h2_gpu_bus_report();
        graphics_snapshot();
        xv_logf("[h2/blocked] %s MMIO address=%08X fn=%08X\n", ohci ? "OHCI" : apu ? "MCPX APU" : "NV2A", address, xv_cur_fn);
        xv_log_flush();
        sceKernelExitProcess(24);
        /* Vita3K can return briefly while process teardown is pending. */
        for (;;) sceKernelDelayThread(1000);
    }
}
void h2_kernel_stack_fault(xctx *c, const char *reason, uint32_t first, uint32_t second)
{
    graphics_snapshot();
    xv_logf("[h2/blocked] kernel stack reason=%s first=%08X second=%08X fn=%08X esp=%08X\n",
            reason, first, second, xv_cur_fn, c ? c->r[4] : 0);
    xv_log_flush();
    sceKernelExitProcess(26);
    for (;;) sceKernelDelayThread(1000);
}
void h2_timer_fault(xctx *c, const char *reason, uint32_t first, uint32_t second)
{
    graphics_snapshot();
    xv_logf("[h2/blocked] timer reason=%s first=%08X second=%08X fn=%08X esp=%08X\n",
            reason, first, second, xv_cur_fn, c ? c->r[4] : 0);
    xv_log_flush();
    sceKernelExitProcess(27);
    for (;;) sceKernelDelayThread(1000);
}
uint32_t h2_platform_fpscr_read(void)
{
    uint32_t value;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(value) : : "memory");
    return value;
}
void h2_platform_fpscr_write(uint32_t value)
{ __asm__ volatile("vmsr fpscr, %0" : : "r"(value) : "memory", "vfpcc"); }
void h2_fp_environment_fault(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{
    graphics_snapshot();
    xv_logf("[h2/blocked] FP environment ip=%08X address=%08X value=%08X esp=%08X\n",
            ip, address, value, c->r[4]);
    xv_log_flush();
    sceKernelExitProcess(28);
    for (;;) sceKernelDelayThread(1000);
}
void xv_watch_enter(uint32_t address, xctx *c) { (void)address; (void)c; }
void xv_watch_leave(uint32_t address, uint32_t back, xctx *c) { (void)address; (void)back; (void)c; }
static void trace_mapped_word(uint32_t address)
{
    /* Diagnostic reads must never resolve through the shared trash page. */
    uint32_t arena = xk_mem_arena_size();
    if (arena < 4096 || address > UINT32_MAX - 3) return;
    for (uint32_t page = address >> 12; page <= (address + 3) >> 12; ++page) {
        uint32_t offset = g_xpt[page];
        if ((offset & 4095) || (uint64_t)offset + 4096 > arena - 4096) return;
    }
    xv_logf("[h2/error-state] address=%08X word=%08X\n", address, X_M32(address));
}
void xv_trace_func(uint32_t address)
{
    static unsigned count;
    static unsigned movie_probe_count;
    if (xk_cur && movie_probe_count < 16 &&
        (address == 0x372030 || address == 0x3E97E0 || address == 0x3E9750 || address == 0x3E9C70)) {
        const xctx *c = &xk_cur->ctx;
        ++movie_probe_count;
        xv_logf("[h2/movie] fn=%08X return=%08X eax=%08X ebx=%08X flags=%08X\n",
                address, X_M32(c->r[4]), c->r[0], c->r[3], xf_eflags((xctx *)c));
        trace_mapped_word(0x466D6C); trace_mapped_word(0x5637DC);
        trace_mapped_word(0x484B00); trace_mapped_word(0x484B10); trace_mapped_word(0x484B28);
    }
    static unsigned error_path_count;
    if (xk_cur && error_path_count < 64 &&
        (address == 0x13F10 || address == 0x163820 || address == 0x163890 ||
         address == 0x223240 || address == 0x12B450 || address == 0x18E810 ||
         address == 0x68250 || address == 0x2C8A0 || address == 0x13C20 ||
         address == 0x13CD0 || address == 0x214940 || address == 0x213484 ||
         address == 0x2133A1 || address == 0x121B00 || address == 0x18EAA0)) {
        const xctx *c = &xk_cur->ctx;
        ++error_path_count;
        xv_logf("[h2/error-path] fn=%08X return=%08X eax=%08X ecx=%08X edx=%08X esi=%08X\n",
                address, X_M32(c->r[4]), c->r[0], c->r[1], c->r[2], c->r[6]);
        if (address == 0x68250 || address == 0x223240) {
            const uint32_t globals[] = {0x4CF770, 0x4CF77C, 0x4E6948,
                                       0x4E6470, 0x4E64A0, 0x4E9BB8,
                                       0x51EA00, 0x51EA04, 0x47004C};
            for (unsigned i = 0; i < sizeof globals / sizeof *globals; ++i)
                trace_mapped_word(globals[i]);
            /* These pointer slots themselves are owned image-backed data. */
            uint32_t state = X_M32(0x4E6948), mode = X_M32(0x4CF77C);
            if (state && state <= UINT32_MAX - 0x1123) {
                trace_mapped_word(state); trace_mapped_word(state + 0x1120);
            }
            if (mode && mode <= UINT32_MAX - 0x1B) {
                trace_mapped_word(mode + 8); trace_mapped_word(mode + 0x18);
            }
        }
    }
    /* Read-only evidence for the pinned 1088E0 descriptor walk. The exact
     * record array is image-backed; reject pointers outside it before reads. */
    static int descriptor_chain_logged;
    if (!descriptor_chain_logged && (address == 0x175F40u || address == 0x106460u) &&
        xk_cur && X_M32(xk_cur->ctx.r[4]) == 0x10894Du) {
        descriptor_chain_logged = 1;
        uint32_t node = X_M32(0x4E0330u);
        unsigned seen = 0;
        for (unsigned n = 0; node && n < 17; ++n) {
            if (node < 0x4678E8u || node > 0x468568u || (node - 0x4678E8u) % 0xC8u) {
                xv_logf("[h2/startup] descriptor chain unexpected node=%08X\n", node);
                break;
            }
            unsigned bit = 1u << ((node - 0x4678E8u) / 0xC8u);
            if (seen & bit) { xv_logf("[h2/startup] descriptor chain cycle node=%08X\n", node); break; }
            seen |= bit;
            xv_logf("[h2/startup] descriptor index=%u node=%08X initializer=%08X next=%08X\n",
                    n, node, X_M32(node + 0x10u), X_M32(node + 0xC4u));
            node = X_M32(node + 0xC4u);
        }
    }
    if (address == 0x3F5240u && xk_cur) {
        xctx *c = &xk_cur->ctx;
        uint32_t pp = X_M32(c->r[4] + 4);
        xv_logf("[h2/graphics] CreateDevice LTCG flags=%08X output=%08X parameters=%08X width=%u height=%u format=%08X\n",
                c->r[0], c->r[1], pp, X_M32(pp), X_M32(pp + 4), X_M32(pp + 8));
    }
    if (address == 0x3FC530u && xk_cur) {
        xctx *c = &xk_cur->ctx;
        xv_logf("[h2/graphics] device release caller=%08X device=%08X esi=%08X\n",
                X_M32(c->r[4]), c->r[0], c->r[6]);
    }
    /* XAPI's direct application call targets 0x12190 in the pinned image.
     * Keep that observation even if constructor tracing uses the initial cap. */
    if (++count <= 2000 || (count % 100000) == 0 || address == 0x12190u) {
        const xctx *c = xk_cur ? &xk_cur->ctx : NULL;
        xv_logf("[h2/function] #%u thread=%d eip=%08X eax=%08X ecx=%08X esp=%08X\n",
                count, xk_cur ? xk_cur->id : 0, address,
                c ? c->r[0] : 0, c ? c->r[1] : 0, c ? c->r[4] : 0);
    }
}
void xv_trace_call(xctx *c, const char *name, unsigned count)
{
    char args[160]; unsigned used = 0;
    for (unsigned i = 0; i < count && i < 8; ++i)
        used += snprintf(args + used, sizeof args - used, " %08X", X_ARG(i));
    if (!count) args[0] = 0;
    xv_logf("[h2/kernel] thread=%d return=%08X %s%s\n",
            xk_cur ? xk_cur->id : 0, X_M32(c->r[4]), name, args);
}
void xv_boot_missing_kernel(xctx *c, const char *name)
{
    xv_logf("[h2/blocked] kernel=%s fn=%08X return=%08X eax=%08X esp=%08X\n",
            name, xv_cur_fn, X_M32(c->r[4]), c->r[0], c->r[4]);
    xv_log_flush();
    sceKernelExitProcess(21);
    for (;;) sceKernelDelayThread(1000);
}
void __wrap_xv_unimpl(xctx *c, uint32_t address, const char *name)
{
    graphics_snapshot();
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    xv_logf("[h2/fp] native FPSCR=%08X guest FCW=%04X FSW=%04X\n", fpscr, c->fcw, c->fsw);
    xv_logf("[h2/blocked] instruction=%s address=%08X fn=%08X eax=%08X ecx=%08X esp=%08X\n",
            name, address, xv_cur_fn, c->r[0], c->r[1], c->r[4]);
    xv_log_flush();
    sceKernelExitProcess(22);
    for (;;) sceKernelDelayThread(1000);
}
void xv_runtime_trap(xctx *c, uint32_t address)
{
    xv_logf("[h2/blocked] guest trap address=%08X fn=%08X eax=%08X ecx=%08X esp=%08X return=%08X\n",
            address, xv_cur_fn, c->r[0], c->r[1], c->r[4], X_M32(c->r[4]));
    if (xv_cur_fn >= 0x3E0000 && xv_cur_fn < 0x3F0000) {
        trace_mapped_word(0x466D6C); trace_mapped_word(0x5637DC);
        trace_mapped_word(0x484B00); trace_mapped_word(0x484B10); trace_mapped_word(0x484B28);
    }
    graphics_snapshot();
    xv_log_flush();
    sceKernelExitProcess(23);
    for (;;) sceKernelDelayThread(1000);
}

int main(void)
{
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/xita-halo2", 0777);
    sceIoMkdir("ux0:data/xita-halo2/save", 0777);
    log_fd = sceIoOpen("ux0:data/xita-halo2/boot.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    xv_logf("[h2/boot] native XBE startup harness, title XH2B00001\n");
    FILE *input = fopen("app0:halo2_image.bin", "rb");
    uint32_t base, size;
    if (!input || fread(&base, 4, 1, input) != 1 || fread(&size, 4, 1, input) != 1) {
        xv_logf("[h2/blocked] unable to read image header\n"); return 1;
    }
    if (base != 0x10000u || size != 0x5754C0u || xv_entry_point != 0x2D0AEEu) {
        xv_logf("[h2/blocked] image identity fields disagree with profile\n"); return 2;
    }
    xk_mem_setup(base, size);
    uint32_t arena_size = (xk_mem_arena_size() + 4095u) & ~4095u;
    SceUID arena = sceKernelAllocMemBlock("halo2_guest", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, arena_size, NULL);
    if (arena < 0 || sceKernelGetMemBlockBase(arena, (void **)&g_xram) < 0) {
        xv_logf("[h2/blocked] arena allocation %u bytes failed: %08X\n", arena_size, arena); return 3;
    }
    memset(g_xram, 0, arena_size);
    xk_mem_bind_arena();
    if (fread(g_xram + xk_mem_image_arena_offset(), 1, size, input) != size) {
        xv_logf("[h2/blocked] incomplete image read\n"); return 4;
    }
    fclose(input);
    xv_logf("[h2/boot] image=%08X+%X arena=%u entry=%08X tls=%08X\n",
            base, size, arena_size, xv_entry_point, xv_game_tls_dir);
    if (h2_cache_mounts() != 0) {
        xv_logf("[h2/blocked] private cache volume provisioning failed\n"); return 7;
    }
    if (h2_instance_memory_init() != 0) {
        xv_logf("[h2/blocked] GPU instance range reservation failed\n"); return 8;
    }
    xk_init(base, size, xv_game_tls_dir, "ux0:data/xita-halo2/game", "ux0:data/xita-halo2/save");
    xk_file_set_ce_adapter_enabled(0);
    xk_file_set_balanced_lifetime(1);
    h2_gpu_bus_reset(64u * 1024u * 1024u);
    if (prepare_disk_header() != 0) {
        xv_logf("[h2/blocked] private disk header storage unavailable\n"); return 6;
    }
    xk_path_mount("\\device\\harddisk0\\partition0", disk_header_path);
    if (!xk_thread_create(0x10000, 0, xv_entry_point, 0, 0, 0)) {
        xv_logf("[h2/blocked] initial guest thread allocation failed\n"); return 5;
    }
    xv_logf("[h2/boot] entering guest scheduler\n");
    xk_run_until_idle();
    xv_logf("[h2/boot] scheduler returned\n");
    graphics_snapshot();
    xv_log_flush();
    return 0;
}
