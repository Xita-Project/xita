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

int h2_dxt23_gxm_rect(const uint8_t *source, size_t source_bytes,
                       uint8_t *destination, size_t destination_bytes,
                       unsigned width, unsigned height)
{
    if (!width || !height || width > 1024 || height > 1024 ||
        (width & (width - 1)) || (height & (height - 1))) return 0;
    unsigned columns = (width + 3) / 4, rows = (height + 3) / 4;
    size_t bytes = (size_t)columns * rows * 16;
    if (!source || !destination || source_bytes != bytes || destination_bytes != bytes ||
        (uintptr_t)source > UINTPTR_MAX - bytes || (uintptr_t)destination > UINTPTR_MAX - bytes ||
        ((uintptr_t)source < (uintptr_t)destination + bytes &&
         (uintptr_t)destination < (uintptr_t)source + bytes)) return 0;
    for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x) {
        unsigned index = 0, output_bit = 0;
        for (unsigned bit = 0; (1u << bit) < rows || (1u << bit) < columns; ++bit) {
            if ((1u << bit) < rows) index |= ((y >> bit) & 1) << output_bit++;
            if ((1u << bit) < columns) index |= ((x >> bit) & 1) << output_bit++;
        }
        memcpy(destination + index * 16, source + ((size_t)y * columns + x) * 16, 16);
    }
    return 1;
}
