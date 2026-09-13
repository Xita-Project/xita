/* Copy a shader's register window into its immutable frame snapshot.
 * Source registers are Xbox c[-96..95]. Out-of-range rows retain the existing
 * zero-fill behavior. Source and destination must not overlap. */
#pragma once
#include <stdint.h>
#include <string.h>

static inline void xv_constant_window_copy(float *output, const float source[192][4],
                                            int16_t base, uint16_t count)
{
    int first = (int)base + 96;
    if (!count) return;
    if (first >= 0 && first < 192 && count <= 192u - (unsigned)first) {
        memcpy(output, source[first], (size_t)count * 16u);
        return;
    }
    unsigned prefix = first < 0 ? (unsigned)-first : 0;
    if (prefix > count) prefix = count;
    int valid_first = first + (int)prefix;
    unsigned copied = valid_first >= 0 && valid_first < 192 ? 192u - (unsigned)valid_first : 0;
    if (copied > count - prefix) copied = count - prefix;
    if (prefix) memset(output, 0, (size_t)prefix * 16u);
    if (copied) memcpy(output + prefix * 4u, source[valid_first], (size_t)copied * 16u);
    unsigned tail = count - prefix - copied;
    if (tail) memset(output + (prefix + copied) * 4u, 0, (size_t)tail * 16u);
}
