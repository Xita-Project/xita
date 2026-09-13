/* Canonical linear host regions for the uncompressed XDK tile subset.
 * No physical DRAM bank swizzle, compression tags or hardware tile readback. */
#pragma once
#include <stdint.h>
typedef struct h2_host_tile { uint32_t address, bytes, pitch, flags; uint8_t enabled; } h2_host_tile;
typedef struct h2_host_tiles { h2_host_tile entries[8]; } h2_host_tiles;
int h2_host_tile_assign(h2_host_tiles *tiles, unsigned index, uint32_t address,
                         uint32_t bytes, uint32_t pitch, uint32_t flags,
                         uint32_t zstart, uint32_t zoffset, uint32_t physical_bytes);
int h2_host_tile_disable(h2_host_tiles *tiles, unsigned index);
/* A RAM lease may be outside regions, or wholly inside one region. A partial
 * intersection is rejected rather than guessing hardware clipping/aliasing. */
int h2_host_tiles_span(const h2_host_tiles *tiles, uint32_t address, uint32_t bytes);
