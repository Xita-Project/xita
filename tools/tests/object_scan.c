#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { ARENA = 4 << 20, TABLE = 0x18000, RECORDS = 0x21000 };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
unsigned reference_end;
void original_scan_0(xctx *), original_scan_1(xctx *), original_scan_2(xctx *);
unsigned xv_object_scan_empty(xctx *, unsigned);
void xv_object_scan_override(int);
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void __wrap_xv_preempt(xctx *c) { (void)c; abort(); }
static uint32_t random_state = 937235;
static uint32_t random_word(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int enabled = !strcmp(argv[1], "on") || !strcmp(argv[1], "environment");
    if (!strcmp(argv[1], "environment")) setenv("XV_NATIVE_OBJECT_SCAN", "1", 1);
    else unsetenv("XV_NATIVE_OBJECT_SCAN");
    xv_object_scan_override(!strcmp(argv[1], "on") ? 1 :
                            !strcmp(argv[1], "off") ? 0 : -1);
    g_xram = malloc(ARENA);
    g_img_base = g_xram;
    g_xpt = calloc(1u << 20, sizeof *g_xpt);
    uint8_t *saved = malloc(ARENA);
    assert(g_xram && g_xpt && saved);
    for (unsigned i = 0; i < ARENA / 4096; i++) g_xpt[i] = (i ^ 1u) * 4096u;
    unsigned accepted = 0, skipped_total = 0, declined = 0;
    void (*reference[])(xctx *) = {original_scan_0, original_scan_1, original_scan_2};
    for (unsigned k = 0; k < 12000; k++) {
        memset(g_xram, 0xa5, ARENA);
        xctx initial;
        for (unsigned i = 0; i < sizeof initial; i++)
            ((uint8_t *)&initial)[i] = (uint8_t)random_word();
        unsigned pass = k % 3, layout = (k / 3) % 12;
        uint32_t address = RECORDS + (k % 4);
        if (layout == 1) address += 4088;
        if (layout == 2) address += 4095;
        if (layout == 3) address = 0xffffffe0u;
        unsigned index = random_word() % 300;
        int limit = (int)index + (int)(random_word() % 350) + 1;
        int budget = (int)(random_word() % 200) - 2;
        if (layout == 4) index = 0xffffu;
        if (layout == 5) limit = -1;
        if (layout == 6) limit = (int)index + 1;
        if (layout == 7) budget = 1;
        uint32_t table = layout == 8 ? TABLE + 4049u : TABLE;
        initial.r[5] = table;
        initial.r[6] = address;
        initial.r[7] = index;
        initial.preempt = budget;
        initial.f_kind = random_word() % 6;
        initial.f_bits = (unsigned[]){8,16,32}[random_word() % 3];
        initial.f_cf_override &= 1;
        initial.f_of_override &= 1;
        initial.f_cf &= 1;
        initial.f_of &= 1;
        X_IMG32(0x2FC6ACu) = table;
        X_M16(table + 0x2eu) = (uint16_t)limit;
        /* Empty runs end at different live entries; no following page is
         * required to be contiguous. The top virtual page maps to arena 0. */
        unsigned run = layout == 9 ? 0 : layout == 10 ? 350 : random_word() % 200;
        for (unsigned n = 0; n < run; n++) X_M16(address + n * 12u) = 0;
        X_M16(address + run * 12u) = 0x4567;
        memcpy(saved, g_xram, ARENA);
        xctx actual = initial, expected = initial;
        unsigned skipped = xv_object_scan_empty(&actual, pass);
        assert(!memcmp(saved, g_xram, ARENA));
        assert(skipped <= 128 && (!skipped || enabled));
        if (skipped) {
            accepted++;
            skipped_total += skipped;
            assert(skipped < (unsigned)budget);
            assert(index + skipped < (unsigned)limit);
            reference_end = index + skipped;
            reference[pass](&expected);
        } else declined++;
        if (memcmp(&actual, &expected, sizeof actual)) {
            for (unsigned i = 0; i < sizeof actual; i++)
                if (((uint8_t *)&actual)[i] != ((uint8_t *)&expected)[i])
                    fprintf(stderr, "case %u pass %u skipped %u byte %u expected %02x got %02x\n",
                            k, pass, skipped, i, ((uint8_t *)&expected)[i], ((uint8_t *)&actual)[i]);
            abort();
        }
        assert(!memcmp(saved, g_xram, ARENA));
    }
    if (enabled) assert(accepted > 500 && skipped_total > 10000 && declined > 500);
    else assert(accepted == 0);
    printf("PASS object scan %s: 12000 cases, %u accepted, %u entries, %u declined\n",
           argv[1], accepted, skipped_total, declined);
    free(saved); free(g_xpt); free(g_xram);
    return 0;
}
