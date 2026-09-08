#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../xv_index_copy.h"

int main(void)
{
    const unsigned counts[] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33,
        255, 256, 257, 511, 512, 513, 65536, 131072};
    uint32_t random = 17;
    for (unsigned size = 0; size < sizeof counts / sizeof *counts; size++)
        for (unsigned offset = 0; offset < 4; offset++)
            for (unsigned mode = 0; mode < 4; mode++) {
                unsigned n = counts[size], expected = 0;
                uint8_t *allocation = malloc(n * 2 + 4), *src = allocation + offset;
                uint8_t *dst = malloc(n * 2 + 32);
                assert(allocation && dst);
                memset(dst, 0xA5, n * 2 + 32);
                for (unsigned i = 0; i < n; i++) {
                    random = random * 1664525u + 1013904223u;
                    uint16_t value = mode == 0 ? 0 : mode == 1 ? 65535 : mode == 2 ? i : random >> 16;
                    memcpy(src + i * 2, &value, 2);
                    if ((unsigned)value + 1 > expected) expected = (unsigned)value + 1;
                }
                for (unsigned mode = 0; mode < 2; mode++) {
                    memset(dst + 16, 0xA5, n * 2);
                    unsigned bound = mode ? xv_index_copy_bounds_neon(dst + 16, src, n) :
                                            xv_index_copy_bounds(dst + 16, src, n);
                    assert(bound == expected);
                    assert(!memcmp(src, dst + 16, n * 2));
                }
                for (unsigned i = 0; i < 16; i++)
                    assert(dst[i] == 0xA5 && dst[16 + n * 2 + i] == 0xA5);
                free(dst); free(allocation);
            }
    assert(xv_index_copy_bounds(NULL, NULL, 0) == 0);
    assert(xv_index_copy_bounds_neon(NULL, NULL, 0) == 0);
    puts("PASS: 672 index snapshots, vector/chunk boundaries, unaligned sources, full pools, bounds and guards");
}
