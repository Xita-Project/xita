#include "xk_query_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SIZE = (XV_QUERY_MEMORY_BLOCKS + 64) * 64, PAGES = 256 };
static unsigned char arena[SIZE], initial[SIZE], warm[SIZE], expected[SIZE], saved[SIZE];
static uint32_t pages[PAGES], other_pages[PAGES];
static XvQueryMemory record, saved_record;
static XvQueryMemoryView view;

static void setup(void)
{
    unsigned i;
    for (i = 0; i < SIZE; ++i) arena[i] = (unsigned char)(i * 37u + (i >> 8) + 11u);
    for (i = 0; i < PAGES; ++i) pages[i] = (i % (SIZE / 4096)) * 4096u;
    pages[0] = pages[1] = 4096; /* Two guest pages alias one physical page. */
    view = (XvQueryMemoryView){arena, SIZE, pages, PAGES};
    assert(xv_query_memory_begin(&record, &view));
}

static void get(XvQueryMemory *s, unsigned offset, void *dst, unsigned n)
{
    if (s) assert(xv_query_memory_read(s, offset, n));
    memcpy(dst, arena + offset, n);
}

static void put(XvQueryMemory *s, unsigned offset, const void *src, unsigned n)
{
    if (s) assert(xv_query_memory_write(s, offset, n));
    memcpy(arena + offset, src, n);
}

/* An independent byte program executes on both the cold and changed warm
 * arenas. It deliberately reads its own writes and performs a same-value cold
 * store, so a final-difference patch or all-reads dependency set is incorrect. */
static void program(XvQueryMemory *s)
{
    unsigned char a[20], b[8], sum = 0;
    unsigned i;
    get(s, 61, a, 8);
    for (i = 0; i < 5; ++i) b[i] = a[i] ^ 0x39;
    put(s, 62, b, 5);
    get(s, 62, a, 20);
    for (i = 0; i < 20; ++i) sum += a[i];
    for (i = 0; i < 8; ++i) b[i] = sum + i;
    put(s, 255, b, 8);
    b[0] = (unsigned char)(110u * 37u + 11u); /* Same as cold arena[110]. */
    put(s, 110, b, 1);
    memset(b, 0x73, 4); put(s, 200, b, 4);
    get(s, 201, b, 2); put(s, 206, b, 2);
    if (s) assert(xv_query_memory_mapping(s, 0, 4096));
    get(s, 4100, a, 4);
    for (i = 0; i < 4; ++i) b[i] = a[i] ^ 0xe1;
    if (s) assert(xv_query_memory_mapping(s, 1, 4096));
    put(s, 4101, b, 4);
    get(s, 4102, b, 2); put(s, 4200, b, 2);
}

static int dependent(unsigned i)
{ return (i >= 61 && i < 82) || (i >= 4100 && i < 4104); }

static void rejects_without_writes(const XvQueryMemoryView *v)
{
    memcpy(saved, arena, SIZE); saved_record = record;
    assert(!xv_query_memory_validate(&record, v));
    assert(!xv_query_memory_replay(&record, v));
    assert(!memcmp(saved, arena, SIZE));
    assert(!memcmp(&saved_record, &record, sizeof(record)));
}

static void transaction(void)
{
    unsigned i;
    setup(); memcpy(initial, arena, SIZE);
    program(&record);
    assert(xv_query_memory_finish(&record, &view));
    assert(record.status == XV_QM_FINISHED && record.mapping_count == 2);
    memcpy(warm, initial, SIZE);
    for (i = 0; i < SIZE; ++i) if (!dependent(i)) warm[i] ^= 0xa5;
    memcpy(arena, warm, SIZE); program(NULL); memcpy(expected, arena, SIZE);
    memcpy(arena, warm, SIZE);
    assert(xv_query_memory_validate(&record, &view));
    assert(xv_query_memory_replay(&record, &view));
    assert(!memcmp(arena, expected, SIZE));
    assert(arena[110] == initial[110]); /* Same-value cold store is replayed. */
    /* One changed dependency in any block rejects before even the first patch. */
    for (i = 0; i < SIZE; ++i) if (dependent(i)) {
        memcpy(arena, warm, SIZE); arena[i] ^= 1;
        rejects_without_writes(&view);
    }
    memcpy(arena, warm, SIZE);
    pages[200] = 0; /* Unobserved mappings are not dependencies. */
    assert(xv_query_memory_replay(&record, &view));
    memcpy(arena, warm, SIZE);
    pages[1] = 8192; rejects_without_writes(&view);
    pages[1] = 4096;
    {
        XvQueryMemoryView bad = view;
        memcpy(other_pages, pages, sizeof(pages));
        bad.pages = other_pages; rejects_without_writes(&bad);
        bad = view; bad.arena = initial; rejects_without_writes(&bad);
        bad = view; bad.usable_size -= 64; rejects_without_writes(&bad);
        bad = view; --bad.page_count; rejects_without_writes(&bad);
        rejects_without_writes(NULL);
    }
}

static void masks_and_collisions(void)
{
    unsigned char b[128];
    setup(); memset(b, 0x51, sizeof(b));
    assert(xv_query_memory_write(&record, 0, 64)); memcpy(arena, b, 64);
    assert(xv_query_memory_read(&record, 0, 64));
    assert(record.blocks[0].writes == UINT64_MAX && record.blocks[0].reads == 0);
    assert(xv_query_memory_read(&record, 127, 2));
    assert(xv_query_memory_write(&record, 126, 4)); memcpy(arena + 126, b, 4);
    /* Physical blocks4 and148 both hash to120; retain both exact keys. */
    assert(xv_query_memory_write(&record, 4 * 64, 1)); arena[4 * 64] = 0x63;
    assert(xv_query_memory_write(&record, 148 * 64, 1)); arena[148 * 64] = 0x27;
    assert(xv_query_memory_finish(&record, &view));
    /* Restore dependencies overwritten by the cross-block cold store. */
    arena[127] = (unsigned char)(127 * 37 + 11);
    arena[128] = (unsigned char)(128 * 37 + 11);
    arena[4 * 64] = arena[148 * 64] = 0;
    assert(xv_query_memory_replay(&record, &view));
    assert(arena[4 * 64] == 0x63 && arena[148 * 64] == 0x27);
    assert(arena[126] == 0x51 && arena[129] == 0x51);
    assert(xv_query_memory_begin(&record, &record.view));
    assert(record.block_count == 0 && record.mapping_count == 0 && !record.reason);
    assert(xv_query_memory_read(&record, SIZE, 0));
    assert(xv_query_memory_write(&record, 17, 0));
    assert(!record.block_count);
    assert(xv_query_memory_finish(&record, &view));
    memcpy(saved, arena, SIZE);
    assert(xv_query_memory_replay(&record, &view));
    assert(!memcmp(saved, arena, SIZE));
}

static void capacity(void)
{
    unsigned i;
    setup();
    for (i = 0; i < XV_QUERY_MEMORY_BLOCKS; ++i) {
        assert(xv_query_memory_write(&record, i * 64, 1));
        arena[i * 64] = (unsigned char)i;
    }
    assert(record.block_count == XV_QUERY_MEMORY_BLOCKS);
    assert(xv_query_memory_read(&record, 0, 1)); /* Existing at capacity. */
    assert(xv_query_memory_finish(&record, &view));
    memset(arena, 0xff, SIZE); assert(xv_query_memory_replay(&record, &view));
    for (i = 0; i < XV_QUERY_MEMORY_BLOCKS; ++i) assert(arena[i * 64] == (unsigned char)i);
    /* Dependencies and writes in the upper part of the enlarged table must
     * remain exact; a full-table rejection must publish no earlier writes. */
    setup(); memcpy(initial, arena, SIZE);
    for (i = 0; i < XV_QUERY_MEMORY_BLOCKS; ++i) {
        assert(xv_query_memory_read(&record, i * 64, 1));
        assert(xv_query_memory_write(&record, i * 64 + 1, 1));
        arena[i * 64 + 1] ^= 0x81;
    }
    assert(xv_query_memory_finish(&record, &view));
    memcpy(expected, arena, SIZE); memcpy(arena, initial, SIZE);
    arena[(XV_QUERY_MEMORY_BLOCKS - 1) * 64] ^= 1;
    rejects_without_writes(&view);
    arena[(XV_QUERY_MEMORY_BLOCKS - 1) * 64] ^= 1;
    assert(xv_query_memory_replay(&record, &view));
    assert(!memcmp(arena, expected, SIZE));
    setup();
    assert(!xv_query_memory_write(&record, 0, XV_QUERY_MEMORY_BLOCKS * 64 + 1));
    assert(record.status == XV_QM_INVALID && (record.reason & XV_QM_OVERFLOW));
    assert(!xv_query_memory_finish(&record, &view)); rejects_without_writes(&view);
    setup();
    for (i = 0; i < XV_QUERY_MEMORY_MAPPINGS; ++i) {
        pages[i] = 0;
        assert(xv_query_memory_mapping(&record, i, 0));
    }
    assert(xv_query_memory_mapping(&record, 0, 0));
    assert(xv_query_memory_finish(&record, &view));
    assert(xv_query_memory_validate(&record, &view));
    assert(xv_query_memory_begin(&record, &view));
    for (i = 0; i < XV_QUERY_MEMORY_MAPPINGS; ++i)
        assert(xv_query_memory_mapping(&record, i, 0));
    pages[i] = 0;
    assert(!xv_query_memory_mapping(&record, i, 0));
    assert(record.reason & XV_QM_OVERFLOW); rejects_without_writes(&view);
}

static void word_masks(void)
{
    unsigned alignment, position, mask, j;
    const unsigned offsets[] = {0, 28, 32, 60};
    for (alignment = 0; alignment < 2; ++alignment)
    for (position = 0; position < 4; ++position)
    for (mask = 0; mask < 16; ++mask) {
        unsigned off = offsets[position], base = alignment;
        setup();
        view.arena += alignment;
        view.usable_size -= alignment ? 64u : 0u;
        assert(xv_query_memory_begin(&record, &view));
        memcpy(initial, arena, SIZE);
        for (j = 0; j < 4; ++j) if (mask & (1u << j)) {
            unsigned char value;
            assert(xv_query_memory_read(&record, off + j, 1));
            value = view.arena[off + j] ^ 0x3c;
            assert(xv_query_memory_write(&record, off + j, 1));
            view.arena[off + j] = value;
        }
        assert(xv_query_memory_finish(&record, &view));
        memcpy(warm, initial, SIZE);
        for (j = 0; j < 4; ++j)
            if (!(mask & (1u << j))) warm[base + off + j] ^= 0xa5;
        memcpy(expected, warm, SIZE);
        for (j = 0; j < 4; ++j)
            if (mask & (1u << j)) expected[base + off + j] ^= 0x3c;
        memcpy(arena, warm, SIZE);
        assert(xv_query_memory_replay(&record, &view));
        assert(!memcmp(arena, expected, SIZE));
        for (j = 0; j < 4; ++j) if (mask & (1u << j)) {
            memcpy(arena, warm, SIZE); arena[base + off + j] ^= 1;
            rejects_without_writes(&view);
        }
    }
}

static void invalidation(void)
{
    XvQueryMemoryView bad;
    setup();
    assert(xv_query_memory_mapping(&record, 0, 4096)); pages[0] = 8192;
    assert(!xv_query_memory_finish(&record, &view));
    assert(record.reason & XV_QM_REMAP); rejects_without_writes(&view);
    setup();
    assert(xv_query_memory_mapping(&record, 0, 4096)); pages[0] = 8192;
    assert(!xv_query_memory_mapping(&record, 0, 8192));
    assert(record.reason & XV_QM_REMAP);
    setup(); assert(!xv_query_memory_mapping(&record, 0, 0));
    assert(record.reason & XV_QM_REMAP);
    setup(); assert(!xv_query_memory_mapping(&record, PAGES, 0));
    setup(); assert(!xv_query_memory_mapping(&record, 0, 4097));
    setup(); assert(!xv_query_memory_mapping(&record, 0, SIZE));
    setup(); assert(!xv_query_memory_read(&record, SIZE - 1, 2));
    setup(); assert(!xv_query_memory_write(&record, UINT32_MAX, 2));
    setup(); assert(!xv_query_memory_read(&record, 1, UINT32_MAX));
    setup(); assert(!xv_query_memory_read(&record, SIZE + 1, 0));
    setup(); bad = view; --bad.usable_size;
    assert(!xv_query_memory_begin(&record, &bad));
    bad = view; bad.arena = NULL; assert(!xv_query_memory_begin(&record, &bad));
    bad = view; bad.usable_size = 0; assert(!xv_query_memory_begin(&record, &bad));
    bad = view; bad.pages = NULL; assert(!xv_query_memory_begin(&record, &bad));
    bad = view; bad.page_count = (1u << 20) + 1; assert(!xv_query_memory_begin(&record, &bad));
    bad = view; bad.pages = (const uint32_t *)((const unsigned char *)pages + 1);
    assert(!xv_query_memory_begin(&record, &bad));
    bad = view; bad.pages = (const uint32_t *)(const void *)arena;
    assert(!xv_query_memory_begin(&record, &bad));
    assert(!xv_query_memory_begin(&record, NULL));
    rejects_without_writes(&view);
    setup(); bad = view; bad.usable_size = 4096 + 64;
    assert(xv_query_memory_begin(&record, &bad));
    assert(!xv_query_memory_mapping(&record, 0, 4096)); /* Truncated physical page. */
    setup(); bad = view; bad.pages = NULL; bad.page_count = 0;
    assert(xv_query_memory_begin(&record, &bad));
    assert(xv_query_memory_read(&record, 3, 3));
    assert(xv_query_memory_finish(&record, &bad));
    assert(xv_query_memory_replay(&record, &bad));
    setup(); bad = view; bad.pages = other_pages;
    assert(!xv_query_memory_finish(&record, &bad));
    assert(record.reason & XV_QM_ROOTS);
    setup(); xv_query_memory_invalidate(&record, XV_QM_CALLBACK);
    assert(!xv_query_memory_read(&record, 0, 1));
    assert(record.reason & XV_QM_CALLBACK); rejects_without_writes(&view);
    setup(); xv_query_memory_invalidate(&record, 0);
    assert(record.reason & XV_QM_UNKNOWN); rejects_without_writes(&view);
    setup(); assert(xv_query_memory_finish(&record, &view));
    assert(!xv_query_memory_write(&record, 0, 1));
    assert(record.reason & XV_QM_BAD_STATE); rejects_without_writes(&view);
    setup(); program(&record); assert(xv_query_memory_finish(&record, &view));
}

int main(void)
{
    transaction(); masks_and_collisions(); word_masks(); capacity(); invalidation();
    printf("PASS exact query memory: byte program/replay, aliases, atomic rejection, masks, collisions, capacities, invalidation/restart (%zu-byte state)\n", sizeof(record));
    return 0;
}
