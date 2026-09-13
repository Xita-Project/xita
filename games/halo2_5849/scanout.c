#include "scanout.h"
static int span(const void *pointer, size_t bytes)
{ return pointer && bytes <= UINTPTR_MAX - (uintptr_t)pointer; }
static int overlap(const void *a, size_t an, const void *b, size_t bn)
{ return (uintptr_t)a < (uintptr_t)b + bn && (uintptr_t)b < (uintptr_t)a + an; }
int h2_scanout_convert(uint8_t *output, size_t output_bytes,
                         const uint8_t *input, size_t input_bytes,
                         const uint8_t *rgb_gamma, size_t gamma_bytes)
{
    if (output_bytes < H2_DISPLAY_BYTES || input_bytes < H2_SCANOUT_BYTES || gamma_bytes < 768 ||
        !span(output, H2_DISPLAY_BYTES) || !span(input, H2_SCANOUT_BYTES) || !span(rgb_gamma, 768) ||
        overlap(output, H2_DISPLAY_BYTES, input, H2_SCANOUT_BYTES) ||
        overlap(output, H2_DISPLAY_BYTES, rgb_gamma, 768)) return 0;
    for (unsigned y = 0; y < H2_DISPLAY_HEIGHT; ++y) {
        for (unsigned x = 0; x < H2_DISPLAY_WIDTH; ++x) {
            uint8_t *to = output + (y * H2_DISPLAY_WIDTH + x) * 4;
            if (x >= 160 && x < 800 && y >= 32 && y < 512) {
                const uint8_t *from = input + ((y - 32) * H2_SCANOUT_WIDTH + x - 160) * 4;
                to[0] = rgb_gamma[from[2] * 3];
                to[1] = rgb_gamma[from[1] * 3 + 1];
                to[2] = rgb_gamma[from[0] * 3 + 2];
            } else to[0] = to[1] = to[2] = 0;
            to[3] = 255;
        }
    }
    return 1;
}
