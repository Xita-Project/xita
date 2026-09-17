#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define XV_LOG(...) ((void)0)
#include "xv_texture_state.h"

_Static_assert(sizeof(SceGxmTexture) == 16, "Test all four SDK control words");
static SceGxmTexture bound[2][SCE_GXM_MAX_TEXTURE_UNITS];
static unsigned calls[2], rejected[2];
static int fail_once;
static SceGxmContext *context(unsigned index) { return (SceGxmContext *)(uintptr_t)(index + 1); }
int sceGxmSetFragmentTexture(SceGxmContext *ctx, unsigned unit, const SceGxmTexture *texture)
{
    unsigned index = (unsigned)(uintptr_t)ctx - 1;
    assert(index < 2);
    calls[index]++;
    uint32_t words[4]; memcpy(words, texture, sizeof words);
    if (fail_once || unit >= SCE_GXM_MAX_TEXTURE_UNITS || words[0] == 0xbad0bad0u) {
        fail_once = 0; rejected[index]++; return -22;
    }
    bound[index][unit] = *texture;
    return 0;
}
static uint32_t rng = 0x47584d31;
static unsigned random_word(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static SceGxmTexture descriptor(void)
{
    uint32_t words[4] = { random_word(), random_word(), random_word(), random_word() };
    SceGxmTexture value; memcpy(&value, words, sizeof value); return value;
}

#include "texture_config.inc"
int main(int argc, char **argv)
{
    (void)argv;
    if (argc > 1) {
        const char *e = getenv("XV_TEXTURE_STATE_CACHE");
        int configured = e ? atoi(e) != 0 : XV_TEXTURE_STATE_CACHE_DEFAULT;
        assert(texture_state_enabled() == configured);
        for (int mode = -2; mode <= 2; mode++) {
            xv_d3d_texture_state_override(mode);
            assert(texture_state_enabled() == (mode < 0 ? configured : !!mode));
        }
        xv_d3d_texture_state_override(-1);
        assert(texture_state_enabled() == configured);
        puts("PASS: configured default, explicit override and restoration");
        return 0;
    }
    xv_texture_state state; memset(&state, 0xcd, sizeof state); state.valid = 0;
    SceGxmTexture value = descriptor(), saved = value;
    assert(xv_texture_state_bind(&state, context(1), 15, &value) == XV_TEXTURE_BOUND);
    memset(&value, 0, sizeof value); /* The cache must own a copy. */
    assert(xv_texture_state_bind(&state, context(1), 15, &saved) == XV_TEXTURE_UNCHANGED);
    for (unsigned bit = 0; bit < 128; bit++) {
        unsigned before = calls[1];
        uint32_t words[4]; memcpy(words, &saved, sizeof words);
        words[bit / 32] ^= 1u << (bit % 32);
        memcpy(&value, words, sizeof value);
        assert(xv_texture_state_bind(&state, context(1), 15, &value) == XV_TEXTURE_BOUND);
        assert(xv_texture_state_bind(&state, context(1), 15, &value) == XV_TEXTURE_UNCHANGED);
        assert(calls[1] == before + 1);
    }
    /* A failed replacement invalidates an older successful entry. */
    saved = value; value = descriptor(); fail_once = 1;
    assert(xv_texture_state_bind(&state, context(1), 15, &value) == XV_TEXTURE_BIND_ERROR);
    assert(xv_texture_state_bind(&state, context(1), 15, &saved) == XV_TEXTURE_BOUND);
    assert(xv_texture_state_bind(&state, context(1), 16, &saved) == XV_TEXTURE_BIND_ERROR);
    assert(xv_texture_state_bind(&state, context(1), UINT32_MAX, &saved) == XV_TEXTURE_BIND_ERROR);
    unsigned before = calls[1];
    assert(xv_texture_state_bind(NULL, context(1), 15, &saved) == XV_TEXTURE_BOUND);
    assert(xv_texture_state_bind(NULL, context(1), 15, &saved) == XV_TEXTURE_BOUND);
    assert(calls[1] == before + 2);

    memset(bound, 0, sizeof bound); memset(calls, 0, sizeof calls);
    memset(rejected, 0, sizeof rejected); state.valid = 0;
    SceGxmTexture pool[32];
    for (unsigned i = 0; i < 32; i++) pool[i] = descriptor();
    unsigned skipped = 0, draws = 0, boundaries = 0;
    for (unsigned n = 0; n < 120000; n++) {
        unsigned event = random_word();
        if (event % 61 == 0) {
            /* Other rendering paths change active state before a new range. */
            unsigned unit = random_word() % SCE_GXM_MAX_TEXTURE_UNITS;
            value = descriptor();
            assert(!sceGxmSetFragmentTexture(context(0), unit, &value));
            assert(!sceGxmSetFragmentTexture(context(1), unit, &value));
            state.valid = 0; boundaries++;
        }
        unsigned unit = (event >> 7) % SCE_GXM_MAX_TEXTURE_UNITS;
        value = event & 1 ? bound[0][unit] : pool[(event >> 19) % 32];
        if (event % 101 == 0) {
            uint32_t bad = 0xbad0bad0u; memcpy(&value, &bad, sizeof bad);
        }
        int original = sceGxmSetFragmentTexture(context(0), unit, &value);
        unsigned result = xv_texture_state_bind(&state, context(1), unit, &value);
        assert((original < 0) == (result == XV_TEXTURE_BIND_ERROR));
        skipped += result == XV_TEXTURE_UNCHANGED;
        /* Compare all actual binding state at each simulated draw. */
        assert(!memcmp(bound[0], bound[1], sizeof bound[0])); draws++;
    }
    assert(calls[0] - calls[1] == skipped && skipped > 20000);
    assert(rejected[0] == rejected[1] && rejected[0] > 1000);
    printf("PASS: %u draw states match, %u rebindings skipped, %u invalidation boundaries; all 128 descriptor bits, copied storage, failures and unit bounds verified\n", draws, skipped, boundaries);
}
