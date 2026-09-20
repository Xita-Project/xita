/* Host fixture for recomp/kernel/xk_render_view.c (increment A): links the real xk_mem.c.
 *   cc -std=gnu11 -O1 -g -fsanitize=address,undefined -DXV_THREAD_PAGE_TABLE=1 -DXV_RENDER_VIEW=1 -DXV_RENDER_VIEW_DEFAULT=1 \
 *      -I. -Irecomp -Irecomp/kernel -Iruntime tools/tests/render_view.c recomp/kernel/xk_render_view.c \
 *      recomp/kernel/xk_mem.c -o /tmp/render_view && /tmp/render_view */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xk.h"
#include "xk_render_view.h"
#include "xv_x86rt.h"

uint8_t *g_xram;
void xk_os_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
uint64_t xk_os_monotonic_us(void) { static uint64_t t; return t += 100; }
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void)
{
    const uint32_t image_base = 0x10000u, image_size = 0x20000u;   /* 32 image pages */
    setenv("XV_RENDER_VIEW", "1", 1); setenv("XV_RENDER_VIEW_LEARN_INTERVAL", "0", 1); setenv("XV_RENDER_VIEW_LEARN_PASSES", "2", 1);
    setenv("XV_RENDER_VIEW_SHADOW_PAGES", "16", 1);
    xk_mem_setup(image_base, image_size);
    uint32_t arena = xk_mem_arena_size();
    g_xram = calloc(arena, 1); assert(g_xram);
    xk_mem_bind_arena();
    xk_render_view_layout L; xk_mem_render_view_layout(&L);
    CHECK(L.shadow_pages == 16 && L.image_pages == 32 && L.shadow_off + 16 * 4096u == arena);
    uint32_t *live = g_xpt; CHECK(xv_host_page_table == live);
    /* an allocation alias: VA 0x00500000 -> physical page 0x61 (like a guest heap mapping) */
    xk_mem_map_alias(0x00500000u, 0x61000u);
    uint32_t vps[8]; unsigned n = xk_mem_page_aliases(0x61000u, vps, 8);
    CHECK(n == 4);   /* identity, 0x80000000, 0xF0000000, 0x00500000 */
    memset(g_xram + 0x61000u, 0x11, 4096); memset(g_xram + 0x62000u, 0x22, 4096);
    *(uint32_t *)(g_img_base + 0x2A000u) = 7;          /* image page 26 */
    xv_render_view_configure();
    CHECK(xv_render_view_enabled);
    /* before any learn pass the scene keeps the live table */
    { unsigned scope = 0; xv_render_view_enter(&scope, NULL); CHECK(scope == 0 && xv_host_page_table == live); xv_render_view_leave(&scope); }
    /* learn pass 1: baseline at present 0, mutate, diff at present 1 */
    xv_render_view_present(0);
    g_xram[0x61000u + 100] = 0x33;                       /* physical page 0x61 changes */
    *(uint32_t *)(g_img_base + 0x2A000u) = 8;            /* image page 26 changes */
    xv_render_view_present(1);
    /* enter: page 0x61 and its aliases must translate into the shadow region, others unchanged */
    unsigned scope = 0; xv_render_view_enter(&scope, NULL);
    CHECK(scope == 1 && xv_host_page_table != live && g_xpt == live);
    uint8_t *p = (uint8_t *)X_G(0x61000u), *q = (uint8_t *)X_G(0x80061000u), *r = (uint8_t *)X_G(0x00500000u);
    CHECK(p - g_xram >= L.shadow_off && p == q && p == r);
    CHECK(p[100] == 0x33 && p[0] == 0x11);               /* shadow holds the live content */
    CHECK((uint8_t *)X_G(0x62000u) == g_xram + 0x62000u);
    uint8_t *img = (uint8_t *)X_G(0x2A000u);                /* image page 26 (X_IMG address 0x2A000) */
    CHECK(img - g_xram >= L.image_copy_off && *(uint32_t *)img == 8);
    CHECK((uint8_t *)X_G(image_base) - g_xram == L.image_copy_off);   /* every image page reads the copy */
    /* the scene writes through the render table: live is untouched until leave */
    *(uint32_t *)X_G(0x61000u + 8) = 0x12345678u; *(uint32_t *)img = 9;
    CHECK(*(uint32_t *)(g_xram + 0x61008u) != 0x12345678u);
    *(uint32_t *)(g_xram + 0x61000u + 200) = 0xCAFEu;                 /* a helper writing live directly */
    /* a live-table remap during the scene is not mirrored (dropped), one outside is */
    xk_mem_map_alias(0x00600000u, 0x62000u);
    CHECK((uint8_t *)X_G(0x00600000u) == g_xram + 0x00600000u);      /* render table keeps the identity mapping mid-scene */
    xv_render_view_leave(&scope);
    CHECK(xv_host_page_table == live);
    CHECK(*(uint32_t *)(g_xram + 0x61008u) == 0x12345678u);           /* copied back */
    CHECK(*(uint32_t *)(g_xram + 0x61000u + 200) == 0xCAFEu);        /* merge, not clobber */
    CHECK(g_xram[0x61000u + 100] == 0x33);
    CHECK(*(uint32_t *)(g_img_base + 0x2A000u) == 9);
    xk_mem_map_alias(0x00700000u, 0x62000u);
    { unsigned s2 = 0; xv_render_view_enter(&s2, NULL); CHECK((uint8_t *)X_G(0x00700000u) == g_xram + 0x62000u); xv_render_view_leave(&s2); }
    /* nesting: inner scopes do nothing */
    { unsigned a = 0, b = 0; xv_render_view_enter(&a, NULL); xv_render_view_enter(&b, NULL); CHECK(a == 1 && b == 0 && xv_host_page_table != live);
      xv_render_view_leave(&b); CHECK(xv_host_page_table != live); xv_render_view_leave(&a); CHECK(xv_host_page_table == live); }
    xv_render_view_report(3);
    /* slot overflow: 20 distinct pages change with 16 slots */
    xv_render_view_present(2);
    for (unsigned i = 0; i < 20; i++) g_xram[0x100000u + i * 4096u]++;
    xv_render_view_present(3);
    xv_render_view_report(1);
    printf("PASS render_view: aliases %u, arena %u KiB\n", n, arena >> 10);
    return 0;
}
