/* Read-only views of the observed one-level linear X8R8G8B8/A8R8G8B8 textures.
 * This does not validate shader/sampler state or execute a draw. */
#pragma once
#include "command_state.h"
typedef struct h2_linear_texture {
    const uint8_t *pixels;
    uint32_t physical, bytes, width, height, pitch, method_format;
} h2_linear_texture;
/* Resolve through the selected existing DMA object and whole-span mapper.
 * No guest, command or output mutation on failure. The returned bytes are
 * read-only, include row padding and remain owned by the mapper. */
int h2_linear_texture_read(const h2_command_state *state, const h2_kelvin_clear *memory,
                            unsigned unit, h2_linear_texture *view);
/* The observed 8x8, one-level DXT23 allocation, still compressed. This API
 * neither decodes pixels nor validates sampler/combiner behavior. */
typedef struct h2_block_texture {
    const uint8_t *blocks;
    uint32_t physical, bytes, width, height, block_pitch, method_format;
} h2_block_texture;
int h2_dxt23_texture_read(const h2_command_state *state, const h2_kelvin_clear *memory,
                           unsigned unit, h2_block_texture *view);
/* Native183's four enabled samplers reference the same 8x8 BC1 allocation.
 * Resolve only its exact one-level 2D format and all 32 compressed bytes.
 * Returned ownership, read-only behavior and rejection match DXT23 above. */
int h2_dxt1_texture_read(const h2_command_state *state, const h2_kelvin_clear *memory,
                         unsigned unit, h2_block_texture *view);
