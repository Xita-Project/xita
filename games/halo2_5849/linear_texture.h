/* Read-only view of the observed one-level linear X8R8G8B8 texture.
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
