/*
 * harness.c - host-side boot of the recompiled game (Linux/x86-64).
 * Builds the guest memory arena + page table, loads the flattened XBE image into its own pages, brings the
 * kernel translator up and starts the XBE entry point on a guest thread - exactly like the Xbox kernel: the
 * game's own XAPI start-up code (lifted) then creates the main thread, initialises heap/TLS/drive letters
 * and calls main().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xv_x86rt.h"
#include "../kernel/xk.h"

uint8_t *g_xram;
extern const uint32_t xv_game_main, xv_game_tls_dir;

int main(int argc, char **argv)
{
    const char *img = argc > 1 ? argv[1] : "halo_image.bin";
    const char *game_dir = argc > 2 ? argv[2] : "../haloce";
    const char *save_dir = argc > 3 ? argv[3] : "host/save";
    FILE *f = fopen(img, "rb");
    if (!f) { perror(img); return 1; }
    uint32_t base, size;
    if (fread(&base, 4, 1, f) != 1 || fread(&size, 4, 1, f) != 1) return 1;
    xk_mem_setup(base, size);
    g_xram = calloc(xk_mem_arena_size(), 1);
    if (fread(g_xram + xk_mem_image_arena_offset(), 1, size, f) != size) { fprintf(stderr, "short image\n"); return 1; }
    fclose(f);
    fprintf(stderr, "[harness] image %u KB at %08X (arena %u MB), entry %08X, main() = %08X, tls dir %08X\n",
            size >> 10, base, xk_mem_arena_size() >> 20, xv_entry_point, xv_game_main, xv_game_tls_dir);

    extern void xv_trace_init(void); xv_trace_init();
    xk_init(base, size, xv_game_tls_dir, game_dir, save_dir);
    xk_thread_create(0x10000, 0, xv_entry_point, 0, 0, 0);     /* the initial thread runs mainCRTStartup() */
    xk_run_until_idle();
    fprintf(stderr, "[harness] done\n");
    return 0;
}
