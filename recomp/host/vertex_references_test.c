/* Real uploader with mocked GPU storage; every fetched record and every older
 * GPU snapshot must remain correct across mutations, aliases and slot reuse. */
#define XV_VERTEX_UPLOAD_BYTES (1024u * 1024u)
#define main baseline_upload_main
#include "vertex_upload_test.c"
#undef main
#include "../../runtime/xv_index_copy.h"

static uint32_t random_state = 0x528631;
static unsigned random_value(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void copy_checks(void)
{
    unsigned char src[2049], dst[2049];
    for (unsigned align = 0; align < 16; align++) for (unsigned n = 0; n <= 1000; n += 37) {
        xv_vertex_refs refs; unsigned maximum = 0; unsigned char covered[8192] = {0};
        for (unsigned i = 0; i < n; i++) {
            uint16_t index = i == 0 ? 65535 : random_value();
            memcpy(src + align + i * 2, &index, 2);
            if ((unsigned)index + 1 > maximum) maximum = (unsigned)index + 1;
            covered[index >> 3] = 1;
        }
        memset(dst, 0xa5, sizeof dst);
        assert(xv_index_copy_reference_bounds(dst + 15 - align, src + align, n, &refs) == maximum);
        assert(!memcmp(dst + 15 - align, src + align, n * 2));
        for (unsigned i = 0; i < sizeof dst; i++)
            if (i < 15 - align || i >= 15 - align + n * 2) assert(dst[i] == 0xa5);
        unsigned groups = 0;
        for (unsigned i = 0; i < 8192; i++) {
            assert(!!(refs.bits[i >> 5] & (1u << (i & 31))) == covered[i]);
            groups += covered[i];
        }
        assert(groups == refs.groups);
    }
    uint16_t *all = malloc(65536 * 2), *retained = malloc(65536 * 2);
    assert(all && retained);
    for (unsigned i = 0; i < 65536; i++) all[i] = i;
    xv_vertex_refs dense;
    assert(xv_index_copy_reference_bounds(retained, all, 65536, &dense) == 65536);
    assert(dense.groups == 8192 && !memcmp(all, retained, 65536 * 2));
    assert(!xv_vertex_refs_sparse(&dense, 65536 * 16, 16));
    free(all); free(retained);
    xv_vertex_refs refs = {0};
    assert(!xv_vertex_refs_sparse(&refs, 4096, 4));
    xv_vertex_refs_add(&refs, 65535);
    assert(xv_vertex_refs_sparse(&refs, 65536 * 3, 3));
    assert(!xv_vertex_refs_sparse(&refs, 65536 * 3 - 1, 3));
    assert(!xv_vertex_refs_sparse(&refs, UINT32_MAX, UINT32_MAX));
    assert(!xv_vertex_refs_sparse(&refs, 0, 0));
}

static void mutation_checks(void)
{
    unsigned char source[4096]; memset(source, 3, sizeof source);
    xv_vertex_refs refs; xv_vertex_refs_clear(&refs);
    xv_vertex_refs_add(&refs, 0); xv_vertex_refs_add(&refs, 1023);
    const unsigned char *p = xv_vertex_upload_referenced(0, source, sizeof source, 4, &refs);
    assert(p && !memcmp(p, source, sizeof source));
    source[500 * 4] = 7; /* Entirely unreferenced group: earlier bytes remain usable. */
    unsigned before = copies;
    assert(xv_vertex_upload_referenced(0, source, sizeof source, 4, &refs) == p);
    assert(copies == before && p[500 * 4] == 3);
    xv_vertex_refs_add(&refs, 500); /* The next draw must observe the update. */
    const unsigned char *q = xv_vertex_upload_referenced(0, source, sizeof source, 4, &refs);
    assert(q && q != p && q[500 * 4] == 7 && p[500 * 4] == 3);
    source[0] = 19;
    const unsigned char *r = xv_vertex_upload(0, source, sizeof source);
    assert(r && r != p && r != q && !memcmp(r, source, sizeof source));
    assert(q[0] == 3 && p[0] == 3);
    xv_vertex_upload_shutdown(); assert(!live); next_id = 1;
}

static void slot_checks(void)
{
    unsigned char source[3][16385], expected[3][8][16384];
    const void *gpu[3][8]; unsigned counts[3] = {0};
    for (unsigned i = 0; i < sizeof source; i++) ((unsigned char *)source)[i] = random_value();
    for (unsigned frame = 0; frame < 500; frame++) {
        unsigned slot = frame % 3; xv_vertex_upload_reset(slot); counts[slot] = 0;
        xv_vertex_upload_override(frame & 1); xv_vertex_copy_override((frame >> 1) & 1);
        xv_vertex_compare_override((frame >> 2) & 1);
        for (unsigned draw = 0; draw < 8; draw++) {
            unsigned which = random_value() % 3, offset = draw & 1;
            unsigned char *data = source[which] + offset;
            for (unsigned i = 0; i < 13; i++) data[random_value() % 16384] ^= random_value() | 1;
            uint16_t indices[49]; indices[0] = 1023;
            for (unsigned i = 1; i < 49; i++) indices[i] = random_value() & 1023;
            xv_vertex_refs refs; uint16_t retained[49];
            assert(xv_index_copy_reference_bounds(retained, indices, 49, &refs) == 1024);
            const unsigned char *p = draw & 2 ? xv_vertex_upload(slot, data, 16384) :
                xv_vertex_upload_referenced(slot, data, 16384, 16, &refs);
            assert(p);
            for (unsigned i = 0; i < 49; i++) assert(!memcmp(p + indices[i] * 16, data + indices[i] * 16, 16));
            gpu[slot][draw] = p; memcpy(expected[slot][draw], p, 16384); counts[slot]++;
            for (unsigned s = 0; s < 3; s++) for (unsigned d = 0; d < counts[s]; d++)
                assert(!memcmp(gpu[s][d], expected[s][d], 16384));
            assert(!memcmp(pools[slot].cpu, pools[slot].gpu, pools[slot].valid_bytes));
        }
    }
    assert(reference_checks && reference_hits && reference_compared < reference_requested);
    xv_vertex_upload_shutdown(); assert(!live);
}

int main(int argc, char **argv)
{
    assert(!xv_vertex_references_enabled());
    xv_vertex_references_override(1); assert(xv_vertex_references_enabled());
    xv_vertex_references_override(0); assert(!xv_vertex_references_enabled());
    xv_vertex_references_override(-1); assert(!xv_vertex_references_enabled());
    int initial_blocks=xv_vertex_blocks_enabled();
    if (argc > 1) assert(initial_blocks == atoi(argv[1]));
    xv_vertex_blocks_override(1);assert(xv_vertex_blocks_enabled());
    xv_vertex_blocks_override(0);assert(!xv_vertex_blocks_enabled());
    xv_vertex_blocks_override(-1);assert(xv_vertex_blocks_enabled()==initial_blocks);
    copy_checks(); mutation_checks(); slot_checks();
    assert((block_checks!=0)==(initial_blocks!=0));
    puts("PASS: exact retained indices/coverage, all 65536 indices, referenced and unreferenced mutations, aliases, 4000 draws across 500 slot generations, immutable GPU snapshots and overrides");
    return 0;
}
