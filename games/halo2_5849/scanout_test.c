#include "scanout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    uint8_t *input = malloc(H2_SCANOUT_BYTES), *output = malloc(H2_DISPLAY_BYTES + 16);
    uint8_t table[768]; assert(input && output);
    for (unsigned i = 0; i < 256; ++i) {
        table[i * 3] = 255 - i; table[i * 3 + 1] = i ^ 0x5A; table[i * 3 + 2] = i * 3;
    }
    for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 640; ++x) {
        uint8_t *p = input + (y * 640 + x) * 4;
        p[0] = x; p[1] = y; p[2] = x + y; p[3] = x ^ y;
    }
    memset(output, 0xCD, H2_DISPLAY_BYTES + 16);
    assert(h2_scanout_convert(output, H2_DISPLAY_BYTES, input, H2_SCANOUT_BYTES, table, 768));
    for (unsigned y = 0; y < 544; ++y) for (unsigned x = 0; x < 960; ++x) {
        const uint8_t *p = output + (y * 960 + x) * 4;
        if (x >= 160 && x < 800 && y >= 32 && y < 512) {
            unsigned sx = x - 160, sy = y - 32;
            assert(p[0] == 255 - (uint8_t)(sx + sy));
            assert(p[1] == ((uint8_t)sy ^ 0x5A) && p[2] == (uint8_t)(sx * 3));
        } else assert(!p[0] && !p[1] && !p[2]);
        assert(p[3] == 255);
    }
    for (unsigned i = H2_DISPLAY_BYTES; i < H2_DISPLAY_BYTES + 16; ++i) assert(output[i] == 0xCD);
    memset(output, 0xCD, H2_DISPLAY_BYTES);
    assert(!h2_scanout_convert(output, H2_DISPLAY_BYTES - 1, input, H2_SCANOUT_BYTES, table, 768));
    assert(!h2_scanout_convert(output, H2_DISPLAY_BYTES, input, H2_SCANOUT_BYTES - 1, table, 768));
    assert(!h2_scanout_convert(output, H2_DISPLAY_BYTES, input, H2_SCANOUT_BYTES, table, 767));
    assert(!h2_scanout_convert(output, H2_DISPLAY_BYTES, output + 4, H2_SCANOUT_BYTES, table, 768));
    assert(!h2_scanout_convert(output, H2_DISPLAY_BYTES, input, H2_SCANOUT_BYTES, output + 4, 768));
    assert(!h2_scanout_convert(output, H2_DISPLAY_BYTES, (void *)(UINTPTR_MAX - 8), H2_SCANOUT_BYTES, table, 768));
    assert(!h2_scanout_convert(NULL, H2_DISPLAY_BYTES, input, H2_SCANOUT_BYTES, table, 768));
    for (unsigned i = 0; i < H2_DISPLAY_BYTES; ++i) assert(output[i] == 0xCD);
    free(input); free(output);
    puts("Progressive scanout: RGB gamma, channel order, opaque alpha, centered pixels, complete bounds and alias rejection pass.");
}
