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
static int g_decode_instead;                     /* locate(): decode a DXT3/5 image the GPU would misdecode (see rewrite_mode3_block) */
static uint32_t g_mode3_decoded;                 /* images decoded for that reason */

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
    /* The endpoint order picks the mode for DXT3/5 colour blocks too: NVIDIA-era hardware
     * (the Xbox's NV2A) decodes them like DXT1, and Halo 2's HUD glyphs rely on it - the
     * four-colour rule the D3D spec describes turned every digit into a white block. */
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

/* Hashed bytes per source offset (diagnostic: which images pay for re-hashing). */
static struct { uint32_t offset, code; unsigned unit; uint64_t bytes; } g_hash_top[24];
static void hash_account(uint32_t offset, uint32_t bytes, unsigned unit, uint32_t code)
{
    unsigned free = 24, least = 0;
    for (unsigned i = 0; i < 24; ++i) {
        if (g_hash_top[i].offset == offset && g_hash_top[i].bytes) { g_hash_top[i].bytes += bytes; return; }
        if (!g_hash_top[i].bytes && free == 24) free = i;
        if (g_hash_top[i].bytes < g_hash_top[least].bytes) least = i;
    }
    unsigned slot = free < 24 ? free : least;
    g_hash_top[slot].offset = offset; g_hash_top[slot].code = code; g_hash_top[slot].unit = unit; g_hash_top[slot].bytes = bytes;
}
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
    { int n = snprintf(buf, cap, "mode3dec=%u", (unsigned)g_mode3_decoded); if (n > 0) used = (size_t)n < cap ? (size_t)n : cap - 1; }
    for (unsigned k = 0; k < 3 && used + 1 < cap; ++k) {          /* the three most re-hashed sources since boot */
        unsigned best = 24;
        for (unsigned i = 0; i < 24; ++i) if (g_hash_top[i].bytes && (best == 24 || g_hash_top[i].bytes > g_hash_top[best].bytes)) best = i;
        if (best == 24) break;
        int n = snprintf(buf + used, cap - used, " hash@%08X(u%u,%02X)=%lluMB", g_hash_top[best].offset, g_hash_top[best].unit, g_hash_top[best].code, (unsigned long long)(g_hash_top[best].bytes >> 20));
        if (n < 0) break;
        used += (size_t)n; g_hash_top[best].bytes = 0;            /* reported: reset so the next line shows the new interval */
    }
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
    unsigned levels;                                     /* mip levels located (>= 1) */
    int cube;                                            /* six faces (GPU path): decoded swizzled, level 0 */
    uint32_t face_stride;                                /* source bytes per cube face (full chain, 128-aligned) */
    uint32_t face_texels;                                /* decoded texels per cube face (GXM face alignment) */
    int native;                                          /* GPU path, DXT: output is the compressed blocks in GXM order */
    uint32_t out_bytes;                                  /* output size (native) */
    uint32_t lvl_out[16];                                /* native: per level output byte offset */
    uint32_t texels;                                     /* decoded texels over all levels (or faces) */
    uint32_t lvl_src[16], lvl_dst[16];                   /* per level: source byte offset, output texel offset */
    const uint8_t *src;
    uint32_t palette[256], pal_mask;
    uint32_t key[6];                                     /* offset, fmt, ctrl1, rect, palette, dma */
} located;

/* First few loads and failures per (format, outcome): the unit's registers, so an
 * unsupported or misdescribed texture can be identified from the log. */
/* XV_UBC_NATIVE=<mask> (default 7): which DXT kinds go to the GPU as compressed blocks
 * (bit 0 DXT1, bit 1 DXT3, bit 2 DXT5); the rest are decoded to texels as before. */
static unsigned ubc_native_mask(void)
{
    static int mask = -1;
    if (mask < 0) { const char *e = getenv("XV_UBC_NATIVE"); mask = e ? atoi(e) & 7 : 7; }
    return (unsigned)mask;
}
/* Mean ARGB of a native (compressed) image's level 0 by decoding its blocks; diagnostics only. */
uint32_t menu_texture_native_mean(const uint32_t *blocks, uint32_t w, uint32_t h, unsigned dxt)
{
    uint32_t nb = ((w + 3) / 4) * ((h + 3) / 4), bytes = dxt == 1 ? 8 : 16;
    uint64_t a = 0, r = 0, g = 0, b = 0, n = 0;
    for (uint32_t i = 0; i < nb; ++i) {
        uint32_t tile[16]; const uint8_t *blk = (const uint8_t *)blocks + (size_t)i * bytes;
        if (dxt == 1) menu_dxt1_block(blk, tile, 4); else if (dxt == 3) menu_dxt3_block(blk, tile, 4); else menu_dxt5_block(blk, tile, 4);
        for (unsigned t = 0; t < 16; ++t, ++n) { a += tile[t] >> 24; r += (tile[t] >> 16) & 255; g += (tile[t] >> 8) & 255; b += tile[t] & 255; }
    }
    return n ? (uint32_t)((a / n) << 24 | (r / n) << 16 | (g / n) << 8 | (b / n)) : 0;
}
static void trace(const h2_command_state *s, unsigned unit, uint32_t code, unsigned outcome, const located *L,
                  uint64_t serial, const uint32_t *px)
{
    static struct { uint32_t code; unsigned outcome, n; } seen[64];
    static unsigned nseen;
    static int limit = -1;                      /* XV_TEX_TRACE=<n>: loads logged per (format, outcome); default 8 ok / 3 failures */
    static const char *const names[O_COUNT] = {"ok", "unsupported", "toolarge", "nomap"};
    if (!xv_logf) return;
    if (limit < 0) { const char *e = getenv("XV_TEX_TRACE"); limit = e ? atoi(e) : 0; }
    unsigned i;
    for (i = 0; i < nseen; ++i) if (seen[i].code == code && seen[i].outcome == outcome) break;
    if (i == nseen) { if (nseen >= sizeof seen / sizeof seen[0]) return; seen[i].code = code; seen[i].outcome = outcome; seen[i].n = 0; ++nseen; }
    if (seen[i].n++ >= (limit > 0 ? (unsigned)limit : outcome == O_OK ? 8u : 3u)) return;
    unsigned base = 0x1B00 + unit * 64;
    uint32_t mean = 0;
    if (px && L && L->native) mean = menu_texture_native_mean(px, L->w, L->h, L->d.dxt);
    else if (px && L && L->texels) {            /* mean ARGB of the decoded level 0 (every 7th texel) */
        uint64_t a = 0, r = 0, g = 0, b = 0, n = 0;
        uint32_t lvl0 = L->cube ? L->w * L->h : (L->levels ? L->lvl_dst[0] + L->w * L->h : L->texels);
        for (uint32_t t = 0; t < lvl0; t += 7, ++n) { uint32_t p = px[t]; a += p >> 24; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; }
        if (n) mean = (uint32_t)((a / n) << 24 | (r / n) << 16 | (g / n) << 8 | (b / n));
    }
    unsigned mode3 = 0, idx3 = 0;                /* DXT3/5 blocks in DXT1's three-colour mode, and those using index 3 */
    if (px && L && L->native && L->d.dxt != 1) {
        uint32_t nb = ((L->w + 3) / 4) * ((L->h + 3) / 4);
        for (uint32_t i = 0; i < nb; ++i) {
            const uint8_t *cb = (const uint8_t *)px + (size_t)i * 16 + 8;
            uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
            if (c0 > c1) continue;
            ++mode3;
            uint32_t idx = (uint32_t)(cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24);
            if ((idx & (idx >> 1) & 0x55555555u)) ++idx3;
        }
    }
    xv_logf("[h2/tex] unit=%u code=%02X %s offset=%08X fmt=%08X addr=%08X ctrl=%08X ctrl1=%08X filter=%08X rect=%08X pal=%08X dims=%ux%u levels=%u cube=%d bytes=%u serial=%llu mean=%08X mode3=%u idx3=%u\n",
            unit, (unsigned)code, names[outcome], s->setup[base / 4], s->setup[(base + 4) / 4], s->setup[(base + 8) / 4],
            s->setup[(base + 0xC) / 4], s->setup[(base + 0x10) / 4], s->setup[(base + 0x14) / 4], s->setup[(base + 0x1C) / 4],
            s->setup[(base + 0x20) / 4], L ? L->w : 0, L ? L->h : 0, L ? L->levels : 0, L ? L->cube : 0, L ? L->src_bytes : 0,
            (unsigned long long)serial, mean, mode3, idx3);
}

static int locate(const h2_command_state *s, const h2_kelvin_clear *c, unsigned unit, uint32_t cap, unsigned want_mips, located *L)
{
#define FAIL(outcome) do { note(code, outcome); trace(s, unit, code, outcome, L, 0, NULL); return 0; } while (0)
    if (!s || !c || !c->read_instance || !c->map_physical || unit >= 4) return 0;
    unsigned base = 0x1B00 + unit * 64;
    uint32_t fmt = s->setup[(base + 4) / 4], ctrl = s->setup[(base + 0xC) / 4];
    if (!(ctrl & 0x40000000u)) return 0;                 /* unit disabled */
    memset(L, 0, sizeof *L);
    L->code = (fmt >> 8) & 0xFF; L->lw = (fmt >> 20) & 15; L->lh = (fmt >> 24) & 15;
    unsigned selector = fmt & 3, code = L->code;
    fmt_desc d;
    L->levels = 1;
    if (!describe_format(code, &d)) FAIL(O_UNSUPPORTED);
    L->d = d;
    if ((selector != 1 && selector != 2) || !(s->dma_valid & (1u << selector))) FAIL(O_NOMAP);

    if (d.linear) {
        uint32_t rect = s->setup[(base + 0x1C) / 4];
        L->w = rect >> 16; L->h = rect & 0xFFFF; L->pitch = s->setup[(base + 0x10) / 4] >> 16;
        if (!L->w || !L->h || L->w > 4096 || L->h > 4096 || (uint64_t)L->w * d.bytes > L->pitch) FAIL(O_NOMAP);
        L->src_bytes = (uint32_t)((uint64_t)(L->h - 1) * L->pitch + (uint64_t)L->w * d.bytes);
    } else {
        if (L->lw > 12 || L->lh > 12) FAIL(O_TOOLARGE);
        L->w = 1u << L->lw; L->h = 1u << L->lh;
        /* Mip chain: SET_TEXTURE_FORMAT bits 16-19 count the levels (level 0 included). The
         * levels are stored back to back, each halving both dimensions (minimum one texel, or
         * one 4x4 block for DXT). Only the GXM path asks for them, and only down to 8-texel-wide
         * levels: a GXM linear texture's implicit row stride is its width rounded up to 8
         * texels, so stopping there keeps every level's stride equal to its width. */
        unsigned levels = want_mips ? (fmt >> 16) & 15 : 1;
        if (levels < 1) levels = 1;
        L->src_bytes = 0;
        for (unsigned i = 0; i < levels; ++i) {
            uint32_t w = L->w >> i, h = (L->h >> i) ? L->h >> i : 1;
            if (i && w < 8) { levels = i; break; }
            L->lvl_src[i] = L->src_bytes;
            if (d.dxt) { uint32_t bx = (w + 3) / 4, by = (h + 3) / 4; if (!i) { L->blocks_x = bx; L->blocks_y = by; } L->src_bytes += bx * by * d.bytes; }
            else L->src_bytes += w * h * d.bytes;
        }
        L->levels = levels;
        /* Cube map (SET_TEXTURE_FORMAT bit 2): six square faces stored back to back, each
         * holding the unit's whole mip chain padded to 128 bytes. The GPU path decodes level
         * 0 of every face; the software path keeps seeing face 0 as a plain image. */
        if (want_mips && ((fmt >> 2) & 1)) {
            if (L->lw != L->lh) FAIL(O_UNSUPPORTED);
            unsigned all = (fmt >> 16) & 15; if (all < 1) all = 1;
            uint32_t face = 0;
            for (unsigned i = 0; i < all; ++i) {
                uint32_t w = (L->w >> i) ? L->w >> i : 1, h = (L->h >> i) ? L->h >> i : 1;
                face += d.dxt ? ((w + 3) / 4) * ((h + 3) / 4) * d.bytes : w * h * d.bytes;
            }
            L->face_stride = (face + 127u) & ~127u;
            L->src_bytes = L->face_stride * 6; L->levels = 1; L->cube = 1;
        }
    }
    L->texels = 0;
    for (unsigned i = 0; i < L->levels; ++i) { uint32_t w = L->w >> i, h = (L->h >> i) ? L->h >> i : 1; L->lvl_dst[i] = L->texels; L->texels += w * h; }
    /* GXM cube faces of 16x16 or more (32-bit texels) start on 2048-byte boundaries. */
    if (L->cube) { uint32_t face = L->w * L->h; if (L->w >= 16) face = (face + 511u) & ~511u; L->face_texels = face; L->texels = 6 * face; }
    /* DXT for the GPU: the blocks go up as UBC1/2/3 in GXM's swizzled layout (no decode, 4-8x
     * smaller). Levels are back to back; compressed cube faces of 32x32 or more start on
     * 2048-byte boundaries. The cap counts output bytes as texels here. */
    if (want_mips && d.dxt && !g_decode_instead && (ubc_native_mask() & (1u << (d.dxt >> 1)))) {   /* XV_UBC_NATIVE bits: DXT1, DXT3, DXT5 */
        L->native = 1; L->out_bytes = 0;
        if (L->cube) {
            uint32_t face = L->blocks_x * L->blocks_y * d.bytes;
            if (L->w >= 32) face = (face + 2047u) & ~2047u;
            L->out_bytes = 6 * face; L->face_texels = face / 4;
        } else
            for (unsigned i = 0; i < L->levels; ++i) {
                uint32_t w = L->w >> i, h = (L->h >> i) ? L->h >> i : 1;
                L->lvl_out[i] = L->out_bytes; L->out_bytes += ((w + 3) / 4) * ((h + 3) / 4) * d.bytes;
            }
        L->texels = (L->out_bytes + 3) / 4;
    }
    if (L->texels > cap) FAIL(O_TOOLARGE);

    h2_dma_object dma;
    uint32_t phys;
    if (!h2_dma_load(c->read_instance, c->opaque, s->dma[selector], &dma) ||
        !h2_dma_resolve(&dma, s->setup[base / 4], L->src_bytes, 0, c->physical_bytes, &phys)) FAIL(O_NOMAP);
    L->src = h2_map_physical_read ? h2_map_physical_read(phys, L->src_bytes) : c->map_physical(c->opaque, phys, L->src_bytes);
    if (!L->src) FAIL(O_NOMAP);

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
            !(psrc = c->map_physical(c->opaque, pphys, pal_len * 4))) FAIL(O_NOMAP);
        memcpy(L->palette, psrc, pal_len * 4);
        L->pal_mask = pal_len - 1;
    }
    L->key[0] = s->setup[base / 4]; L->key[1] = fmt; L->key[2] = s->setup[(base + 0x10) / 4];
    L->key[3] = s->setup[(base + 0x1C) / 4]; L->key[5] = s->dma[selector];
    L->key[4] = d.kind == PF_P8 ? s->setup[(base + 0x20) / 4] : 0;   /* the palette register only matters to P8 (the game rotates it) */
    return 1;
#undef FAIL
}

static void decode_level(const located *L, unsigned level, const uint8_t *base, uint32_t *rgba)
{
    const fmt_desc d = L->d;
    const uint8_t *src = base + L->lvl_src[level];
    uint32_t w = L->w >> level, h = (L->h >> level) ? L->h >> level : 1, pitch = L->pitch;
    unsigned lw = L->lw > level ? L->lw - level : 0, lh = L->lh > level ? L->lh - level : 0;
    if (d.dxt) {
        /* Compressed images are not swizzled: the 4x4 blocks are stored row by row (the
         * hardware consumes S3TC data as laid out by the encoder; xemu uploads it untouched).
         * Only 8x8 menu images (2x2 blocks, where Morton and row order agree) were ever seen
         * before the level's 256x256 maps came out scrambled. */
        (void)lw; (void)lh;
        uint32_t blocks_x = (w + 3) / 4, blocks_y = (h + 3) / 4;
        for (uint32_t by = 0; by < blocks_y; ++by)
            for (uint32_t bx = 0; bx < blocks_x; ++bx) {
                uint32_t bi = by * blocks_x + bx, tile[16];
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

/* Every level of the chain, back to back; each level's rows are packed at its width.
 * A cube map instead yields its six faces (level 0) in the GXM swizzled order a square
 * SCE_GXM_TEXTURE_CUBE face uses: Morton with y in the even bits and x in the odd bits
 * (Vita3K decode_morton2_y(code) = compact(code >> 0)), the transpose of the NV2A order. */
/* One level's DXT blocks (row-major in the source) into GXM's swizzled block order:
 * Morton with the block row in the even bits and the column in the odd bits, the larger
 * dimension's remaining bits on top - the same transposition the cube faces use. */
/* XV_UBC_SWAP=<mask> (bit 0 DXT1, bit 1 DXT3, bit 2 DXT5): exchange the red and blue fields
 * of those blocks' RGB565 endpoints. Vita3K presents UBC3 (DXT5) texels with red and blue
 * exchanged while UBC1 is right (its BC3 path, not the GPU's); hardware is expected to take
 * the standard order for all three, so this is off by default. A DXT1
 * block's mode is chosen by comparing its endpoints, so when the exchange would reverse
 * their order the endpoints are swapped back and the indices remapped (0<->1, and 2<->3 in
 * four-colour mode). DXT3/5 colour blocks always decode in four-colour mode. */
static uint16_t swap_rb565(uint16_t c) { return (uint16_t)((c & 0x07E0u) | ((c >> 11) & 0x1Fu) | ((c & 0x1Fu) << 11)); }
void menu_dxt_swap_rb(uint8_t *blk, unsigned dxt)
{
    uint8_t *cb = dxt == 1 ? blk : blk + 8;
    uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
    uint16_t s0 = swap_rb565(c0), s1 = swap_rb565(c1);
    if (dxt == 1 && (c0 > c1) != (s0 > s1)) {
        uint32_t idx = (uint32_t)(cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24);
        uint32_t flip = c0 > c1 ? 0x55555555u : (~(idx >> 1) & 0x55555555u);   /* 4-colour: all pairs; 3-colour: only 0/1 */
        idx ^= flip;
        cb[4] = (uint8_t)idx; cb[5] = (uint8_t)(idx >> 8); cb[6] = (uint8_t)(idx >> 16); cb[7] = (uint8_t)(idx >> 24);
        uint16_t t = s0; s0 = s1; s1 = t;
    }
    cb[0] = (uint8_t)s0; cb[1] = (uint8_t)(s0 >> 8); cb[2] = (uint8_t)s1; cb[3] = (uint8_t)(s1 >> 8);
}
static unsigned ubc_swap_mask(void)
{
    static int mask = -1;
    if (mask < 0) { const char *e = getenv("XV_UBC_SWAP"); mask = e ? atoi(e) & 7 : 0; }
    return (unsigned)mask;
}
/* DXT3/5 colour blocks: the NV2A applies DXT1's rule (c0 <= c1 -> three colours, index 3
 * black) and Halo 2's assets rely on it, while GXM and Mesa decode them four-colour
 * regardless. A three-colour block that only uses indices 0 and 1 is rewritten exactly
 * (endpoints swapped, index bit flipped); one that uses index 2 or 3 cannot be, so the whole
 * image is decoded on the CPU instead (decode_instead). */
static int block_needs_decode(const uint8_t *blk, unsigned dxt)
{
    const uint8_t *cb = dxt == 1 ? blk : blk + 8;
    uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
    if (dxt == 1 || c0 > c1) return 0;
    uint32_t idx = (uint32_t)(cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24);
    return (idx & 0xAAAAAAAAu) != 0;             /* an index 2 or 3 somewhere */
}
static void rewrite_mode3_block(uint8_t *blk, unsigned dxt)
{
    if (dxt == 1) return;
    uint8_t *cb = blk + 8;
    uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
    if (c0 >= c1) return;                        /* four-colour already, or equal endpoints (either order decodes the same) */
    cb[0] = (uint8_t)c1; cb[1] = (uint8_t)(c1 >> 8); cb[2] = (uint8_t)c0; cb[3] = (uint8_t)(c0 >> 8);
    for (unsigned i = 4; i < 8; ++i) cb[i] ^= 0x55;   /* indices 0 <-> 1 (only those are present) */
}
static int image_needs_decode(const located *L)
{
    if (!L->native || L->d.dxt == 1) return 0;
    if (L->cube) {
        uint32_t nb = L->blocks_x * L->blocks_y;
        for (unsigned f = 0; f < 6; ++f)
            for (uint32_t i = 0; i < nb; ++i) if (block_needs_decode(L->src + (size_t)f * L->face_stride + (size_t)i * 16, L->d.dxt)) return 1;
        return 0;
    }
    for (uint32_t off = 0; off + 16 <= L->src_bytes; off += 16) if (block_needs_decode(L->src + off, L->d.dxt)) return 1;
    return 0;
}
uint32_t menu_texture_mode3_decoded(void) { return g_mode3_decoded; }
static void native_level(const located *L, const uint8_t *src, uint32_t w, uint32_t h, uint8_t *out)
{
    uint32_t bx_count = (w + 3) / 4, by_count = (h + 3) / 4;
    unsigned lbw = 0, lbh = 0;
    int swap = (ubc_swap_mask() >> (L->d.dxt >> 1)) & 1;
    while ((1u << lbw) < bx_count) ++lbw;
    while ((1u << lbh) < by_count) ++lbh;
    for (uint32_t by = 0; by < by_count; ++by)
        for (uint32_t bx = 0; bx < bx_count; ++bx) {
            uint8_t *dst = out + (size_t)morton(by, bx, lbh, lbw) * L->d.bytes;
            memcpy(dst, src + ((size_t)by * bx_count + bx) * L->d.bytes, L->d.bytes);
            rewrite_mode3_block(dst, L->d.dxt);
            if (swap) menu_dxt_swap_rb(dst, L->d.dxt);
        }
}
static void decode(const located *L, uint32_t *rgba)
{
    if (L->native) {
        uint8_t *out = (uint8_t *)rgba;
        if (L->cube) { for (unsigned f = 0; f < 6; ++f) native_level(L, L->src + (size_t)f * L->face_stride, L->w, L->h, out + (size_t)f * L->face_texels * 4); }
        else for (unsigned i = 0; i < L->levels; ++i) native_level(L, L->src + L->lvl_src[i], L->w >> i, (L->h >> i) ? L->h >> i : 1, out + L->lvl_out[i]);
        return;
    }
    if (!L->cube) { for (unsigned i = 0; i < L->levels; ++i) decode_level(L, i, L->src, rgba + L->lvl_dst[i]); return; }
    uint32_t w = L->w, h = L->h;
    uint32_t *linear = malloc((size_t)w * h * 4);
    if (!linear) return;
    for (unsigned f = 0; f < 6; ++f) {
        decode_level(L, 0, L->src + (size_t)f * L->face_stride, linear);
        uint32_t *out = rgba + (size_t)f * L->face_texels;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) out[morton(y, x, L->lh, L->lw)] = linear[(size_t)y * w + x];
    }
    free(linear);
}

int menu_texture_load(const h2_command_state *s, const h2_kelvin_clear *c,
                      unsigned unit, uint32_t *rgba, uint32_t cap, uint32_t *ow, uint32_t *oh,
                      int *out_linear)
{
    located L;
    if (!locate(s, c, unit, cap, 0, &L)) return 0;
    decode(&L, rgba);
    note(L.code, O_OK); trace(s, unit, L.code, O_OK, &L, 0, rgba);
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

typedef struct { uint32_t key[6]; uint64_t hash, pal_hash; uint32_t *texels; uint32_t w, h, bytes, levels, ntexels; int linear, cube, native; uint64_t used;
                 int tracked; uint32_t page_lo, page_hi, verified; unsigned clean_hits;
                 uint8_t *snapshot; uint32_t snapshot_bytes; } tex_entry;   /* snapshot: diagnostic copy of the source */
/* A Halo 2 level binds hundreds of distinct textures (base, bump, lightmap, cube per
 * draw); 64 entries thrashed (6780 misses per 60 flips, 10 s of decoding). */
enum { TEX_CACHE_ENTRIES = 512, TEX_CACHE_BUDGET = 64u << 20 };   /* within the 128 MB newlib heap */
static tex_entry g_entries[TEX_CACHE_ENTRIES];
static size_t g_cache_bytes;
static uint64_t g_hits, g_misses, g_hash_bytes, g_hash_skipped;

const uint32_t *menu_texture_acquire(const h2_command_state *s, const h2_kelvin_clear *c, unsigned unit,
                                     uint64_t serial, uint32_t cap, unsigned want_mips, uint32_t *ow, uint32_t *oh,
                                     int *out_linear, uint64_t *out_hash, uint32_t *out_levels, uint32_t *out_texels,
                                     int *out_cube, int *out_native)
{
    located L;
    if (!locate(s, c, unit, cap, want_mips, &L)) return NULL;
    if (image_needs_decode(&L)) {                /* three-colour DXT3/5 blocks the GPU would decode differently */
        g_decode_instead = 1;
        int ok = locate(s, c, unit, cap, want_mips, &L);
        g_decode_instead = 0;
        if (!ok) return NULL;
        ++g_mode3_decoded;
    }
    /* The source is normally re-hashed on every draw (level textures reach ~130 KB
     * each). With the arena's per-page write epochs available, an entry with the same
     * key whose source pages carry no stamp at or after its last full read is proven
     * unchanged and reuses its hash; every 64th such use still reads the source in
     * full, and a mismatch there is a strict stop: a writer the stamps did not see. */
    /* P8 images are tracked like the rest; their palette (<= 1 KB, mapped separately) is
     * hashed on every use and compared, so a palette change still forces a full re-hash.
     * (Excluding them re-hashed every 512x512 bump map on every draw: ~1.2 GB per 60 flips.) */
    int tracked = xv_page_epoch != NULL && &g_xram && g_xram && (uintptr_t)L.src >= (uintptr_t)g_xram;
    uint64_t pal_hash = L.d.kind == PF_P8 ? content_hash((const uint8_t *)L.palette, (L.pal_mask + 1) * 4, 0x9E3779B97F4A7C15ull) : 0;
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
    if (clean && pal_hash != keyed->pal_hash) clean = 0;   /* same pixels, different palette */
    if (clean && (++keyed->clean_hits & 63)) { hash = keyed->hash; ++g_hash_skipped; }
    else {
        hash = content_hash(L.src, L.src_bytes, 0x243F6A8885A308D3ull ^ L.src_bytes);
        g_hash_bytes += L.src_bytes;
        hash_account(L.key[0], L.src_bytes, unit, L.code);
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
    tex_entry *e = (keyed && keyed->hash == hash && keyed->levels == L.levels && keyed->cube == L.cube && keyed->native == (L.native ? (int)L.d.dxt : 0)) ? keyed : NULL;
    if (e) { ++g_hits; }
    else {
        ++g_misses;
        if (keyed) victim = keyed;                          /* same key, changed content: replace in place */
        uint32_t bytes = L.texels * 4;
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
        if (!px) {                                   /* heap exhausted: the draw goes out with the unit unbound */
            static unsigned oom_logged;
            if (xv_logf && oom_logged++ < 8) xv_logf("[h2/tex] out of memory for %u bytes (cache %zu bytes, %u entries)\n", bytes, g_cache_bytes, TEX_CACHE_ENTRIES);
            return NULL;
        }
        decode(&L, px);
        note(L.code, O_OK); trace(s, unit, L.code, O_OK, &L, serial, px);
        memcpy(victim->key, L.key, sizeof L.key);
        victim->hash = hash; victim->texels = px; victim->w = L.w; victim->h = L.h; victim->bytes = bytes;
        victim->levels = L.levels; victim->ntexels = L.texels; victim->cube = L.cube; victim->native = L.native ? (int)L.d.dxt : 0;
        victim->pal_hash = pal_hash;
        victim->linear = L.d.linear; g_cache_bytes += bytes;
        victim->clean_hits = 0;
        e = victim;
    }
    if (!clean || !(e->clean_hits & 63)) {     /* hashed this time: the stamps from here on are what matter */
        /* Open a new epoch after reading: a buffer written and then bound several times in one
         * frame (the bloom buffers, ~15 binds each) was re-hashed on every bind because its
         * stamps equalled the verification epoch. Only writers after this point count now. */
        if (tracked && &xv_write_epoch) ++xv_write_epoch;
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
    if (out_levels) *out_levels = e->levels;
    if (out_texels) *out_texels = e->ntexels;
    if (out_cube) *out_cube = e->cube;
    if (out_native) *out_native = e->native;
    return e->texels;
}

void menu_texture_cache_stats(uint64_t *hits, uint64_t *misses, size_t *bytes)
{ *hits = g_hits; *misses = g_misses; *bytes = g_cache_bytes; }
uint64_t menu_texture_hashed_bytes(void) { return g_hash_bytes; }
uint64_t menu_texture_hash_skipped(void) { return g_hash_skipped; }
