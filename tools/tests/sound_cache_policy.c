#include "xk_sound_cache.h"
#include <assert.h>
#include <stdio.h>

static xv_sound_cache cache;
static const float listener[3] = {1, 2, 3};
static int lookup(unsigned ways, unsigned hash, uintptr_t domain, unsigned epoch,
                  unsigned frame, const float sound[3], uint8_t *hit)
{
    return xv_sound_cache_lookup(&cache, ways, hash, domain, epoch, frame, 6,
                                 listener, sound, 0.09f, 0.01f, hit);
}
int main(void)
{
    uint8_t hit = 99;
    float p[5][3] = {{0,0,0},{10,0,0},{20,0,0},{30,0,0},{40,0,0}};
    for (unsigned ways = 1; ways <= 4; ways *= 2) {
        memset(&cache, 0, sizeof cache);
        assert(!lookup(ways, 7, 1, 2, 100, p[0], &hit) && hit == 99);
        for (unsigned i = 0; i < ways; ++i)
            assert(xv_sound_cache_store(&cache, ways, 7, 1, 2, 100+i,
                                        listener, p[i], (uint8_t)i));
        for (unsigned i = 0; i < ways; ++i) {
            assert(lookup(ways, 7, 1, 2, 104, p[i], &hit) && hit == i);
            assert(!lookup(ways, 7, 2, 2, 104, p[i], &hit));
            assert(!lookup(ways, 7, 1, 3, 104, p[i], &hit));
        }
        assert(xv_sound_cache_store(&cache, ways, 7, 1, 2, 104, listener, p[ways], 1));
        assert(!lookup(ways, 7, 1, 2, 104, p[0], &hit)); /* oldest evicted */
        assert(lookup(ways, 7, 1, 2, 104, p[ways], &hit) && hit == 1);
        for (unsigned i = 1; i < ways; ++i)
            assert(lookup(ways, 7, 1, 2, 104, p[i], &hit) && hit == i);
        hit = 99;
    }
    memset(&cache, 0, sizeof cache);
    assert(xv_sound_cache_store(&cache, 4, 7, 1, 2, UINT32_MAX-2, listener, p[0], 1));
    assert(lookup(4, 7, 1, 2, 2, p[0], &hit) && hit == 1); /* age 5, wrap */
    assert(!lookup(4, 7, 1, 2, 3, p[0], &hit)); /* age 6: hit did not extend it */
    assert(!lookup(4, 7, 1, 2, UINT32_MAX-3, p[0], &hit));
    assert(!lookup(3, 7, 1, 2, 2, p[0], &hit));
    assert(!xv_sound_cache_store(&cache, 0, 7, 1, 2, 0, listener, p[0], 0));
    float near[3] = {0.05f,0,0}, far[3] = {0.2f,0,0}, bad[3] = {NAN,0,0};
    assert(lookup(4, 7, 1, 2, 2, near, &hit));
    assert(!lookup(4, 7, 1, 2, 2, far, &hit));
    assert(!lookup(4, 7, 1, 2, 2, bad, &hit));
    assert(!xv_sound_cache_store(&cache, 4, 7, 1, 2, 2, listener, bad, 1));
    float moved[3] = {2,2,3};
    assert(!xv_sound_cache_lookup(&cache,4,7,1,2,2,6,moved,p[0],0.09f,0.01f,&hit));
    assert(!xv_sound_cache_lookup(&cache,4,7,1,2,2,0,listener,p[0],0.09f,0.01f,&hit));
    memset(&cache, 0, sizeof cache);
    assert(xv_sound_cache_store(&cache,4,UINT32_MAX,1,7,100,listener,p[0],0));
    assert(xv_sound_cache_store(&cache,4,UINT32_MAX,2,7,100,listener,p[0],1));
    assert(xv_sound_cache_store(&cache,4,UINT32_MAX,1,8,100,listener,p[0],2));
    assert(lookup(4,UINT32_MAX,1,7,101,p[0],&hit) && hit == 0);
    assert(lookup(4,UINT32_MAX,2,7,101,p[0],&hit) && hit == 1);
    assert(lookup(4,UINT32_MAX,1,8,101,p[0],&hit) && hit == 2);
    /* Squaring this finite difference underflows, but it is not an exact key. */
    float zero[3] = {0,0,0}, tiny[3] = {0x1p-100f,0,0};
    float negative_zero[3] = {-0.0f,0,0};
    assert(xv_sound_cache_distance2(zero,tiny) == 0);
    memset(&cache,0,sizeof cache);
    assert(xv_sound_cache_store(&cache,4,7,1,2,100,zero,zero,1));
    assert(xv_sound_cache_lookup(&cache,4,7,1,2,101,6,zero,zero,0,0,&hit));
    assert(!xv_sound_cache_lookup(&cache,4,7,1,2,101,6,tiny,zero,0,0,&hit));
    assert(!xv_sound_cache_lookup(&cache,4,7,1,2,101,6,zero,tiny,0,0,&hit));
    assert(!xv_sound_cache_lookup(&cache,4,7,1,2,101,6,negative_zero,zero,0,0,&hit));
    puts("PASS: bounded retention, eviction, view/epoch separation, age wrap, anchors, invalid inputs");
    return 0;
}
