#pragma once
#include <stddef.h>
#include <stdint.h>
/* Exactly one 8x8 BC2 level: four row-ordered NV2A blocks to GXM's
 * Y-first swizzled block order. Both spans must be exactly 64 bytes. Aliasing
 * is supported; rejection writes nothing. No decompression or alpha change. */
int h2_dxt23_gxm_8x8(const uint8_t *source, size_t source_bytes,
                       uint8_t *destination, size_t destination_bytes);
