/* Successful depth-only link proofs, published by the pump and read by the
 * recorder. Keys include layout slot, canonical pixel entry and blend variant.
 * Entries may collide: replacement causes a miss, never a partial proof. Reset
 * only after both threads and their queued frames have drained at shutdown. */
#pragma once
#include <stdint.h>

#define XV_DEPTH_PROOF_SLOTS 256u
typedef struct { uint32_t keys[XV_DEPTH_PROOF_SLOTS]; } xv_depth_proofs;

static inline uint32_t xv_depth_proof_key(unsigned vs, int entry, unsigned blend)
{
    if (vs >= 256 || entry < 0 || entry >= 65536 || blend >= 32) return 0;
    return 1u + (vs << 21) + ((unsigned)entry << 5) + blend;
}
static inline unsigned xv_depth_proof_slot(uint32_t key)
{
    return ((key ^ (key >> 11) ^ (key >> 21)) * 2654435761u) & (XV_DEPTH_PROOF_SLOTS - 1u);
}
static inline int xv_depth_proof_read(const xv_depth_proofs *proofs, uint32_t key)
{
    return key && __atomic_load_n(&proofs->keys[xv_depth_proof_slot(key)], __ATOMIC_ACQUIRE) == key;
}
static inline void xv_depth_proof_publish(xv_depth_proofs *proofs, uint32_t key)
{
    if (key) {
        uint32_t *slot = &proofs->keys[xv_depth_proof_slot(key)];
        /* Proofs describe immutable links until drained shutdown. Avoid a
         * repeated release barrier for every depth draw in ordinary/off arms. */
        if (__atomic_load_n(slot, __ATOMIC_RELAXED) != key)
            __atomic_store_n(slot, key, __ATOMIC_RELEASE);
    }
}
