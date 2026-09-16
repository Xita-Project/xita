/* Decode a bound NV2A texture (unit 0) to linear RGBA for the software menu
 * backend. Supports the formats the menu binds: DXT1 (code 0x0C), DXT23/DXT3
 * (0x0E) and swizzled A8R8G8B8 / X8R8G8B8 (0x06/0x0C-linear). Blocks/pixels are
 * de-swizzled from NV2A Morton order. Returns 1 and fills rgba (0xAARRGGBB,
 * width*height texels) on success; 0 if the unit is disabled, the format is
 * unsupported, the size exceeds the caller's cap, or the source will not map. */
#pragma once
#include "command_state.h"
#include "kelvin_clear.h"

int menu_texture_load(const h2_command_state *state, const h2_kelvin_clear *clear,
                      unsigned unit, uint32_t *rgba, uint32_t cap_texels,
                      uint32_t *out_w, uint32_t *out_h);

/* Exposed for unit testing: decode one DXT1 (8-byte) or DXT3 (16-byte) block to
 * a 4x4 RGBA tile written row-major into out with the given stride (in texels). */
void menu_dxt1_block(const uint8_t *block, uint32_t *out, uint32_t stride);
void menu_dxt3_block(const uint8_t *block, uint32_t *out, uint32_t stride);
