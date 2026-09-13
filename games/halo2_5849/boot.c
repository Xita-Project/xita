/* Isolated native startup harness: executes the owned XBE entry and scheduler.
 * No title screen substitute, success-return API fallbacks or CE game adapter.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include "xv_x86rt.h"
#include "kernel/xk.h"
#include "cache_volume.h"

unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;
uint8_t *g_xram;
volatile uint32_t xv_cur_fn;
int xv_trace_enabled = 1, xv_trace_funcs = 1, xv_watch_n;
extern const uint32_t xv_game_tls_dir;
static SceUID log_fd = -1;
static const char disk_header_path[] = "ux0:data/xita-halo2/save/disk-header.bin";

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
void xv_watch_enter(uint32_t address, xctx *c) { (void)address; (void)c; }
void xv_watch_leave(uint32_t address, uint32_t back, xctx *c) { (void)address; (void)back; (void)c; }
void xv_trace_func(uint32_t address)
{
    static unsigned count;
    if (address == 0x3F5240u && xk_cur) {
        xctx *c = &xk_cur->ctx;
        uint32_t pp = X_M32(c->r[4] + 4);
        xv_logf("[h2/graphics] CreateDevice LTCG flags=%08X output=%08X parameters=%08X width=%u height=%u format=%08X\n",
                c->r[0], c->r[1], pp, X_M32(pp), X_M32(pp + 4), X_M32(pp + 8));
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
}
void __wrap_xv_unimpl(xctx *c, uint32_t address, const char *name)
{
    xv_logf("[h2/blocked] instruction=%s address=%08X fn=%08X eax=%08X ecx=%08X esp=%08X\n",
            name, address, xv_cur_fn, c->r[0], c->r[1], c->r[4]);
    xv_log_flush();
    sceKernelExitProcess(22);
}
void xv_runtime_trap(xctx *c, uint32_t address)
{
    xv_logf("[h2/blocked] guest trap address=%08X fn=%08X eax=%08X ecx=%08X esp=%08X return=%08X\n",
            address, xv_cur_fn, c->r[0], c->r[1], c->r[4], X_M32(c->r[4]));
    xv_log_flush();
    sceKernelExitProcess(23);
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
    xk_init(base, size, xv_game_tls_dir, "ux0:data/xita-halo2/game", "ux0:data/xita-halo2/save");
    xk_file_set_ce_adapter_enabled(0);
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
    xv_log_flush();
    return 0;
}
