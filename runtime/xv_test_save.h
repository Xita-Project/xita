/* Developer test saves are opt-in and never fall back to the player profile. */
#pragma once
#include <stddef.h>
#include <stdio.h>
static inline int xv_test_save_path(const char *slot, char *out, size_t size)
{
    if (!slot || !*slot) return 0;
    size_t n = 0;
    for (; slot[n]; ++n) {
        unsigned char c = (unsigned char)slot[n];
        if (n >= 32 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_')) return -1;
    }
    int written = snprintf(out, size, "ux0:data/xita/test-saves/%s", slot);
    return written >= 0 && (size_t)written < size ? 1 : -1;
}
