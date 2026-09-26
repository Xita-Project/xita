#include "kernel/xk_scene_index_run.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { SIZE = 65536, MAX_YIELDS = 1024 };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
unsigned batches, yields, mutation;
static uint32_t bound_address, initial_bound;
static xctx trace[MAX_YIELDS], expected_trace[MAX_YIELDS];
void original(xctx *);
void candidate(xctx *);
static void put(uint32_t address, uint32_t value) { memcpy(X_G(address), &value, 4); }

void xv_preempt(xctx *c)
{
    assert(yields < MAX_YIELDS);
    trace[yields++] = *c;
    c->preempt = 7;
    if (yields == 1) {
        if (mutation == 1) put(bound_address, c->r[3] + 4u);
        if (mutation == 2) {
            memcpy(g_xram + 0x9000, g_xram + 0x2000, 4096);
            g_xpt[2] = 0x9000;
        }
        if (mutation == 3) put(c->r[3] + 4u, c->r[1]);
        if (mutation == 4) {
            memcpy(g_xram + 0xa000, g_xram + 0x7000, 4096);
            g_xpt[7] = 0xa000;
            put(bound_address, initial_bound);
        }
        if (mutation == 5) ++c->r[1];
        if (mutation == 6) { c->r[4] = 0xb000; put(0xb014, initial_bound); }
    }
}

static void reset_pages(void)
{
    for (unsigned i = 0; i < SIZE / 4096; ++i) g_xpt[i] = i * 4096u;
}

int main(void)
{
    g_xram = malloc(SIZE); g_img_base = g_xram;
    g_xpt = calloc(1u << 20, 4);
    uint8_t *initial = malloc(SIZE), *expected = malloc(SIZE);
    assert(g_xram && g_xpt && initial && expected);
    unsigned admitted = 0, handoffs = 0;
    for (unsigned k = 0; k < 2400; ++k) {
        reset_pages(); memset(g_xram, 0, SIZE);
        xctx c; memset(&c, 0xa5, sizeof c);
        c.r[3] = (uint32_t[]){0x1800, 0x1ff0, 0x1ffc, 0x2000}[k % 4];
        c.r[4] = (uint32_t[]){0x7000, 0x6ff0, 0x6fea}[(k / 4) % 3];
        c.r[1] = (uint32_t[]){0, 100, 0x80000000u, 0x7fffffffu}[(k / 12) % 4];
        c.preempt = (int[]){1, 2, 3, 7, 64, 10000}[(k / 48) % 6];
        unsigned length = (unsigned[]){0, 1, 2, 3, 8, 31, 128, 511}[(k / 7) % 8];
        bound_address = c.r[4] + 0x14u;
        initial_bound = c.r[3] + length * 4u;
        put(bound_address, initial_bound);
        for (unsigned i = 0; i < length + 2; ++i) {
            uint32_t value = c.r[1] - 1u;
            if ((k % 5 == 0 && i == length / 2) || (k % 7 == 0 && i == 2)) value = c.r[1];
            put(c.r[3] + 4u * i, value);
        }
        mutation = (k / 17) % 7;
        memcpy(initial, g_xram, SIZE); xctx before = c;
        yields = 0; original(&c); xctx want = c;
        unsigned ny = yields; memcpy(expected_trace, trace, ny * sizeof c);
        memcpy(expected, g_xram, SIZE); uint32_t expected_pages[16];
        memcpy(expected_pages, g_xpt, sizeof expected_pages);
        reset_pages(); memcpy(g_xram, initial, SIZE); c = before;
        yields = batches = 0; candidate(&c);
        if (memcmp(&want, &c, sizeof c) || memcmp(expected, g_xram, SIZE) || ny != yields ||
            memcmp(expected_trace, trace, ny * sizeof c) || memcmp(expected_pages, g_xpt, sizeof expected_pages)) {
            fprintf(stderr, "FAIL case %u yields %u/%u batches %u\n", k, ny, yields, batches);
            return 1;
        }
        admitted += batches; handoffs += yields;
    }
    /* Declines must preserve all context bytes, including stale lazy-flag cells. */
    for (unsigned k = 0; k < 8; ++k) {
        reset_pages(); memset(g_xram, 0, SIZE);
        xctx c; memset(&c, 0xa5, sizeof c);
        c.r[3] = 0x1800; c.r[4] = 0x7000; c.r[1] = 100; c.preempt = 64;
        put(0x7014, 0x1900);
        if (k == 0) c.r[3] = 0xfffffffc;
        if (k == 1) c.r[4] = 0xfffffff0;
        if (k == 2) g_xpt[1] = SIZE;
        if (k == 3) g_xpt[7] = SIZE;
        if (k == 4) c.r[3]++;
        if (k == 5) put(0x7014, 0x1901);
        if (k == 6) { c.r[3] = 0xfd000000; put(0x7014, 0xfd000100); }
        if (k == 7) c.r[4] = 0xfd000000;
        xctx before = c;
        assert(!xv_scene_index_run(&c, g_xram, g_xpt, SIZE));
        assert(!memcmp(&c, &before, sizeof c));
    }
    reset_pages();
    xctx *alias = (xctx *)(g_xram + 0xb000);
    memset(alias, 0, sizeof *alias); alias->r[3] = 0x1800; alias->r[4] = 0x7000; alias->preempt = 64;
    xctx before = *alias;
    assert(!xv_scene_index_run(alias, g_xram, g_xpt, SIZE));
    assert(!memcmp(alias, &before, sizeof *alias));
    assert(admitted && handoffs);
    printf("PASS 2400 context/memory/mapping comparisons; %u batches; %u matching yield states; 9 declines\n", admitted, handoffs);
    if (getenv("INDEX_BENCH")) {
        unsigned lengths[] = {1, 2, 3, 8, 32, 128, 512};
        mutation = 0;
        for (unsigned l = 0; l < sizeof lengths / sizeof *lengths; ++l) {
            reset_pages(); memset(g_xram, 0, SIZE);
            xctx start; memset(&start, 0, sizeof start);
            start.r[3] = 0x1800; start.r[4] = 0x7000; start.r[1] = 100;
            start.preempt = 1000000; put(0x7014, 0x1800 + lengths[l] * 4u);
            for (unsigned mode = 0; mode < 2; ++mode) {
                struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
                for (unsigned i = 0; i < 100000; ++i) {
                    xctx c = start; if (mode) candidate(&c); else original(&c);
                }
                clock_gettime(CLOCK_MONOTONIC, &b);
                printf("cost length %u mode %u %.1f ns/call\n", lengths[l], mode,
                       ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/100000.0);
            }
        }
    }
    free(initial); free(expected); free(g_xram); free(g_xpt);
}
