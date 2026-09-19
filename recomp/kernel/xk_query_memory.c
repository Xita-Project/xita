#include "xk_query_memory.h"
#include <stddef.h>
#include <string.h>

static int host_span(uintptr_t p, size_t n)
{ return n == 0 || (p != 0 && n - 1 <= UINTPTR_MAX - p); }

static int overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn)
{ return an && bn && (a <= b ? b - a < an : a - b < bn); }

static int view_ok(const XvQueryMemory *s, const XvQueryMemoryView *v)
{
    size_t pn;
    if (!v || !v->arena || !v->usable_size || (v->usable_size & 63u) ||
        v->page_count > (1u << 20) || (!v->pages && v->page_count) ||
        ((uintptr_t)v->pages % _Alignof(uint32_t))) return 0;
    pn = (size_t)v->page_count * sizeof(uint32_t);
    return host_span((uintptr_t)v->arena, v->usable_size) &&
        host_span((uintptr_t)v->pages, pn) &&
        !overlap((uintptr_t)s, sizeof(*s), (uintptr_t)v->arena, v->usable_size) &&
        !overlap((uintptr_t)s, sizeof(*s), (uintptr_t)v->pages, pn) &&
        !overlap((uintptr_t)v->arena, v->usable_size, (uintptr_t)v->pages, pn);
}

static int same_view(const XvQueryMemory *s, const XvQueryMemoryView *v)
{
    return v && s->view.arena == v->arena && s->view.pages == v->pages &&
        s->view.usable_size == v->usable_size && s->view.page_count == v->page_count;
}

void xv_query_memory_invalidate(XvQueryMemory *s, unsigned reason)
{
    s->status = XV_QM_INVALID;
    s->reason |= reason ? reason : XV_QM_UNKNOWN;
}

static int fail(XvQueryMemory *s, unsigned reason)
{ xv_query_memory_invalidate(s, reason); return 0; }

int xv_query_memory_begin(XvQueryMemory *s, const XvQueryMemoryView *v)
{
    /* Permit begin(s, &s->view) on restart. */
    XvQueryMemoryView copy;
    if (!v || !view_ok(s, v)) {
        s->status = XV_QM_INVALID; s->reason = XV_QM_BOUNDS;
        s->block_count = s->mapping_count = 0;
        return 0;
    }
    copy = *v;
    s->view = copy;
    /* Entries are initialized lazily; do not clear ~20KiB on every cold query. */
    memset(s->hash, 0, sizeof(s->hash));
    s->reason = s->block_count = s->mapping_count = 0;
    s->status = XV_QM_RECORDING;
    return 1;
}

static int recording(XvQueryMemory *s)
{ return s->status == XV_QM_RECORDING ? 1 : fail(s, XV_QM_BAD_STATE); }

int xv_query_memory_mapping(XvQueryMemory *s, uint32_t guest, uint32_t physical)
{
    unsigned i;
    if (!recording(s)) return 0;
    if (guest >= s->view.page_count || (physical & 4095u) ||
        s->view.usable_size < 4096 || physical > s->view.usable_size - 4096)
        return fail(s, XV_QM_BOUNDS);
    if (s->view.pages[guest] != physical) return fail(s, XV_QM_REMAP);
    for (i = 0; i < s->mapping_count; ++i) {
        if (s->mappings[i].guest_page == guest)
            return s->mappings[i].physical_page == physical ? 1 : fail(s, XV_QM_REMAP);
    }
    if (s->mapping_count == XV_QUERY_MEMORY_MAPPINGS) return fail(s, XV_QM_OVERFLOW);
    s->mappings[s->mapping_count++] = (XvQueryMemoryMapping){guest, physical};
    return 1;
}

static XvQueryMemoryBlock *block(XvQueryMemory *s, uint32_t offset)
{
    /* Use high product bits: low bits would collide at every16KiB stride. */
    unsigned h = ((offset >> 6) * UINT32_C(2654435761)) >> 24;
    unsigned n;
    for (n = 0; n < XV_QUERY_MEMORY_HASH_SIZE; ++n) {
        unsigned i = s->hash[h];
        if (!i) {
            XvQueryMemoryBlock *b;
            if (s->block_count == XV_QUERY_MEMORY_BLOCKS) break;
            i = s->block_count++;
            b = &s->blocks[i];
            b->offset = offset; b->reads = b->writes = 0;
            memcpy(b->initial, s->view.arena + offset, XV_QUERY_MEMORY_BLOCK_SIZE);
            s->hash[h] = (uint16_t)(i + 1u);
            return b;
        }
        if (s->blocks[i - 1u].offset == offset) return &s->blocks[i - 1u];
        h = (h + 1u) & (XV_QUERY_MEMORY_HASH_SIZE - 1u);
    }
    fail(s, XV_QM_OVERFLOW);
    return NULL;
}

static int access_memory(XvQueryMemory *s, uint32_t offset, uint32_t length, int write)
{
    if (!recording(s)) return 0;
    if (offset > s->view.usable_size || length > s->view.usable_size - offset)
        return fail(s, XV_QM_BOUNDS);
    while (length) {
        unsigned first = offset & 63u, n = 64u - first;
        uint64_t mask;
        XvQueryMemoryBlock *b = block(s, offset & ~63u);
        if (!b) return 0;
        if (n > length) n = length;
        mask = n == 64 ? UINT64_MAX : ((UINT64_C(1) << n) - 1u) << first;
        if (write) b->writes |= mask;
        else b->reads |= mask & ~b->writes;
        offset += n; length -= n;
    }
    return 1;
}

int xv_query_memory_read(XvQueryMemory *s, uint32_t offset, uint32_t length)
{ return access_memory(s, offset, length, 0); }
int xv_query_memory_write(XvQueryMemory *s, uint32_t offset, uint32_t length)
{ return access_memory(s, offset, length, 1); }

static int mappings_match(const XvQueryMemory *s, const XvQueryMemoryView *v)
{
    unsigned i;
    for (i = 0; i < s->mapping_count; ++i)
        if (v->pages[s->mappings[i].guest_page] != s->mappings[i].physical_page) return 0;
    return 1;
}

int xv_query_memory_finish(XvQueryMemory *s, const XvQueryMemoryView *v)
{
    unsigned i;
    if (!recording(s)) return 0;
    if (!same_view(s, v)) return fail(s, XV_QM_ROOTS);
    if (!mappings_match(s, v)) return fail(s, XV_QM_REMAP);
    for (i = 0; i < s->block_count; ++i)
        memcpy(s->blocks[i].final, s->view.arena + s->blocks[i].offset, XV_QUERY_MEMORY_BLOCK_SIZE);
    s->status = XV_QM_FINISHED;
    return 1;
}

/* Byte order is expressed as bytes, so a native word load works on either
 * endian host. Only requested bytes participate in the exact comparison. */
_Alignas(uint32_t) static const unsigned char byte_masks[16][4] = {
    {0,0,0,0}, {255,0,0,0}, {0,255,0,0}, {255,255,0,0},
    {0,0,255,0}, {255,0,255,0}, {0,255,255,0}, {255,255,255,0},
    {0,0,0,255}, {255,0,0,255}, {0,255,0,255}, {255,255,0,255},
    {0,0,255,255}, {255,0,255,255}, {0,255,255,255}, {255,255,255,255}
};

static int bytes_match(const unsigned char *live, const unsigned char *saved,
                       uint32_t bits)
{
    /* A32-bit mask covers32 bytes. Avoid one variable64-bit shift per byte
     * on ARM32; trailing zero groups need no loads or comparisons. */
    while (bits) {
        unsigned nibble = bits & 15u;
        if (nibble) {
            uint32_t a, b, mask;
            memcpy(&a, live, sizeof(a));
            memcpy(&b, saved, sizeof(b));
            memcpy(&mask, byte_masks[nibble], sizeof(mask));
            if ((a ^ b) & mask) return 0;
        }
        bits >>= 4; live += 4; saved += 4;
    }
    return 1;
}

static void patch_bytes(unsigned char *live, const unsigned char *saved,
                         uint32_t bits)
{
    while (bits) {
        unsigned nibble = bits & 15u;
        if (nibble == 15u) memcpy(live, saved, 4);
        else if (nibble) {
            /* Partial words store only the recorded bytes. */
            unsigned j;
            for (j = 0; j < 4; ++j)
                if (nibble & (1u << j)) live[j] = saved[j];
        }
        bits >>= 4; live += 4; saved += 4;
    }
}

int xv_query_memory_validate(const XvQueryMemory *s, const XvQueryMemoryView *v)
{
    unsigned i;
    if (s->status != XV_QM_FINISHED || !same_view(s, v) || !mappings_match(s, v)) return 0;
    for (i = 0; i < s->block_count; ++i) {
        const XvQueryMemoryBlock *b = &s->blocks[i];
        if (!bytes_match(v->arena + b->offset, b->initial, (uint32_t)b->reads) ||
            !bytes_match(v->arena + b->offset + 32, b->initial + 32,
                         (uint32_t)(b->reads >> 32))) return 0;
    }
    return 1;
}

int xv_query_memory_replay(const XvQueryMemory *s, const XvQueryMemoryView *v)
{
    unsigned i;
    if (!xv_query_memory_validate(s, v)) return 0;
    for (i = 0; i < s->block_count; ++i) {
        const XvQueryMemoryBlock *b = &s->blocks[i];
        patch_bytes(s->view.arena + b->offset, b->final, (uint32_t)b->writes);
        patch_bytes(s->view.arena + b->offset + 32, b->final + 32,
                    (uint32_t)(b->writes >> 32));
    }
    return 1;
}
