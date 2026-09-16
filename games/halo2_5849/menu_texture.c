#include "menu_texture.h"
#include <string.h>

static uint32_t argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{ return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }

/* Expand an RGB565 colour to (r,g,b) 8-bit. */
static void rgb565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    *g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
    *b = (uint8_t)((c & 0x1F) * 255 / 31);
}

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


int menu_texture_load(const h2_command_state *s, const h2_kelvin_clear *c,
                      unsigned unit, uint32_t *rgba, uint32_t cap, uint32_t *ow, uint32_t *oh)
{
    if (!s || !c || !c->read_instance || !c->map_physical || unit >= 4) return 0;
    unsigned base = 0x1B00 + unit * 64;
    uint32_t fmt = s->setup[(base + 4) / 4], ctrl = s->setup[(base + 0xC) / 4];
    if (!(ctrl & 0x40000000u)) return 0;                 /* unit disabled */
    unsigned code = (fmt >> 8) & 0xFF, lw = (fmt >> 20) & 15, lh = (fmt >> 24) & 15;
    unsigned selector = fmt & 3;
    if ((selector != 1 && selector != 2) || !(s->dma_valid & (1u << selector))) return 0;
    if (lw > 10 || lh > 10) return 0;
    uint32_t w = 1u << lw, h = 1u << lh;
    if ((uint64_t)w * h > cap) return 0;

    uint32_t bpb, blocks_x = (w + 3) / 4, blocks_y = (h + 3) / 4, src_bytes;
    int dxt;
    if (code == 0x0C) { dxt = 1; bpb = 8; }              /* DXT1 */
    else if (code == 0x0E) { dxt = 3; bpb = 16; }         /* DXT23 */
    else if (code == 0x06) { dxt = 0; bpb = 0; }          /* A8R8G8B8 swizzled */
    else return 0;
    src_bytes = dxt ? blocks_x * blocks_y * bpb : w * h * 4;

    h2_dma_object dma;
    uint32_t phys;
    if (!h2_dma_load(c->read_instance, c->opaque, s->dma[selector], &dma) ||
        !h2_dma_resolve(&dma, s->setup[base / 4], src_bytes, 0, c->physical_bytes, &phys)) return 0;
    const uint8_t *src = c->map_physical(c->opaque, phys, src_bytes);
    if (!src) return 0;

    if (dxt) {
        unsigned lbw = lw > 2 ? lw - 2 : 0, lbh = lh > 2 ? lh - 2 : 0;
        for (uint32_t by = 0; by < blocks_y; ++by)
            for (uint32_t bx = 0; bx < blocks_x; ++bx) {
                uint32_t bi = morton(bx, by, lbw, lbh);
                const uint8_t *blk = src + (size_t)bi * bpb;
                uint32_t *dst = rgba + (size_t)(by * 4) * w + bx * 4;
                if (dxt == 1) menu_dxt1_block(blk, dst, w);
                else menu_dxt3_block(blk, dst, w);
            }
    } else {
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t si = morton(x, y, lw, lh);
                memcpy(&rgba[y * w + x], src + (size_t)si * 4, 4);
            }
    }
    *ow = w; *oh = h;
    return 1;
}
