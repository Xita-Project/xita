#include "scanout.h"
#include <string.h>
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
    /* Identical output to the per-pixel form (opaque black outside the centred
     * 640x480 image, gamma-mapped BGR->RGB inside), written row-wise: the
     * border is copied from one prepared black row and only image pixels are
     * mapped, which matters when this runs under an emulated CPU every flip. */
    enum { LEFT = 160, TOP = 32 };
    static uint8_t black[H2_DISPLAY_WIDTH * 4]; static int black_ready;
    if (!black_ready) { for (unsigned x = 0; x < H2_DISPLAY_WIDTH; ++x) black[x * 4 + 3] = 255; black_ready = 1; }
    for (unsigned y = 0; y < H2_DISPLAY_HEIGHT; ++y) {
        uint8_t *row = output + (size_t)y * H2_DISPLAY_WIDTH * 4;
        if (y < TOP || y >= TOP + H2_SCANOUT_HEIGHT) { memcpy(row, black, sizeof black); continue; }
        memcpy(row, black, LEFT * 4);
        memcpy(row + (LEFT + H2_SCANOUT_WIDTH) * 4, black, (H2_DISPLAY_WIDTH - LEFT - H2_SCANOUT_WIDTH) * 4);
        const uint8_t *from = input + (size_t)(y - TOP) * H2_SCANOUT_WIDTH * 4;
        uint8_t *to = row + LEFT * 4;
        for (unsigned x = 0; x < H2_SCANOUT_WIDTH; ++x, from += 4, to += 4) {
            to[0] = rgb_gamma[from[2] * 3];
            to[1] = rgb_gamma[from[1] * 3 + 1];
            to[2] = rgb_gamma[from[0] * 3 + 2];
            to[3] = 255;
        }
    }
    return 1;
}
