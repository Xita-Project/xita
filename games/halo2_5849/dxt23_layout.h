#pragma once
#include <stddef.h>
#include <stdint.h>
/* Exactly one 8x8 BC2 level: four row-ordered NV2A blocks to GXM's
 * Y-first swizzled block order. Both spans must be exactly 64 bytes. Aliasing
 * is supported; rejection writes nothing. No decompression or alpha change. */
int h2_dxt23_gxm_8x8(const uint8_t *source, size_t source_bytes,
                       uint8_t *destination, size_t destination_bytes);
/* The corresponding one-level 8x8 BC1 layout uses four 8-byte blocks.
 * Exactly 32 bytes per span; the same alias/rejection contract applies. */
int h2_dxt1_gxm_8x8(const uint8_t *source, size_t source_bytes,
                      uint8_t *destination, size_t destination_bytes);
