#ifndef XV_PS_CAPTURE_H
#define XV_PS_CAPTURE_H
#include <stdint.h>

/* Owner-only diagnostic history, not shader identity or rendering state.
 * Retain the original first-512-distinct-hashes logging policy. */
typedef struct {
    uint32_t hashes[1024], occupied[32];
    unsigned count;
} xv_ps_capture;

static inline int xv_ps_capture_insert(xv_ps_capture *seen, uint32_t hash)
{
    if (seen->count >= 512) return 0;
    unsigned slot = (hash * 2654435761u) >> 22;
    /* At most half full: an empty slot always exists. Compare full hashes,
     * including zero, rather than treating a hash bucket as an identity. */
    while (seen->occupied[slot >> 5] & (1u << (slot & 31))) {
        if (seen->hashes[slot] == hash) return 0;
        slot = (slot + 1) & 1023;
    }
    seen->hashes[slot] = hash;
    seen->occupied[slot >> 5] |= 1u << (slot & 31);
    seen->count++;
    return 1;
}
#endif
