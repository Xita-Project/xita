/*
 * xv_boot.c - Stage 4 on hardware: boot the recompiled Halo engine on the Vita.
 *
 * This is the app-assembly seam.  It owns guest memory (the ONE definition of g_xram on the Vita),
 * brings the kernel translator up, loads the flattened XBE image, and runs the recompiled entry point
 * on the kernel's own cooperative scheduler.  main.c provides GXM + present; xv_ui_gxm.c bridges the
 * recompiled engine's draws to GXM.
 *
 * Memory: the recompiler addresses guest memory through a 4 KB page table (X_G, recomp/kernel/xk_mem.c).
 * Here the whole arena - physical [0,64MB) + the XBE image copy + a trash page - is a single
 * sceGxmMapMemory'd USER_RW block, so a texture control word pointing at guest physical memory
 * (X_G(0x80000000|data) == g_xram + data) is read by the GPU with zero copies.
 */
#if defined(__vita__)

#include <stdint.h>
#include <string.h>

#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>

#include "recomp/xv_x86rt.h"
#include "recomp/kernel/xk.h"

#include "xv_log.h"
#define BOOT_LOG(...)  xv_logf("[xv/boot] " __VA_ARGS__)
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((uint32_t)(a) - 1))

/* THE definition of guest memory on the Vita (extern in xv_x86rt.h; host defines it in harness.c). */
uint8_t *g_xram;
static SceUID g_xram_uid = -1;

/* Emitted by the recompiler into recomp/xv_fn_table.c. */
extern const uint32_t xv_entry_point;
extern const uint32_t xv_game_tls_dir;

/* Try each path until one opens (VPK-packed image first, then the SD card). */
static SceUID open_first(const char *const *paths, int n)
{
    for (int i = 0; i < n; ++i) {
        SceUID fd = sceIoOpen(paths[i], SCE_O_RDONLY, 0);
        if (fd >= 0) { BOOT_LOG("image: %s\n", paths[i]); return fd; }
    }
    return -1;
}

/* Runs on its own Vita thread (core 0).  Returns when the game exits / deadlocks. */
int xv_boot_recomp(const char *game_dir, const char *save_dir)
{
    const char *img_paths[] = {
        "app0:halo_image.bin",
        "ux0:data/xita/halo_image.bin",
        "uma0:data/xita/halo_image.bin",
    };
    SceUID fd = open_first(img_paths, 3);
    if (fd < 0) { BOOT_LOG("halo_image.bin not found (app0 / ux0 / uma0)\n"); return -1; }

    uint32_t base = 0, size = 0;
    if (sceIoRead(fd, &base, 4) != 4 || sceIoRead(fd, &size, 4) != 4) { sceIoClose(fd); return -1; }
    BOOT_LOG("image base %08X, %u KB\n", base, size >> 10);

    /* Page table + arena bookkeeping (computes xk_mem_arena_size / image offset). */
    xk_mem_setup(base, size);
    uint32_t arena = ALIGN_UP(xk_mem_arena_size(), 4 * 1024);

    g_xram_uid = sceKernelAllocMemBlock("xv_arena", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, arena, NULL);
    if (g_xram_uid < 0) { BOOT_LOG("arena alloc (%u MB) failed: 0x%08X\n", arena >> 20, g_xram_uid); sceIoClose(fd); return -1; }
    sceKernelGetMemBlockBase(g_xram_uid, (void **)&g_xram);
    xk_mem_bind_arena();                                  /* g_img_base for flat image-address access (X_IMG*) */
    { uint32_t lo = xk_mem_image_lo(), hi = xk_mem_image_hi();
      if (lo != (base & ~0xFFFu) || hi != ((base + size + 0xFFFu) & ~0xFFFu)) {
          BOOT_LOG("FATAL image bounds %08X..%08X disagree with base %08X size %X - X_IMG would read wrong memory; aborting\n", lo, hi, base, size);
          sceIoClose(fd); return -1; } }
    int err = sceGxmMapMemory(g_xram, arena, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
    if (err != SCE_OK) { BOOT_LOG("sceGxmMapMemory(arena) failed: 0x%08X\n", err); sceIoClose(fd); return -1; }
    memset(g_xram, 0, arena);
    BOOT_LOG("arena %u MB @ %p, GXM-mapped R/W\n", arena >> 20, g_xram);

    /* Load the flattened image into its own arena pages (past physical RAM). */
    /* sceIoRead can return short on the memory card (seen once right after a USB session: 3.2 of 3.8 MB):
     * keep reading until the whole image is in, and only give up on an error or a zero-length read */
    uint8_t *dst = g_xram + xk_mem_image_arena_offset(); unsigned got = 0; int tries = 0;
    while (got < size) {
        int r = sceIoRead(fd, dst + got, size - got);
        if (r < 0) { BOOT_LOG("image read error 0x%08X at %u / %u\n", r, got, size); break; }
        if (r == 0) { if (++tries > 8) break; sceKernelDelayThread(20000); continue; }
        got += (unsigned)r; if (got < size) BOOT_LOG("short image read (%u / %u), continuing\n", got, size);
    }
    sceIoClose(fd);
    if (got != size) { BOOT_LOG("image incomplete (%u / %u)\n", got, size); return -1; }

    /* Kernel up: drive letters, TLS template, thunk table.  game_dir holds maps/, save_dir is writable. */
    xk_init(base, size, xv_game_tls_dir, game_dir, save_dir);

    /* The initial thread runs mainCRTStartup(); the kernel scheduler takes it from there. */
    if (!xk_thread_create(0x10000, 0, xv_entry_point, 0, 0, 0)) { BOOT_LOG("entry thread create failed\n"); return -1; }

    BOOT_LOG("running recompiled engine (entry %08X)\n", xv_entry_point);
    xk_run_until_idle();
    BOOT_LOG("engine returned\n");
    return 0;
}

void xv_boot_free_arena(void)
{
    if (g_xram_uid >= 0) { sceGxmUnmapMemory(g_xram); sceKernelFreeMemBlock(g_xram_uid); g_xram_uid = -1; g_xram = NULL; }
}

#endif /* __vita__ */
