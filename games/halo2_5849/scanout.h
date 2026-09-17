#pragma once
#include <stddef.h>
#include <stdint.h>
enum {
    H2_SCANOUT_WIDTH = 640, H2_SCANOUT_HEIGHT = 480,
    H2_DISPLAY_WIDTH = 960, H2_DISPLAY_HEIGHT = 544,
    H2_SCANOUT_BYTES = 640 * 480 * 4,
    H2_DISPLAY_BYTES = 960 * 544 * 4
};
/* Pinned progressive mode only: little-endian A8R8G8B8 to Vita A8B8G8R8.
 * Center at native pixel size, apply RGB DAC tables and ignore scanout alpha.
 * Complete spans and all output aliases are checked before the first write. */
int h2_scanout_convert(uint8_t *output, size_t output_bytes,
                         const uint8_t *input, size_t input_bytes,
                         const uint8_t *rgb_gamma, size_t gamma_bytes);
int h2_platform_present(const uint8_t *pixels, size_t bytes,
                          const uint8_t *rgb_gamma, uint32_t *vcount);
/* Queue the frame for the display's next vblank without waiting (the flip path waits once). */
int h2_platform_present_queue(const uint8_t *pixels, size_t bytes, const uint8_t *rgb_gamma);

int h2_platform_blank(int blank);
