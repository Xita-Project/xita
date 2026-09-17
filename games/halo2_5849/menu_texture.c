#include "menu_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Write-epoch tracking (Vita build only; absent in the host tests): see menu_texture_acquire. */
extern uint32_t xv_page_epoch[] __attribute__((weak));   /* static table in checked builds; absent in the host tests */
extern uint32_t xv_page_count __attribute__((weak)), xv_write_epoch __attribute__((weak));
extern uint8_t *g_xram __attribute__((weak));
extern void *h2_map_physical_read(uint32_t address, uint32_t bytes) __attribute__((weak));
extern void h2_texture_tracking_fault(uint32_t address, uint32_t bytes) __attribute__((weak));
extern uint32_t xk_mem_image_arena_offset(void) __attribute__((weak));
extern void xv_logf(const char *, ...) __attribute__((weak));

static uint32_t argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{ return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }

/* Expand an RGB565 colour to (r,g,b) 8-bit. */
static void rgb565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    *g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
    *b = (uint8_t)((c & 0x1F) * 255 / 31);
}
static uint8_t x5(unsigned v) { return (uint8_t)(v * 255 / 31); }
static uint8_t x4(unsigned v) { return (uint8_t)(v * 17); }

/* Decode DXT1 colour block -> 4x4, alpha 255 (or 0 for the punch-through case). */
static void dxt1_colors(const uint8_t *block, uint32_t out[16], int keep_alpha)
{
    uint16_t c0 = (uint16_t)(block[0] | block[1] << 8);
    uint16_t c1 = (uint16_t)(block[2] | block[3] << 8);
    uint8_t r[4], g[4], b[4], a[4];
    rgb565(c0, &r[0], &g[0], &b[0]); rgb565(c1, &r[1], &g[1], &b[1]);
    a[0] = a[1] = a[2] = a[3] = 255;
    if (c0 > c1) {
        r[2] = (uint8_t)((2 * r[0] + r[1]) / 3); g[2] = (uint8_t)((2 * g[0] + g[1]) / 3); b[2] = (uint8_t)((2 * b[0] + b[1]) / 3);
        r[3] = (uint8_t)((r[0] + 2 * r[1]) / 3); g[3] = (uint8_t)((g[0] + 2 * g[1]) / 3); b[3] = (uint8_t)((b[0] + 2 * b[1]) / 3);
    } else {
        r[2] = (uint8_t)((r[0] + r[1]) / 2); g[2] = (uint8_t)((g[0] + g[1]) / 2); b[2] = (uint8_t)((b[0] + b[1]) / 2);
        r[3] = g[3] = b[3] = 0; if (keep_alpha) a[3] = 0;
    }
    uint32_t bits = (uint32_t)(block[4] | block[5] << 8 | block[6] << 16 | (uint32_t)block[7] << 24);
    for (unsigned i = 0; i < 16; ++i) {
        unsigned idx = (bits >> (2 * i)) & 3;
        out[i] = argb(a[idx], r[idx], g[idx], b[idx]);
    }
}

void menu_dxt1_block(const uint8_t *block, uint32_t *out, uint32_t stride)
{
    uint32_t tile[16];
    dxt1_colors(block, tile, 1);
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x)
        out[y * stride + x] = tile[y * 4 + x];
}

void menu_dxt3_block(const uint8_t *block, uint32_t *out, uint32_t stride)
{
    uint32_t tile[16];
    dxt1_colors(block + 8, tile, 0);         /* colour is the 8-byte DXT1 half */
    for (unsigned i = 0; i < 16; ++i) {
        unsigned nib = (block[i / 2] >> ((i & 1) * 4)) & 0xF; /* 4-bit explicit alpha */
        uint8_t a = (uint8_t)(nib * 255 / 15);
        unsigned x = i % 4, y = i / 4;
        uint32_t c = tile[i];
        out[y * stride + x] = (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
    }
}

void menu_dxt5_block(const uint8_t *block, uint32_t *out, uint32_t stride)
{
    uint32_t tile[16];
    dxt1_colors(block + 8, tile, 0);         /* colour is the 8-byte DXT1 half */
    uint8_t a[8];                             /* 8-byte interpolated alpha block */
    a[0] = block[0]; a[1] = block[1];
    if (a[0] > a[1]) {
        for (unsigned i = 1; i <= 6; ++i) a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
    } else {
        for (unsigned i = 1; i <= 4; ++i) a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
        a[6] = 0; a[7] = 255;
    }
    uint64_t bits = 0;
    for (unsigned i = 0; i < 6; ++i) bits |= (uint64_t)block[2 + i] << (8 * i);
    for (unsigned i = 0; i < 16; ++i) {
        unsigned idx = (unsigned)((bits >> (3 * i)) & 7), x = i % 4, y = i / 4;
        out[y * stride + x] = (tile[i] & 0x00FFFFFFu) | ((uint32_t)a[idx] << 24);
    }
}

/* NV2A Morton index for (x,y) within a swizzled log2w x log2h image. */
static uint32_t morton(uint32_t x, uint32_t y, unsigned logw, unsigned logh)
{
    unsigned ml = logw < logh ? logw : logh;
    uint32_t o = 0;
    for (unsigned b = 0; b < ml; ++b)
        o |= ((x >> b) & 1u) << (2 * b) | ((y >> b) & 1u) << (2 * b + 1);
    if (logw > logh) o |= (x >> ml) << (ml + logh);
    else if (logh > logw) o |= (y >> ml) << (ml + logw);
    return o;
}

/* Colour formats (NV097_SET_TEXTURE_FORMAT_COLOR_*): texel layout + storage. */
enum { PF_Y8 = 1, PF_AY8, PF_A8, PF_A8Y8, PF_A1R5G5B5, PF_X1R5G5B5, PF_A4R4G4B4, PF_R5G6B5,
       PF_A8R8G8B8, PF_X8R8G8B8, PF_A8B8G8R8, PF_B8G8R8A8, PF_R8G8B8A8, PF_P8 };
typedef struct { uint8_t kind, bytes, linear, dxt; } fmt_desc;

static int describe_format(unsigned code, fmt_desc *d)
{
#define SZ(k, n)  *d = (fmt_desc){k, n, 0, 0}; return 1
#define LIN(k, n) *d = (fmt_desc){k, n, 1, 0}; return 1
    switch (code) {
    case 0x00: SZ(PF_Y8, 1);        case 0x01: SZ(PF_AY8, 1);
    case 0x02: SZ(PF_A1R5G5B5, 2);  case 0x03: SZ(PF_X1R5G5B5, 2);
    case 0x04: SZ(PF_A4R4G4B4, 2);  case 0x05: SZ(PF_R5G6B5, 2);
    case 0x06: SZ(PF_A8R8G8B8, 4);  case 0x07: SZ(PF_X8R8G8B8, 4);
    case 0x0B: SZ(PF_P8, 1);                                      /* I8_A8R8G8B8: palette index */
    case 0x0C: *d = (fmt_desc){0, 8, 0, 1}; return 1;    /* DXT1 */
    case 0x0E: *d = (fmt_desc){0, 16, 0, 3}; return 1;   /* DXT23 */
    case 0x0F: *d = (fmt_desc){0, 16, 0, 5}; return 1;   /* DXT45 */
    case 0x10: LIN(PF_A1R5G5B5, 2); case 0x11: LIN(PF_R5G6B5, 2);
    case 0x12: LIN(PF_A8R8G8B8, 4); case 0x13: LIN(PF_Y8, 1);
    case 0x19: SZ(PF_A8, 1);        case 0x1A: SZ(PF_A8Y8, 2);
    case 0x1B: LIN(PF_AY8, 1);      case 0x1C: LIN(PF_X1R5G5B5, 2);
    case 0x1D: LIN(PF_A4R4G4B4, 2); case 0x1E: LIN(PF_X8R8G8B8, 4);
    case 0x1F: LIN(PF_A8, 1);       case 0x20: LIN(PF_A8Y8, 2);
    case 0x3A: SZ(PF_A8B8G8R8, 4);  case 0x3B: SZ(PF_B8G8R8A8, 4);
    case 0x3C: SZ(PF_R8G8B8A8, 4);
    case 0x3F: LIN(PF_A8B8G8R8, 4); case 0x40: LIN(PF_B8G8R8A8, 4);
    case 0x41: LIN(PF_R8G8B8A8, 4);
    default: return 0;
    }
#undef SZ
#undef LIN
}

/* Convert one stored texel (little-endian bytes) to 0xAARRGGBB. */
static uint32_t convert(unsigned kind, const uint8_t *p)
{
    uint16_t v = (uint16_t)(p[0] | p[1] << 8);
    uint8_t r, g, b;
    switch (kind) {
    case PF_Y8:       return argb(255, p[0], p[0], p[0]);
    case PF_AY8:      return argb(p[0], p[0], p[0], p[0]);
    case PF_A8:       return argb(p[0], 0, 0, 0);
    case PF_A8Y8:     return argb(p[1], p[0], p[0], p[0]);
    case PF_A1R5G5B5: return argb((v & 0x8000) ? 255 : 0, x5((v >> 10) & 31), x5((v >> 5) & 31), x5(v & 31));
    case PF_X1R5G5B5: return argb(255, x5((v >> 10) & 31), x5((v >> 5) & 31), x5(v & 31));
    case PF_A4R4G4B4: return argb(x4(v >> 12), x4((v >> 8) & 15), x4((v >> 4) & 15), x4(v & 15));
    case PF_R5G6B5:   rgb565(v, &r, &g, &b); return argb(255, r, g, b);
    case PF_A8R8G8B8: return argb(p[3], p[2], p[1], p[0]);
    case PF_X8R8G8B8: return argb(255, p[2], p[1], p[0]);
    case PF_A8B8G8R8: return argb(p[3], p[0], p[1], p[2]);
    case PF_B8G8R8A8: return argb(p[0], p[1], p[2], p[3]);
    case PF_R8G8B8A8: return argb(p[0], p[3], p[2], p[1]);
    default:          return 0;
    }
}

enum { O_OK, O_UNSUPPORTED, O_TOOLARGE, O_NOMAP, O_COUNT };
typedef struct { uint32_t code, n[O_COUNT]; } stat_entry;
static stat_entry g_stats[48];
static unsigned g_nstats;

static void note(uint32_t code, unsigned outcome)
{
    for (unsigned i = 0; i < g_nstats; ++i)
        if (g_stats[i].code == code) { ++g_stats[i].n[outcome]; return; }
    if (g_nstats < sizeof g_stats / sizeof g_stats[0]) {
        g_stats[g_nstats].code = code; ++g_stats[g_nstats].n[outcome]; ++g_nstats;
    }
}

size_t menu_texture_stats(char *buf, size_t cap)
{
    size_t used = 0;
    if (!buf || !cap) return 0;
    buf[0] = 0;
    for (unsigned i = 0; i < g_nstats && used + 1 < cap; ++i) {
        int n = snprintf(buf + used, cap - used, "%s%02X:%u/%u/%u/%u", i ? " " : "",
                         (unsigned)g_stats[i].code, (unsigned)g_stats[i].n[O_OK],
                         (unsigned)g_stats[i].n[O_UNSUPPORTED], (unsigned)g_stats[i].n[O_TOOLARGE],
                         (unsigned)g_stats[i].n[O_NOMAP]);
        if (n < 0) break;
        used += (size_t)n;
    }
    return used < cap ? used : cap - 1;
}

int menu_texture_describe(const h2_command_state *s, unsigned unit,
                          uint32_t *code, uint32_t *width, uint32_t *height)
{
    if (!s || unit >= 4) return 0;
    unsigned base = 0x1B00 + unit * 64;
    uint32_t fmt = s->setup[(base + 4) / 4], ctrl = s->setup[(base + 0xC) / 4];
    fmt_desc d;
    *code = (fmt >> 8) & 0xFF;
    if (describe_format(*code, &d) && d.linear) {
        uint32_t rect = s->setup[(base + 0x1C) / 4];
        *width = rect >> 16; *height = rect & 0xFFFF;
    } else {
        *width = 1u << ((fmt >> 20) & 15); *height = 1u << ((fmt >> 24) & 15);
    }
    return !!(ctrl & 0x40000000u);
}

/* A located texture unit: validated registers, mapped source span and palette. */
typedef struct {
    fmt_desc d;
    unsigned code, lw, lh;
    uint32_t w, h, pitch, src_bytes, blocks_x, blocks_y;
    const uint8_t *src;
    uint32_t palette[256], pal_mask;
    uint32_t key[6];                                     /* offset, fmt, ctrl1, rect, palette, dma */
} located;

static int locate(const h2_command_state *s, const h2_kelvin_clear *c, unsigned unit, uint32_t cap, located *L)
{
    if (!s || !c || !c->read_instance || !c->map_physical || unit >= 4) return 0;
    unsigned base = 0x1B00 + unit * 64;
    uint32_t fmt = s->setup[(base + 4) / 4], ctrl = s->setup[(base + 0xC) / 4];
    if (!(ctrl & 0x40000000u)) return 0;                 /* unit disabled */
    memset(L, 0, sizeof *L);
    L->code = (fmt >> 8) & 0xFF; L->lw = (fmt >> 20) & 15; L->lh = (fmt >> 24) & 15;
    unsigned selector = fmt & 3, code = L->code;
    fmt_desc d;
    if (!describe_format(code, &d)) { note(code, O_UNSUPPORTED); return 0; }
    L->d = d;
    if ((selector != 1 && selector != 2) || !(s->dma_valid & (1u << selector))) { note(code, O_NOMAP); return 0; }

    if (d.linear) {
        uint32_t rect = s->setup[(base + 0x1C) / 4];
        L->w = rect >> 16; L->h = rect & 0xFFFF; L->pitch = s->setup[(base + 0x10) / 4] >> 16;
        if (!L->w || !L->h || L->w > 4096 || L->h > 4096 || (uint64_t)L->w * d.bytes > L->pitch) { note(code, O_NOMAP); return 0; }
        L->src_bytes = (uint32_t)((uint64_t)(L->h - 1) * L->pitch + (uint64_t)L->w * d.bytes);
    } else {
        if (L->lw > 12 || L->lh > 12) { note(code, O_TOOLARGE); return 0; }
        L->w = 1u << L->lw; L->h = 1u << L->lh;
        if (d.dxt) { L->blocks_x = (L->w + 3) / 4; L->blocks_y = (L->h + 3) / 4; L->src_bytes = L->blocks_x * L->blocks_y * d.bytes; }
        else L->src_bytes = L->w * L->h * d.bytes;
    }
    if ((uint64_t)L->w * L->h > cap) { note(code, O_TOOLARGE); return 0; }

    h2_dma_object dma;
    uint32_t phys;
    if (!h2_dma_load(c->read_instance, c->opaque, s->dma[selector], &dma) ||
        !h2_dma_resolve(&dma, s->setup[base / 4], L->src_bytes, 0, c->physical_bytes, &phys)) { note(code, O_NOMAP); return 0; }
    L->src = h2_map_physical_read ? h2_map_physical_read(phys, L->src_bytes) : c->map_physical(c->opaque, phys, L->src_bytes);
    if (!L->src) { note(code, O_NOMAP); return 0; }

    /* P8 texels index the unit's palette (SET_TEXTURE_PALETTE: DMA bit 0 A/B,
     * length bits 2-3 = 256>>n entries, offset bits 6-31), A8R8G8B8 entries. */
    if (d.kind == PF_P8) {
        uint32_t pal = s->setup[(base + 0x20) / 4];
        unsigned pal_sel = (pal & 1) ? 2 : 1, pal_len = 256u >> ((pal >> 2) & 3);
        h2_dma_object pdma;
        uint32_t pphys;
        const uint8_t *psrc;
        if (!(s->dma_valid & (1u << pal_sel)) ||
            !h2_dma_load(c->read_instance, c->opaque, s->dma[pal_sel], &pdma) ||
            !h2_dma_resolve(&pdma, pal & ~0x3Fu, pal_len * 4, 0, c->physical_bytes, &pphys) ||
            !(psrc = c->map_physical(c->opaque, pphys, pal_len * 4))) { note(code, O_NOMAP); return 0; }
        memcpy(L->palette, psrc, pal_len * 4);
        L->pal_mask = pal_len - 1;
    }
    L->key[0] = s->setup[base / 4]; L->key[1] = fmt; L->key[2] = s->setup[(base + 0x10) / 4];
    L->key[3] = s->setup[(base + 0x1C) / 4]; L->key[4] = s->setup[(base + 0x20) / 4]; L->key[5] = s->dma[selector];
    return 1;
}

static void decode(const located *L, uint32_t *rgba)
{
    const fmt_desc d = L->d;
    const uint8_t *src = L->src;
    uint32_t w = L->w, h = L->h, pitch = L->pitch;
    unsigned lw = L->lw, lh = L->lh;
    if (d.dxt) {
        unsigned lbw = lw > 2 ? lw - 2 : 0, lbh = lh > 2 ? lh - 2 : 0;
        for (uint32_t by = 0; by < L->blocks_y; ++by)
            for (uint32_t bx = 0; bx < L->blocks_x; ++bx) {
                uint32_t bi = morton(bx, by, lbw, lbh), tile[16];
                const uint8_t *blk = src + (size_t)bi * d.bytes;
                if (d.dxt == 1) menu_dxt1_block(blk, tile, 4);
                else if (d.dxt == 3) menu_dxt3_block(blk, tile, 4);
                else menu_dxt5_block(blk, tile, 4);
                for (unsigned ty = 0; ty < 4 && by * 4 + ty < h; ++ty)  /* clip 4x4 tiles to small images */
                    for (unsigned tx = 0; tx < 4 && bx * 4 + tx < w; ++tx)
                        rgba[(size_t)(by * 4 + ty) * w + bx * 4 + tx] = tile[ty * 4 + tx];
            }
    } else if (d.linear) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                rgba[(size_t)y * w + x] = convert(d.kind, src + (size_t)y * pitch + (size_t)x * d.bytes);
    } else if (d.kind == PF_P8) {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                rgba[(size_t)y * w + x] = L->palette[src[morton(x, y, lw, lh)] & L->pal_mask];
    } else {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                rgba[(size_t)y * w + x] = convert(d.kind, src + (size_t)morton(x, y, lw, lh) * d.bytes);
    }
}

int menu_texture_load(const h2_command_state *s, const h2_kelvin_clear *c,
                      unsigned unit, uint32_t *rgba, uint32_t cap, uint32_t *ow, uint32_t *oh,
                      int *out_linear)
{
    located L;
    if (!locate(s, c, unit, cap, &L)) return 0;
    decode(&L, rgba);
    note(L.code, O_OK);
    *ow = L.w; *oh = L.h;
    if (out_linear) *out_linear = L.d.linear;   /* linear images are addressed in texels, not [0,1] */
    return 1;
}

/* Decoded-texture cache. Menu draws rebind the same material and screen images
 * thousands of times per second; decoding a 512x512 DXT5 or a 640x480 screen
 * image per draw dominated frame time. Entries are keyed by the unit registers
 * AND a hash of every source byte (plus palette), so a CPU-updated font cache or
 * a re-rendered screen image can never be served stale. LRU within a byte budget;
 * entries used by the current draw are never evicted. */
static uint64_t content_hash(const uint8_t *p, size_t n, uint64_t h)
{
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w; memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x9E3779B97F4A7C15ull; h ^= h >> 29;
    }
    for (; i < n; ++i) { h = (h ^ p[i]) * 0x100000001B3ull; }
    return h;
}

typedef struct { uint32_t key[6]; uint64_t hash; uint32_t *texels; uint32_t w, h, bytes; int linear; uint64_t used;
                 int tracked; uint32_t page_lo, page_hi, verified; unsigned clean_hits;
                 uint8_t *snapshot; uint32_t snapshot_bytes; } tex_entry;   /* snapshot: diagnostic copy of the source */
enum { TEX_CACHE_ENTRIES = 64, TEX_CACHE_BUDGET = 40u << 20 };
static tex_entry g_entries[TEX_CACHE_ENTRIES];
static size_t g_cache_bytes;
static uint64_t g_hits, g_misses, g_hash_bytes, g_hash_skipped;

const uint32_t *menu_texture_acquire(const h2_command_state *s, const h2_kelvin_clear *c, unsigned unit,
                                     uint64_t serial, uint32_t cap, uint32_t *ow, uint32_t *oh, int *out_linear,
                                     uint64_t *out_hash)
{
    located L;
    if (!locate(s, c, unit, cap, &L)) return NULL;
    /* The source is normally re-hashed on every draw (level textures reach ~130 KB
     * each). With the arena's per-page write epochs available, an entry with the same
     * key whose source pages carry no stamp at or after its last full read is proven
     * unchanged and reuses its hash; every 64th such use still reads the source in
     * full, and a mismatch there is a strict stop: a writer the stamps did not see. */
    int tracked = xv_page_epoch != NULL && &g_xram && g_xram && L.d.kind != PF_P8 &&
                  (uintptr_t)L.src >= (uintptr_t)g_xram;
    uint32_t page_lo = 0, page_hi = 0;
    if (tracked) {
        uintptr_t off = (uintptr_t)L.src - (uintptr_t)g_xram;
        page_lo = (uint32_t)(off >> 12); page_hi = (uint32_t)((off + L.src_bytes - 1) >> 12);
        if (page_hi >= xv_page_count) tracked = 0;
        /* The XBE image's own data pages are written by unstamped image-relative stores
         * (X_IMG*): sources there keep the full re-hash. Level textures live in RAM. */
        if (xk_mem_image_arena_offset && page_hi >= xk_mem_image_arena_offset() >> 12) tracked = 0;
    }
    tex_entry *keyed = NULL, *victim = NULL;
    for (unsigned i = 0; i < TEX_CACHE_ENTRIES; ++i) {
        tex_entry *t = &g_entries[i];
        if (t->texels && !memcmp(t->key, L.key, sizeof L.key)) { keyed = t; continue; }
        if (!t->texels) { if (!victim || victim->texels) victim = t; }
        else if (t->used != serial && (!victim || (victim->texels && t->used < victim->used))) victim = t;
    }
    int clean = 0;
    if (keyed && tracked && keyed->tracked && keyed->page_lo == page_lo && keyed->page_hi == page_hi) {
        clean = 1;
        for (uint32_t p = page_lo; p <= page_hi; ++p) if (xv_page_epoch[p] >= keyed->verified) { clean = 0; break; }
    }
    uint64_t hash;
    if (clean && (++keyed->clean_hits & 63)) { hash = keyed->hash; ++g_hash_skipped; }
    else {
        hash = content_hash(L.src, L.src_bytes, 0x243F6A8885A308D3ull ^ L.src_bytes);
        g_hash_bytes += L.src_bytes;
        if (L.d.kind == PF_P8) hash = content_hash((const uint8_t *)L.palette, (L.pal_mask + 1) * 4, hash);
        if (clean && hash != keyed->hash && h2_texture_tracking_fault) {
            uint32_t emin = 0xFFFFFFFFu, emax = 0;
            for (uint32_t p = page_lo; p <= page_hi; ++p) { if (xv_page_epoch[p] < emin) emin = xv_page_epoch[p]; if (xv_page_epoch[p] > emax) emax = xv_page_epoch[p]; }
            uint32_t first = 0xFFFFFFFFu, last = 0, ndiff = 0;
            if (keyed->snapshot && keyed->snapshot_bytes == L.src_bytes)
                for (uint32_t i = 0; i < L.src_bytes; ++i) if (keyed->snapshot[i] != L.src[i]) { if (first == 0xFFFFFFFFu) first = i; last = i; ++ndiff; }
            if (xv_logf) xv_logf("[h2/texture-fault] key=%08X bytes=%u pages=%u..%u stamps=%u..%u verified=%u now=%u diff=%u first=%u last=%u\n",
                    L.key[0], L.src_bytes, page_lo, page_hi, emin, emax, keyed->verified, xv_write_epoch, ndiff, first, last);
            h2_texture_tracking_fault(L.key[0], L.src_bytes);
        }
    }
    tex_entry *e = (keyed && keyed->hash == hash) ? keyed : NULL;
    if (e) { ++g_hits; }
    else {
        ++g_misses;
        if (keyed) victim = keyed;                          /* same key, changed content: replace in place */
        uint32_t bytes = L.w * L.h * 4;
        /* Evict least-recently-used entries (never this draw's) until the budget fits. */
        while (g_cache_bytes + bytes > TEX_CACHE_BUDGET) {
            tex_entry *old = NULL;
            for (unsigned i = 0; i < TEX_CACHE_ENTRIES; ++i)
                if (g_entries[i].texels && g_entries[i].used != serial && (!old || g_entries[i].used < old->used)) old = &g_entries[i];
            if (!old) break;
            g_cache_bytes -= old->bytes; free(old->texels); old->texels = NULL;
            if (!victim || victim->texels) victim = old;
        }
        if (!victim) return NULL;
        if (victim->texels) { g_cache_bytes -= victim->bytes; free(victim->texels); victim->texels = NULL; }
        uint32_t *px = malloc(bytes);
        if (!px) return NULL;
        decode(&L, px);
        note(L.code, O_OK);
        memcpy(victim->key, L.key, sizeof L.key);
        victim->hash = hash; victim->texels = px; victim->w = L.w; victim->h = L.h; victim->bytes = bytes;
        victim->linear = L.d.linear; g_cache_bytes += bytes;
        victim->clean_hits = 0;
        e = victim;
    }
    if (!clean || !(e->clean_hits & 63)) {     /* hashed this time: the stamps from here on are what matter */
        e->tracked = tracked; e->page_lo = page_lo; e->page_hi = page_hi; e->verified = tracked ? xv_write_epoch : 0;
        static int keep_snapshots = -1;         /* XV_TEXTURE_SNAPSHOT=1: retain sources (<=256 KB) so a mismatch is described */
        if (keep_snapshots < 0) { const char *k = getenv("XV_TEXTURE_SNAPSHOT"); keep_snapshots = k ? atoi(k) : 0; }
        if (keep_snapshots && tracked && L.src_bytes <= 262144) {
            if (e->snapshot_bytes != L.src_bytes) { free(e->snapshot); e->snapshot = malloc(L.src_bytes); e->snapshot_bytes = e->snapshot ? L.src_bytes : 0; }
            if (e->snapshot) memcpy(e->snapshot, L.src, L.src_bytes);
        }
    }
    e->used = serial;
    *ow = e->w; *oh = e->h;
    if (out_linear) *out_linear = e->linear;
    if (out_hash) *out_hash = e->hash ^ ((uint64_t)e->key[1] << 32) ^ e->key[0];   /* content + format/offset */
    return e->texels;
}

void menu_texture_cache_stats(uint64_t *hits, uint64_t *misses, size_t *bytes)
{ *hits = g_hits; *misses = g_misses; *bytes = g_cache_bytes; }
uint64_t menu_texture_hashed_bytes(void) { return g_hash_bytes; }
uint64_t menu_texture_hash_skipped(void) { return g_hash_skipped; }
