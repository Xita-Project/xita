#include "host_tiles.h"
#include <string.h>
static int overlaps(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{ return (uint64_t)a < (uint64_t)b + m && (uint64_t)b < (uint64_t)a + n; }
int h2_host_tile_assign(h2_host_tiles *tiles, unsigned index, uint32_t address,
                         uint32_t bytes, uint32_t pitch, uint32_t flags,
                         uint32_t zstart, uint32_t zoffset, uint32_t physical_bytes)
{
    if (!tiles || index >= 8 || !bytes || ((address | bytes) & 0x3FFF) ||
        address >= physical_bytes || bytes > physical_bytes - address ||
        !pitch || (pitch & 63) || pitch > 0x10000 || pitch > bytes ||
        (flags & ~1u) || zstart || zoffset) return 0;
    for (unsigned i = 0; i < 8; ++i) {
        const h2_host_tile *other = &tiles->entries[i];
        if (i != index && other->enabled && overlaps(address, bytes, other->address, other->bytes)) return 0;
    }
    h2_host_tile value = {address, bytes, pitch, flags, 1};
    tiles->entries[index] = value;
    return 1;
}
int h2_host_tile_disable(h2_host_tiles *tiles, unsigned index)
{
    if (!tiles || index >= 8) return 0;
    memset(&tiles->entries[index], 0, sizeof tiles->entries[index]);
    return 1;
}
int h2_host_tiles_span(const h2_host_tiles *tiles, uint32_t address, uint32_t bytes)
{
    if (!tiles || !bytes || (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    for (unsigned i = 0; i < 8; ++i) {
        const h2_host_tile *tile = &tiles->entries[i];
        if (tile->enabled && overlaps(address, bytes, tile->address, tile->bytes) &&
            (address < tile->address || (uint64_t)address + bytes > (uint64_t)tile->address + tile->bytes)) return 0;
    }
    return 1;
}
