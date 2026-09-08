/* Program identity excludes colors and inactive combiner stages. Keep the raw
 * capture hash separately: logs and the small semantic HUD router use it. */
#pragma once
#include <stdint.h>
#include <string.h>

static inline uint32_t xv_ps_program_key(const void *definition)
{
    uint8_t d[240];
    memcpy(d, definition, sizeof d);
    unsigned n = d[0xd4] < 8 ? d[0xd4] : 8;
    const unsigned bases[] = {0x00, 0x68, 0x88, 0xb4};
    for (unsigned i = 0; i < 4; ++i)
        memset(d + bases[i] + 4 * n, 0, 4 * (8 - n));
    for (unsigned off = 0xe4; off <= 0xe8; off += 4) {
        uint32_t word;
        memcpy(&word, d + off, 4);
        if (n < 8) word &= (1u << (4 * n)) - 1u;
        memcpy(d + off, &word, 4);
    }
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < sizeof d; ++i) {
        if ((i >= 0x28 && i < 0x68) || (i >= 0xac && i < 0xb4)) continue;
        h = (h ^ d[i]) * 16777619u;
    }
    return h;
}
