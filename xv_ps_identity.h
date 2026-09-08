/* Exact shader identity memoization. Colors remain per draw; no guest pointer
 * is a cache key, since Halo reuses a mutable shader-definition scratch area. */
#pragma once
#include "xv_ps_key.h"

#define XV_PS_IDENTITY_SLOTS 64u
typedef struct {
    uint8_t program[168];
    uint32_t hash, key, valid;
} xv_ps_identity_entry;
typedef struct {
    xv_ps_identity_entry entries[XV_PS_IDENTITY_SLOTS];
    unsigned last, lookups, adjacent_hits, table_hits, misses;
} xv_ps_identity_cache;

static inline void xv_ps_identity_lookup(xv_ps_identity_cache *cache,
    const void *definition, uint32_t *hash, uint32_t *key)
{
    const uint8_t *d=definition;
    uint8_t program[168];
    memcpy(program,d,40);
    memcpy(program+40,d+0x68,68);
    memcpy(program+108,d+0xB4,60);
    cache->lookups++;
    xv_ps_identity_entry *e=&cache->entries[cache->last];
    if (e->valid && !memcmp(e->program,program,sizeof program)) cache->adjacent_hits++;
    else {
        uint32_t h=2166136261u;
        for (unsigned i=0;i<sizeof program;i++) h=(h^program[i])*16777619u;
        cache->last=h&(XV_PS_IDENTITY_SLOTS-1u);
        e=&cache->entries[cache->last];
        if (e->valid && e->hash==h && !memcmp(e->program,program,sizeof program)) cache->table_hits++;
        else {
            memcpy(e->program,program,sizeof program);
            e->hash=h; e->key=xv_ps_program_key(d); e->valid=1; cache->misses++;
        }
    }
    *hash=e->hash; *key=e->key;
}
