#include "dxt23_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint8_t storage[256], expected[256], before[256];
    unsigned cases = 0;
    for (int offset = -63; offset <= 63; ++offset) {
        for (unsigned i = 0; i < sizeof storage; ++i) storage[i] = (i * 71u) ^ (i >> 2);
        memcpy(before, storage, sizeof before); memcpy(expected, storage, sizeof expected);
        unsigned destination = 96 + offset;
        for (unsigned i = 0; i < 64; ++i) {
            unsigned block = i / 16, source_block = ((block & 1) << 1) | (block >> 1);
            expected[destination + i] = before[96 + source_block * 16 + i % 16];
        }
        assert(h2_dxt23_gxm_8x8(storage + 96, 64, storage + destination, 64));
        assert(!memcmp(storage, expected, sizeof storage)); ++cases;
    }
    memcpy(before, storage, sizeof before);
    for (unsigned n = 0; n < 128; ++n) if (n != 64) {
        assert(!h2_dxt23_gxm_8x8(storage, n, storage + 128, 64));
        assert(!h2_dxt23_gxm_8x8(storage, 64, storage + 128, n));
    }
    assert(!h2_dxt23_gxm_8x8(NULL, 64, storage, 64));
    assert(!h2_dxt23_gxm_8x8(storage, 64, NULL, 64));
    assert(!h2_dxt23_gxm_8x8((void *)(UINTPTR_MAX - 32), 64, storage, 64));
    assert(!h2_dxt23_gxm_8x8(storage, 64, (void *)(UINTPTR_MAX - 32), 64));
    assert(!memcmp(storage, before, sizeof storage));
    printf("DXT23 layout: %u overlapping spans, rejected sizes/pointers unchanged\n", cases);
}
