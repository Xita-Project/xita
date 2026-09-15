#include "dxt23_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(unsigned size, int (*layout)(const uint8_t *, size_t, uint8_t *, size_t))
{
    uint8_t storage[256], expected[256], before[256];
    unsigned cases = 0;
    for (int offset = 1-(int)size; offset < (int)size; ++offset) {
        for (unsigned i = 0; i < sizeof storage; ++i) storage[i] = (i * 71u) ^ (i >> 2);
        memcpy(before, storage, sizeof before); memcpy(expected, storage, sizeof expected);
        unsigned destination = 96 + offset;
        for (unsigned i = 0; i < size; ++i) {
            unsigned block = i / (size/4), source_block = ((block & 1) << 1) | (block >> 1);
            expected[destination + i] = before[96 + source_block * (size/4) + i % (size/4)];
        }
        assert(layout(storage + 96, size, storage + destination, size));
        assert(!memcmp(storage, expected, sizeof storage)); ++cases;
    }
    memcpy(before, storage, sizeof before);
    for (unsigned n = 0; n < 128; ++n) if (n != size) {
        assert(!layout(storage, n, storage + 128, size));
        assert(!layout(storage, size, storage + 128, n));
    }
    assert(!layout(NULL, size, storage, size));
    assert(!layout(storage, size, NULL, size));
    assert(!layout((void *)(UINTPTR_MAX - (size/2)), size, storage, size));
    assert(!layout(storage, size, (void *)(UINTPTR_MAX - (size/2)), size));
    assert(!memcmp(storage, before, sizeof storage));
    printf("Block layout %u bytes: %u overlapping spans, rejected sizes/pointers unchanged\n", size, cases);
}

int main(void) { check(64,h2_dxt23_gxm_8x8); check(32,h2_dxt1_gxm_8x8); }
