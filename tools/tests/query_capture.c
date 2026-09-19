#include "xk_query_capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { USABLE = 12288, SIZE = USABLE + 4096, PAGES = 8 };
static unsigned char arena[SIZE], other_arena[SIZE], before[SIZE], expected[SIZE];
static uint32_t pages[PAGES + 1], other_pages[PAGES + 1];
static XvQueryMemory cap;
static XvQueryMemoryView view;

static void setup(void)
{
    unsigned i;
    for (i = 0; i < SIZE; ++i) arena[i] = (unsigned char)(i * 17u + (i >> 8));
    for (i = 0; i <= PAGES; ++i) pages[i] = (i % 3) * 4096;
    pages[0] = 4096; pages[1] = 0;
    memcpy(before, arena, SIZE);
    memcpy(other_pages, pages, sizeof(pages));
    view = (XvQueryMemoryView){arena, USABLE, pages, PAGES};
    assert(xv_query_memory_begin(&cap, &view));
}

static void invalid(unsigned reason)
{
    assert(cap.status == XV_QM_INVALID && (cap.reason & reason));
    memcpy(expected, arena, SIZE);
    assert(!xv_query_memory_replay(&cap, &view));
    assert(!memcmp(expected, arena, SIZE));
}

static void contiguous(void)
{
    const unsigned char value[4] = {71, 72, 73, 74};
    setup();
    /* Starting guest page0 maps physical4096; the next guest page maps0.
     * Typed access crosses into physical8192, never consults guest page1. */
    xv_query_capture_touch(&cap, arena, pages, 4094, arena + 8190, 4, 0);
    xv_query_capture_touch(&cap, arena, pages, 4095, arena + 8191, 4, 1);
    assert(!memcmp(before, arena, SIZE)); /* Notifications never execute stores. */
    memcpy(arena + 8191, value, 4);
    assert(cap.mapping_count == 1 && cap.mappings[0].guest_page == 0);
    assert(cap.block_count == 2);
    assert(xv_query_memory_finish(&cap, &view));
    memcpy(arena, before, SIZE); arena[8194] ^= 0xa5; arena[8195] ^= 0x39;
    pages[1] = 4096; /* Unconsulted PTE must not cause a false dependency. */
    memcpy(expected, arena, SIZE); memcpy(expected + 8191, value, 4);
    assert(xv_query_memory_replay(&cap, &view));
    assert(!memcmp(expected, arena, SIZE));
    memcpy(arena, before, SIZE); pages[0] = 0;
    assert(!xv_query_memory_replay(&cap, &view));
    assert(!memcmp(before, arena, SIZE));
    pages[0] = 4096; arena[8193] ^= 1;
    memcpy(expected, arena, SIZE);
    assert(!xv_query_memory_replay(&cap, &view));
    assert(!memcmp(expected, arena, SIZE));
}

static void mappings(void)
{
    unsigned char value;
    setup(); pages[2] = pages[0];
    xv_query_capture_touch(&cap, arena, pages, 7, arena + 4103, 1, 0);
    value = arena[4103] ^ 0x3c;
    xv_query_capture_touch(&cap, arena, pages, 8199, arena + 4103, 1, 1);
    arena[4103] = value;
    assert(cap.mapping_count == 2 && cap.block_count == 1);
    assert(xv_query_memory_finish(&cap, &view));
    memcpy(arena, before, SIZE);
    assert(xv_query_memory_replay(&cap, &view));
    assert(arena[4103] == value);
    setup();
    assert(xv_query_capture_pte(&cap, arena, pages, 3) == pages[3]);
    assert(!cap.block_count && cap.mapping_count == 1);
    pages[3] = 4096;
    assert(!xv_query_memory_finish(&cap, &view)); invalid(XV_QM_REMAP);
    setup();
    assert(xv_query_capture_pte(&cap, arena, pages, 3) == pages[3]);
    assert(xv_query_memory_finish(&cap, &view));
    pages[3] = 4096;
    assert(!xv_query_memory_replay(&cap, &view));
    assert(!memcmp(before, arena, SIZE));
    setup();
    /* Out of the recording view but inside the original allocated array:
     * preserve the original PTE result, invalidate only the recording. */
    pages[PAGES] = 0x76543210;
    assert(xv_query_capture_pte(&cap, arena, pages, PAGES) == 0x76543210);
    invalid(XV_QM_BOUNDS);
    setup(); pages[2] = USABLE;
    assert(xv_query_capture_pte(&cap, arena, pages, 2) == USABLE);
    invalid(XV_QM_BOUNDS);
    setup(); pages[2] = 4097;
    assert(xv_query_capture_pte(&cap, arena, pages, 2) == 4097);
    invalid(XV_QM_BOUNDS);
    setup();
    xv_query_capture_touch(&cap, arena, pages, 0, arena + 4096, 1, 0);
    pages[0] = 0;
    xv_query_capture_touch(&cap, arena, pages, 0, arena, 1, 0);
    invalid(XV_QM_REMAP);
}

static void physical(void)
{
    const unsigned char bytes[4] = {4, 3, 2, 1};
    setup();
    xv_query_capture_physical(&cap, arena + 63, 3, 0);
    xv_query_capture_physical(&cap, arena + 65, 4, 1);
    memcpy(arena + 65, bytes, 4);
    assert(cap.mapping_count == 0 && cap.block_count == 2);
    assert(xv_query_memory_finish(&cap, &view));
    memcpy(arena, before, SIZE); arena[62] ^= 1; arena[66] ^= 1;
    memcpy(expected, arena, SIZE); memcpy(expected + 65, bytes, 4);
    assert(xv_query_memory_replay(&cap, &view));
    assert(!memcmp(expected, arena, SIZE));
    setup();
    xv_query_capture_physical(&cap, arena + USABLE - 1, 1, 0);
    assert(xv_query_memory_finish(&cap, &view));
    assert(xv_query_memory_validate(&cap, &view));
    setup(); xv_query_capture_physical(&cap, arena + USABLE, 1, 0); invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_physical(&cap, arena + USABLE - 1, 2, 1); invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_physical(&cap, NULL, 1, 0); invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_physical(&cap, arena, UINT32_MAX, 0); invalid(XV_QM_BOUNDS);
    setup();
    xv_query_capture_physical(&cap, arena, XV_QUERY_MEMORY_BLOCKS * 64 + 1, 0);
    invalid(XV_QM_OVERFLOW);
}

static void roots_and_bounds(void)
{
    setup();
    xv_query_capture_touch(&cap, other_arena, pages, 0, other_arena + 4096, 1, 0);
    invalid(XV_QM_ROOTS);
    setup();
    xv_query_capture_touch(&cap, arena, other_pages, 0, arena + 4096, 1, 0);
    invalid(XV_QM_ROOTS);
    setup(); other_pages[0] = 8192;
    assert(xv_query_capture_pte(&cap, arena, other_pages, 0) == 8192);
    invalid(XV_QM_ROOTS);
    setup();
    assert(xv_query_capture_pte(&cap, other_arena, pages, 0) == 4096);
    invalid(XV_QM_ROOTS);
    setup(); xv_query_capture_touch(&cap, arena, pages, 0, arena + 4097, 1, 0);
    invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_touch(&cap, arena, pages, PAGES * 4096, arena, 1, 0);
    invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_touch(&cap, arena, pages, UINT32_MAX, arena, 2, 0);
    invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_touch(&cap, arena, pages, 0, arena + 4096, UINT32_MAX, 1);
    invalid(XV_QM_BOUNDS);
    setup(); xv_query_capture_touch(&cap, arena, pages, 4095, arena + 8191, UINT32_MAX, 0);
    invalid(XV_QM_BOUNDS); assert(!cap.mapping_count); /* Guest extent overflows before PTE capture. */
    setup(); pages[0] = USABLE;
    xv_query_capture_touch(&cap, arena, pages, 0, arena + USABLE, 1, 0);
    invalid(XV_QM_BOUNDS);
    setup(); pages[0] = 8192;
    xv_query_capture_touch(&cap, arena, pages, 4095, arena + USABLE - 1, 2, 0);
    invalid(XV_QM_BOUNDS);
}

static void ignored_and_abandoned(void)
{
    unsigned reason;
    setup();
    xv_query_capture_touch(NULL, NULL, NULL, UINT32_MAX, NULL, 8, 1);
    xv_query_capture_physical(NULL, NULL, UINT32_MAX, 1);
    xv_query_capture_abandon(NULL, XV_QM_CALLBACK);
    assert(xv_query_capture_pte(NULL, arena, pages, 0) == 4096);
    assert(!cap.block_count && !cap.mapping_count);
    xv_query_capture_touch(&cap, NULL, NULL, UINT32_MAX, NULL, 0, 1);
    xv_query_capture_physical(&cap, NULL, 0, 1);
    assert(cap.status == XV_QM_RECORDING && !cap.block_count && !cap.mapping_count);
    xv_query_capture_abandon(&cap, XV_QM_CALLBACK); reason = cap.reason;
    xv_query_capture_touch(&cap, NULL, NULL, UINT32_MAX, NULL, 8, 1);
    xv_query_capture_physical(&cap, NULL, 1, 0);
    xv_query_capture_abandon(&cap, XV_QM_UNKNOWN);
    pages[0] = 8192;
    assert(xv_query_capture_pte(&cap, NULL, pages, 0) == 8192);
    assert(cap.reason == reason); invalid(XV_QM_CALLBACK);
    setup();
    xv_query_capture_touch(&cap, arena, pages, 5, arena + 4102, 1, 1);
    invalid(XV_QM_BOUNDS);
    arena[4102] = 0x7e; /* Original caller's write still executes after decline. */
    assert(arena[4102] == 0x7e);
    assert(xv_query_memory_begin(&cap, &view));
    xv_query_capture_touch(&cap, arena, pages, 6, arena + 4102, 1, 0);
    assert(xv_query_memory_finish(&cap, &view));
    xv_query_capture_touch(&cap, NULL, NULL, 0, NULL, 1, 0);
    xv_query_capture_physical(&cap, NULL, 1, 0);
    assert(cap.status == XV_QM_FINISHED && xv_query_memory_validate(&cap, &view));
}

int main(void)
{
    contiguous(); mappings(); physical(); roots_and_bounds(); ignored_and_abandoned();
    puts("PASS explicit query capture: roots, original PTE values, contiguous page crossing, physical bounds, aliases, invalidation and NULL context");
    return 0;
}
