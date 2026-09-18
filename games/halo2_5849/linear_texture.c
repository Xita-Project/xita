#include "linear_texture.h"
#include <stddef.h>

int h2_linear_texture_read(const h2_command_state *s, const h2_kelvin_clear *c,
                            unsigned unit, h2_linear_texture *view)
{
    if (!s || !c || !view || !c->read_instance || !c->map_physical || unit >= 4) return 0;
    unsigned base = 0x1B00 + unit * 64;
    const unsigned required[] = {0, 4, 0xC, 0x10, 0x1C};
    for (unsigned i = 0; i < sizeof required / sizeof *required; ++i) {
        unsigned method = base + required[i];
        if (!(s->setup_valid[method / 128] & (1u << ((method / 4) % 32)))) return 0;
    }
    uint32_t format = s->setup[(base + 4) / 4], selector = format & 3;
    /* Original method encoding: DMA A=1, B=2; border color; 2D; linear
     * X8R8G8B8 or A8R8G8B8; exactly one mip level; no cube or logarithmic sizes.
     * The view preserves every byte; alpha interpretation belongs to a draw. */
    if ((selector != 1 && selector != 2) ||
        ((format & ~3u) != 0x00011E28u && (format & ~3u) != 0x00011228u) ||
        !(s->setup[(base + 0xC) / 4] & 0x40000000u) ||
        !(s->dma_valid & (1u << selector))) return 0;
    uint32_t pitch_word = s->setup[(base + 0x10) / 4];
    uint32_t rect = s->setup[(base + 0x1C) / 4], width = rect >> 16, height = rect & 0xFFFF;
    uint32_t pitch = pitch_word >> 16;
    /* Explicit view limits, not a claim of general hardware format support. */
    if (!width || !height || width > 4096 || height > 4096 ||
        (pitch_word & 0xFFFF) || pitch < width * 4) return 0;
    uint64_t extent = (uint64_t)pitch * height;
    if (!extent || extent > UINT32_MAX) return 0;
    h2_dma_object dma;
    uint32_t address;
    if (!h2_dma_load(c->read_instance, c->opaque, s->dma[selector], &dma) ||
        !h2_dma_resolve(&dma, s->setup[base / 4], (uint32_t)extent, 0,
                        c->physical_bytes, &address)) return 0;
    const uint8_t *pixels = c->map_physical(c->opaque, address, (uint32_t)extent);
    if (!pixels || (uintptr_t)pixels > UINTPTR_MAX - ((uint32_t)extent - 1)) return 0;
    h2_linear_texture result = {0};
    result.pixels = pixels; result.physical = address; result.bytes = extent;
    result.width = width; result.height = height; result.pitch = pitch;
    result.method_format = format;
    *view = result;
    return 1;
}

static int block_texture_read(const h2_command_state *s, const h2_kelvin_clear *c,
                              unsigned unit, h2_block_texture *view,
                              uint32_t expected_format, uint32_t bytes,
                              uint32_t width, uint32_t height)
{
    if (!s || !c || !view || !c->read_instance || !c->map_physical || unit >= 4) return 0;
    unsigned base = 0x1B00 + unit * 64;
    const unsigned required[] = {0, 4, 0xC};
    for (unsigned i = 0; i < sizeof required / sizeof *required; ++i) {
        unsigned method = base + required[i];
        if (!(s->setup_valid[method / 128] & (1u << ((method / 4) % 32)))) return 0;
    }
    uint32_t format = s->setup[(base + 4) / 4], selector = format & 3;
    /* Wrappers validate/select the complete one-level format and extent.
     * Returned compressed blocks remain read-only; no decoding here. */
    if ((selector != 1 && selector != 2) || (format & ~3u) != expected_format ||
        !(s->setup[(base + 0xC) / 4] & 0x40000000u) ||
        !(s->dma_valid & (1u << selector))) return 0;
    h2_dma_object dma;
    uint32_t address;
    if (!h2_dma_load(c->read_instance, c->opaque, s->dma[selector], &dma) ||
        !h2_dma_resolve(&dma, s->setup[base / 4], bytes, 0, c->physical_bytes, &address)) return 0;
    const uint8_t *blocks = c->map_physical(c->opaque, address, bytes);
    if (!blocks || (uintptr_t)blocks > UINTPTR_MAX - (bytes - 1)) return 0;
    h2_block_texture result = {0};
    result.blocks = blocks; result.physical = address; result.bytes = bytes;
    result.width = width; result.height = height;
    result.block_pitch = bytes / ((height + 3) / 4);
    result.method_format = format;
    *view = result;
    return 1;
}

int h2_dxt23_texture_read(const h2_command_state *s, const h2_kelvin_clear *c,
                           unsigned unit, h2_block_texture *view)
{
    return block_texture_read(s, c, unit, view, 0x03310E28u, 64, 8, 8);
}
int h2_dxt1_texture_read(const h2_command_state *s, const h2_kelvin_clear *c,
                         unsigned unit, h2_block_texture *view)
{
    return block_texture_read(s, c, unit, view, 0x03310C28u, 32, 8, 8);
}

int h2_dxt23_texture_snapshot_read(const h2_command_state *s, const h2_kelvin_clear *c,
                                    unsigned unit, h2_block_texture *view)
{
    if (!s || unit >= 4) return 0;
    uint32_t format = s->setup[(0x1B04 + unit * 64) / 4];
    unsigned log_width = (format >> 20) & 15, log_height = (format >> 24) & 15;
    /* Stop-only diagnostics: 2D, border color, BC2, one mip, no cube/depth.
     * 4096 is a capture limit, not an extension of any draw admission rule.
     * Clamp each physical block dimension to four texels, including 1x1.
     * All format validity, permissions and full-span mapping follow below. */
    if ((format & ~0x0FF00003u) != 0x00010E28u || log_width > 12 || log_height > 12) return 0;
    uint32_t width = 1u << log_width, height = 1u << log_height;
    uint32_t bytes = ((width + 3) / 4) * ((height + 3) / 4) * 16;
    return block_texture_read(s, c, unit, view, format & ~3u, bytes, width, height);
}

int h2_dxt23_texture_rect_read(const h2_command_state *s, const h2_kelvin_clear *c,
                                    unsigned unit, h2_block_texture *view)
{
    if (!s || unit >= 4) return 0;
    uint32_t format = s->setup[(0x1B04 + unit * 64) / 4];
    unsigned log_width = (format >> 20) & 15, log_height = (format >> 24) & 15;
    /* Render view: one-level BC2, power-of-two axes up to 1024. The consumer
     * separately admits sampler, shader, byte budget and destination ownership. */
    if ((format & ~0x0FF00003u) != 0x00010E28u || log_width > 10 || log_height > 10) return 0;
    uint32_t width = 1u << log_width, height = 1u << log_height;
    uint32_t bytes = ((width + 3) / 4) * ((height + 3) / 4) * 16;
    return block_texture_read(s, c, unit, view, format & ~3u, bytes, width, height);
}
