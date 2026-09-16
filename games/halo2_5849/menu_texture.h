/* Decode a bound NV2A texture unit to linear 0xAARRGGBB for the software menu
 * backend. Supports the swizzled (Morton) 8/16/32-bit colour formats, the
 * linear pitch formats (addressed in texels: out_linear=1), P8 through the unit's
 * palette, and DXT1/DXT23/DXT45 blocks; YUV and depth formats are not decoded. Returns 1 and fills rgba (width*height texels) on
 * success; 0 if the unit is disabled, the format is unsupported, the size
 * exceeds the caller's cap, or the source will not map. */
#pragma once
#include <stddef.h>
#include "command_state.h"
#include "kelvin_clear.h"

int menu_texture_load(const h2_command_state *state, const h2_kelvin_clear *clear,
                      unsigned unit, uint32_t *rgba, uint32_t cap_texels,
                      uint32_t *out_w, uint32_t *out_h, int *out_linear);

/* Cached variant: returns a cache-owned decoded image (valid until a later
 * acquire with a different serial evicts it; entries used by `serial` are kept).
 * Keyed by the unit registers plus a hash of all source bytes, so never stale. */
const uint32_t *menu_texture_acquire(const h2_command_state *state, const h2_kelvin_clear *clear,
                                     unsigned unit, uint64_t serial, uint32_t cap_texels,
                                     uint32_t *out_w, uint32_t *out_h, int *out_linear, uint64_t *out_hash);
void menu_texture_cache_stats(uint64_t *hits, uint64_t *misses, size_t *bytes);

/* Register-only view of a unit (no guest memory access): returns the enable bit
 * and reports the colour format code and the described size, for logging. */
int menu_texture_describe(const h2_command_state *state, unsigned unit,
                          uint32_t *code, uint32_t *width, uint32_t *height);

/* Per-format load outcomes since boot, formatted "CC:ok/unsupported/toolarge/nomap ...". */
size_t menu_texture_stats(char *buf, size_t cap);

/* Exposed for unit testing: decode one DXT1 (8-byte), DXT3 or DXT5 (16-byte)
 * block to a 4x4 RGBA tile written row-major into out with the given stride. */
void menu_dxt1_block(const uint8_t *block, uint32_t *out, uint32_t stride);
void menu_dxt3_block(const uint8_t *block, uint32_t *out, uint32_t stride);
void menu_dxt5_block(const uint8_t *block, uint32_t *out, uint32_t stride);
