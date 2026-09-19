/* Experimental Halo CE 3925 shader-constant packing prefix.
 * Caller retains the original final iteration and HLE submission. The caller
 * must own the guest model/constant state throughout this bounded operation.
 * Not yet linked into gameplay; see constant-packing-prototype-20260919.md. */
#ifdef XV_NATIVE_CONSTANT_PACK
#include "xv_x86rt.h"
#include <stdint.h>
#include "xk_owner_phase.h"
#include "xk_object_jobs.h"
#include "xk_constant_pack.h"

/* Owner-thread counters; rejected non-owner calls never touch them. */
static unsigned pack_stats[7];
void xv_constant_pack_stats(unsigned out[7])
{
    memcpy(out, pack_stats, sizeof pack_stats);
    memset(pack_stats, 0, sizeof pack_stats);
}
static int pack_decline(unsigned reason) { pack_stats[reason]++; return 0; }
static void *pack_span(uint32_t address, unsigned size)
{
    if (!size || (address & 3u) || (uint64_t)address + size > 0x100000000ull)
        return NULL;
    uintptr_t first = (uintptr_t)X_G(address);
    for (uint64_t page = ((uint64_t)address & ~4095ull) + 4096;
         page < (uint64_t)address + size; page += 4096)
        if ((uintptr_t)X_G((uint32_t)page) != first + page - address) return NULL;
    return (void *)first;
}
static int pack_overlap(const void *a, unsigned an, const void *b, unsigned bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x < y + bn && y < x + an;
}
int xv_constant_pack_prefix(xctx *c)
{
    uint32_t owner_generation = 0;
    if (xv_owner_phase_active(c, XV_OWNER_SCENE, &owner_generation) != 1 ||
        !xv_object_jobs_native_idle()) return 0;
    const unsigned destination = 0x278258u;
    if (c->r[2] != 0) return pack_decline(4);
    void *descriptor = pack_span(c->r[6], 8);
    if (!descriptor) return pack_decline(4);
    unsigned n = (unsigned)(int16_t)X_M16(c->r[6] + 4);
    if (n < 2 || n > 64) return pack_decline(2);
    if (c->preempt < (int)n) return pack_decline(3);
    unsigned src = X_M32(c->r[6]);
    void *input = pack_span(src, 52*n), *output = pack_span(destination, 48*n);
    void *stack = pack_span(c->r[4], 4);
    if (!input || !output || !stack) return pack_decline(4);
    if (
        pack_overlap(input,52*n,output,48*n) ||
        pack_overlap(output,48*n,descriptor,8) ||
        pack_overlap(output,48*n,stack,4) ||
        pack_overlap(output,48*n,c,sizeof *c)) return pack_decline(5);
#ifdef XV_CHECK_GUEST_ADDRESS
    /* Preserve individual diagnostic watch callbacks through the fallback. */
    if (xv_watch_len) return pack_decline(6);
    /* Stamp every written page before the native stores. */
    if (X_GWN(destination, 48*(n-1)) != output) return pack_decline(4);
#endif
    const unsigned char *in = input;
    unsigned char *out = output;
    for (unsigned i = 0; i < n-1; i++) {
        const unsigned char *a = in + 52*i;
        unsigned char *b = out + 48*i;
        float scale_float;
        memcpy(&scale_float, a, 4);
        double scale = (double)scale_float;
        for (unsigned row = 0; row < 3; row++) {
            for (unsigned col = 0; col < 3; col++) {
                float value, result;
                memcpy(&value, a+4*(1+row+col*3), 4);
                result = (float)(scale*(double)value);
                memcpy(b+row*16+col*4, &result, 4);
            }
            memcpy(b+row*16+12, a+40+row*4, 4);
        }
    }
    pack_stats[0]++; pack_stats[1] += n-1;
    c->r[2] = n-1;
    c->preempt -= n-1;
    return 1;
}

#endif /* XV_NATIVE_CONSTANT_PACK */
