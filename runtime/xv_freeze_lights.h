/* Developer-only, bounded post-freeze evidence. Never repairs guest state.
 * Format v1 is little endian, fixed-width fields, two independently copied
 * samples. Equal samples do NOT establish an atomic simulation snapshot. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define XV_FREEZE_LIGHT_HEADS 512u
#define XV_FREEZE_LIGHT_REFS 2048u
#define XV_FREEZE_LIGHT_MAGIC 0x31464c58u /* XLF1 */
typedef struct {
    uint32_t valid; /* 1 globals, 2 header, 4 heads, 8 records */
    uint32_t globals[4]; /* guest 2fc670..2fc67c */
    uint8_t header[56];
    uint32_t heads[XV_FREEZE_LIGHT_HEADS];
    uint32_t refs[XV_FREEZE_LIGHT_REFS][3];
} xv_freeze_light_sample;
typedef struct {
    uint32_t magic, version, bytes, equal_samples;
    xv_freeze_light_sample samples[2];
} xv_freeze_light_dump;
static inline uint32_t xv_freeze_light_u32(const void *p)
{ uint32_t v; memcpy(&v,p,4); return v; }
static inline uint16_t xv_freeze_light_u16(const void *p)
{ uint16_t v; memcpy(&v,p,2); return v; }
/* Uses the live table, never worker TLS. Rejects trash and shadow mappings.
 * Volatile reads force separate observations; they do not synchronize writers.
 * Caller guarantees a full 1M-entry table and an arena of at least live_limit. */
static inline int xv_freeze_light_read(const uint8_t *arena, const uint32_t *pt,
                                      uint32_t live_limit, uint32_t va,
                                      void *out, size_t count)
{
    if (!arena || !pt || !out || !count || count > UINT32_MAX ||
        (uint64_t)va + count > UINT64_C(0x100000000)) return 0;
    uint8_t *dst = out;
    while (count) {
        uint32_t off = ((const volatile uint32_t *)pt)[va >> 12];
        uint32_t within = va & 4095u;
        size_t n = 4096u - within;
        if (n > count) n = count;
        if ((off & 4095u) || off >= live_limit ||
            (uint64_t)off + within + n > live_limit) return 0;
        const volatile uint8_t *src = arena + off + within;
        for (size_t i=0; i<n; ++i) dst[i] = src[i];
        dst += n; count -= n; va += (uint32_t)n;
    }
    return 1;
}
static inline void xv_freeze_light_sample_read(xv_freeze_light_sample *s,
    const uint8_t *arena, const uint32_t *pt, uint32_t live_limit)
{
    memset(s,0,sizeof *s);
    if (!xv_freeze_light_read(arena,pt,live_limit,0x2fc670u,s->globals,16)) return;
    s->valid |= 1;
    if (!s->globals[1] || !xv_freeze_light_read(arena,pt,live_limit,
        s->globals[1],s->header,sizeof s->header)) return;
    s->valid |= 2;
    unsigned capacity = xv_freeze_light_u16(s->header+0x20);
    unsigned stride = xv_freeze_light_u16(s->header+0x22);
    uint32_t base = xv_freeze_light_u32(s->header+0x34);
    if (!capacity || capacity > XV_FREEZE_LIGHT_REFS || stride != 12 || !base) return;
    if (s->globals[0] && xv_freeze_light_read(arena,pt,live_limit,
        s->globals[0],s->heads,sizeof s->heads)) s->valid |= 4;
    if (xv_freeze_light_read(arena,pt,live_limit,base,s->refs,capacity*12u)) s->valid |= 8;
}
/* Follow the same low-16-bit handle indices and +8 next links as 91ee0.
 * 0=terminates, 1=cycle, 2=out of range, 3=incomplete capture.
 * Only evaluate actual active cluster heads when interpreting the dump: the
 * fixed 512-head capture can include unused entries. Salt validity is separate. */
static inline unsigned xv_freeze_light_chain(const xv_freeze_light_sample *s,
    uint32_t head, unsigned *steps)
{
    uint8_t seen[XV_FREEZE_LIGHT_REFS/8] = {0};
    unsigned capacity = xv_freeze_light_u16(s->header+0x20);
    *steps = 0;
    if ((s->valid & 10u) != 10u || !capacity || capacity > XV_FREEZE_LIGHT_REFS ||
        xv_freeze_light_u16(s->header+0x22) != 12) return 3;
    while (head != UINT32_MAX) {
        unsigned index = head & 0xffffu;
        if (index >= capacity) return 2;
        if (seen[index/8] & (1u << (index%8))) return 1;
        seen[index/8] |= (uint8_t)(1u << (index%8));
        ++*steps;
        head = s->refs[index][2];
    }
    return 0;
}
