#include "dxt23_layout.h"
#include <string.h>

static int block_layout(const uint8_t *source, size_t source_bytes,
                        uint8_t *destination, size_t destination_bytes, size_t bytes)
{
    if (!source || !destination || source_bytes != bytes || destination_bytes != bytes ||
        (uintptr_t)source > UINTPTR_MAX - (bytes - 1) ||
        (uintptr_t)destination > UINTPTR_MAX - (bytes - 1)) return 0;
    /* Reorder complete compressed blocks only; no texel conversion. */
    uint8_t copied[64];
    size_t block = bytes / 4;
    memcpy(copied, source, bytes);
    memcpy(destination, copied, block);
    memcpy(destination + block, copied + block * 2, block);
    memcpy(destination + block * 2, copied + block, block);
    memcpy(destination + block * 3, copied + block * 3, block);
    return 1;
}

int h2_dxt23_gxm_8x8(const uint8_t *source, size_t source_bytes,
                       uint8_t *destination, size_t destination_bytes)
{ return block_layout(source, source_bytes, destination, destination_bytes, 64); }
int h2_dxt1_gxm_8x8(const uint8_t *source, size_t source_bytes,
                      uint8_t *destination, size_t destination_bytes)
{ return block_layout(source, source_bytes, destination, destination_bytes, 32); }
