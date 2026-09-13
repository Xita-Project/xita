#include "host_tiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    h2_host_tiles t = {0}, before;
    assert(h2_host_tile_assign(&t, 0, 0x20000, 0x8000, 2560, 0, 0, 0, 0x4000000));
    assert(t.entries[0].enabled && t.entries[0].pitch == 2560);
    assert(h2_host_tiles_span(&t, 0x20000, 0x8000));
    assert(h2_host_tiles_span(&t, 0x1FFFC, 4) && h2_host_tiles_span(&t, 0x28000, 4));
    assert(!h2_host_tiles_span(&t, 0x1FFFC, 8) && !h2_host_tiles_span(&t, 0x27FFC, 8));
    assert(!h2_host_tiles_span(&t, 0x1C000, 0x10000));
    assert(h2_host_tile_assign(&t, 1, 0x28000, 0x8000, 4096, 1, 0, 0, 0x4000000));
    assert(!h2_host_tiles_span(&t, 0x20000, 0x10000));
    before = t;
    assert(!h2_host_tile_assign(&t, 2, 0x24000, 0x4000, 2560, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 8, 0x30000, 0x4000, 2560, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20000, 0x8000, 2560, 0x80000001, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20000, 0x8000, 2560, 0, 4, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20000, 0x8000, 2560, 0, 0, 4, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20001, 0x8000, 2560, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20000, 0x8001, 2560, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20000, 0x8000, 2561, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x20000, 0x8000, 0, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0x3FFC000, 0x8000, 2560, 0, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 0, 0xFFFFC000, 0x8000, 2560, 0, 0, 0, UINT32_MAX));
    assert(!h2_host_tile_disable(&t, 8) && !memcmp(&t, &before, sizeof t));
    assert(h2_host_tile_disable(&t, 0) && !t.entries[0].enabled);
    assert(h2_host_tiles_span(&t, 0x1FFFC, 8));
    assert(h2_host_tile_disable(&t, 0));
    assert(!h2_host_tiles_span(&t, UINT32_MAX, 2) && !h2_host_tiles_span(&t, 0, 0));
    assert(h2_host_tile_assign(&t, 2, 0x30000, 0x8000, 2560, 0x84000001, 0, 0, 0x4000000));
    assert(t.entries[2].flags == 0x84000001);
    assert(h2_host_tiles_attachment(&t, 0x30000, 2560, 2560, 1, 0x128));
    assert(!h2_host_tiles_attachment(&t, 0x30000, 2560, 2560, 0, 0x128));
    assert(!h2_host_tiles_attachment(&t, 0x30000, 2560, 2560, 1, 0x118));
    assert(!h2_host_tiles_attachment(&t, 0x30000, 2560, 4096, 1, 0x128));
    before = t;
    assert(!h2_host_tile_assign(&t, 2, 0x30000, 0x8000, 2560, 0x84000000, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 2, 0x30000, 0x8000, 2560, 0x84000003, 0, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 2, 0x30000, 0x8000, 2560, 0x84000001, 4, 0, 0x4000000));
    assert(!h2_host_tile_assign(&t, 2, 0x30000, 0x8000, 2560, 0x84000001, 0, 4, 0x4000000));
    assert(!memcmp(&before, &t, sizeof t));
    puts("Host tile regions: bounds, canonical depth metadata, format/pitch guards and unknown-layout rejection pass.");
    return 0;
}
