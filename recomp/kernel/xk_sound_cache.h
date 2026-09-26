#ifndef XK_SOUND_CACHE_H
#define XK_SOUND_CACHE_H

/* Candidate retention policy for sound-obstruction queries. xv_sound_ray
 * uses it only to verify predictions against real casts. The caller must serialize lookup/store, copy a hit before
 * releasing its lock, and never hold that lock across a guest collision cast.
 * domain identifies the memory view; epoch must change when the world changes.
 * Reuse retains the existing bounded-age/endpoint approximation, not exact
 * collision equivalence. Verify against real casts before enabling a policy. */
#include <stdint.h>
#include <string.h>
#include <math.h>

#define XV_SOUND_CACHE_CAPACITY 1024u
typedef struct {
    float listener[3], sound[3];
    uintptr_t domain;
    uint32_t epoch, frame;
    uint8_t used, hit;
} xv_sound_cache_entry;
typedef struct { xv_sound_cache_entry entry[XV_SOUND_CACHE_CAPACITY]; } xv_sound_cache;

static inline int xv_sound_cache_ways_valid(unsigned ways)
{
    return ways == 1 || ways == 2 || ways == 4;
}
static inline int xv_sound_cache_finite(const float p[3])
{
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]);
}
static inline float xv_sound_cache_distance2(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x*x + y*y + z*z;
}
static inline int xv_sound_cache_probe(const xv_sound_cache *cache,
    unsigned ways, uint32_t hash, uintptr_t domain, uint32_t epoch,
    uint32_t frame, uint32_t lifetime, const float listener[3], const float sound[3],
    float listener_epsilon2, float sound_epsilon2, uint8_t *hit,
    xv_sound_cache_entry *matched)
{
    if (!xv_sound_cache_ways_valid(ways) || !lifetime ||
        !xv_sound_cache_finite(listener) || !xv_sound_cache_finite(sound) ||
        !(listener_epsilon2 >= 0) || !(sound_epsilon2 >= 0)) return 0;
    unsigned base = (hash & (XV_SOUND_CACHE_CAPACITY / ways - 1u)) * ways;
    for (unsigned i = base; i < base + ways; ++i) {
        const xv_sound_cache_entry *e = &cache->entry[i];
        if (e->used && e->domain == domain && e->epoch == epoch &&
            frame - e->frame < lifetime &&
            xv_sound_cache_distance2(listener, e->listener) <= listener_epsilon2 &&
            xv_sound_cache_distance2(sound, e->sound) <= sound_epsilon2) {
            *hit = e->hit;
            if (matched) *matched = *e;
            return 1; /* Do not renew the age or move the endpoint anchor. */
        }
    }
    return 0;
}
static inline int xv_sound_cache_lookup(const xv_sound_cache *cache,
    unsigned ways, uint32_t hash, uintptr_t domain, uint32_t epoch,
    uint32_t frame, uint32_t lifetime, const float listener[3], const float sound[3],
    float listener_epsilon2, float sound_epsilon2, uint8_t *hit)
{
    return xv_sound_cache_probe(cache, ways, hash, domain, epoch, frame, lifetime,
        listener, sound, listener_epsilon2, sound_epsilon2, hit, NULL);
}
static inline int xv_sound_cache_store(xv_sound_cache *cache,
    unsigned ways, uint32_t hash, uintptr_t domain, uint32_t epoch,
    uint32_t frame, const float listener[3], const float sound[3], uint8_t hit)
{
    if (!xv_sound_cache_ways_valid(ways) || !xv_sound_cache_finite(listener) ||
        !xv_sound_cache_finite(sound)) return 0;
    unsigned base = (hash & (XV_SOUND_CACHE_CAPACITY / ways - 1u)) * ways;
    unsigned victim = base;
    uint32_t oldest = 0;
    for (unsigned i = base; i < base + ways; ++i) {
        const xv_sound_cache_entry *e = &cache->entry[i];
        if (!e->used) { victim = i; break; }
        uint32_t age = frame - e->frame;
        if (age >= oldest) { oldest = age; victim = i; }
    }
    xv_sound_cache_entry *e = &cache->entry[victim];
    memcpy(e->listener, listener, sizeof e->listener);
    memcpy(e->sound, sound, sizeof e->sound);
    e->domain = domain; e->epoch = epoch; e->frame = frame;
    e->hit = hit; e->used = 1;
    return 1;
}
#endif
