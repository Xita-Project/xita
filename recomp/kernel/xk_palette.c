/* Experimental Halo 3925 model-palette batch, 0xA2781..0xA27C5.
 * The original loop remains the fallback. No worker ownership is introduced.
 * Keep this out of ordinary builds until hardware comparisons justify it. */
#ifdef XV_NATIVE_MODEL_PALETTE
#include "xk.h"
#include <stdlib.h>
#if defined(XV_PALETTE_JOB_PROFILE) && XV_PALETTE_JOB_PROFILE
#include <stdio.h>
/* Accepted serial batches only. Owner-thread counters; never a worker queue. */
static unsigned palette_sizes[65];
#endif

static unsigned palette_batches, palette_matrices, palette_declined[4];
static int palette_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *batch = getenv("XV_NATIVE_MODEL_PALETTE");
        const char *math = getenv("XV_NATIVE_MATH");
        enabled = batch && atoi(batch) != 0 && (!math || atoi(math) != 0);
    }
    return enabled;
}
static int decline(unsigned reason) { palette_declined[reason]++; return 0; }

/* A whole batch can cross guest pages only if all of them have contiguous,
 * stable host mappings. The guest cannot hand off while this helper runs. */
static void *palette_span(uint32_t address, unsigned size)
{
    if (!size || (address & 3u) || (uint64_t)address + size > 0x100000000ull)
        return NULL;
    uintptr_t first = (uintptr_t)X_G(address);
    for (uint64_t page = ((uint64_t)address & ~4095ull) + 4096;
         page < (uint64_t)address + size; page += 4096) {
        if ((uintptr_t)X_G((uint32_t)page) != first + (page - address)) return NULL;
    }
    return (void *)first;
}
static int overlaps(const void *a, unsigned an, const void *b, unsigned bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x < y + bn && y < x + an;
}

/* Same ordered float products, unused SSE lanes and double scale as the
 * validated native 0xB5B40 helper. Compare both against the original lift;
 * do not replace this with reassociated/FMA matrix multiplication. */
static void product(xctx *c, const float *ap, const float *bp, float *op)
{
    float l[13], r[13], v[8][4];
    memcpy(l, ap, sizeof l); memcpy(r, bp, sizeof r);
    for (unsigned i = 0; i < 3; i++) {
        v[i][0] = l[1+i*3]; v[i][1] = 0;
        v[i][2] = l[2+i*3]; v[i][3] = l[3+i*3];
    }
    for (unsigned row = 0; row < 3; row++) {
        float q[4];
        for (unsigned j = 0; j < 4; j++) {
            float first = r[1+row*3] * v[0][j];
            float second = r[2+row*3] * v[1][j];
            float third = r[3+row*3] * v[2][j];
            q[j] = (first + second) + third;
        }
        op[1+row*3] = q[0]; op[2+row*3] = q[2]; op[3+row*3] = q[3];
        if (row == 2) { v[7][0]=q[3]; v[7][1]=q[3]; v[7][2]=q[0]; v[7][3]=q[2]; }
    }
    v[6][0]=l[10]; v[6][1]=0; v[6][2]=l[11]; v[6][3]=l[12];
    for (unsigned j = 0; j < 4; j++) {
        float first = r[10] * v[0][j], third = r[12] * v[2][j];
        v[4][j] = r[11] * v[1][j];
        v[5][j] = l[0];
        float translation = ((first + v[4][j]) + third) * v[5][j];
        v[3][j] = translation + v[6][j];
    }
    op[10]=v[3][0]; op[11]=v[3][2]; op[12]=v[3][3];
    double scale = (double)l[0] * (double)r[0];
    op[0]=(float)scale;
    if (c) {
        memcpy(c->xmm, v, sizeof v);
        c->st[(c->fsp - 1u) & 7u] = scale;
    }
}

int xv_math_model_palette(xctx *c)
{
    if (!palette_enabled()) return decline(0);
    uint32_t sp = c->r[4], model = c->r[5], pose = c->r[7];
    if (!pose || sp < 32 || model > UINT32_MAX - 0xc0u) return decline(1);
    const uint32_t *header = palette_span(model + 0xb8u, 8);
    if (!header) return decline(1);
    uint32_t count = header[0], nodes = header[1];
    if (!count || count > 64 || nodes > UINT32_MAX - 0x68u || sp > UINT32_MAX - 0xe4u)
        return decline(1);
    /* The original takes count-1 back edges. If any would yield, execute it
     * unchanged so callbacks can observe exactly the original guest state. */
    if (count > 1 && c->preempt < (int32_t)count) return decline(2);
    unsigned output_bytes = count * 52u, node_bytes = (count - 1u) * 156u + 52u;
    const float *left = palette_span(pose, output_bytes);
    const uint8_t *right = palette_span(nodes + 0x68u, node_bytes);
    float *output = palette_span(sp + 0xe4u, output_bytes);
    uint32_t *scratch = palette_span(sp - 32u, 32);
    if (!left || !right || !output || !scratch) return decline(3);
    if (overlaps(output,output_bytes,left,output_bytes) ||
        overlaps(output,output_bytes,right,node_bytes) ||
        overlaps(output,output_bytes,header,8) || overlaps(output,output_bytes,scratch,32) ||
        overlaps(scratch,32,left,output_bytes) || overlaps(scratch,32,right,node_bytes) ||
        overlaps(scratch,32,header,8)) return decline(3);
    /* Guard every range before the first mutation. Inputs may alias each other
     * but neither output nor guest call scratch can alias any input. */
    /* No handoff or other consumer observes intermediate register state.
     * Keep all matrix outputs, then reproduce the final call's context once. */
    uint32_t last = count - 1u;
    for (unsigned i = 0; i < last; i++)
        product(NULL, left + i * 13u, (const float *)(right + i * 156u), output + i * 13u);
    product(c, left + last * 13u, (const float *)(right + last * 156u), output + last * 13u);
    uint32_t a = pose + last * 52u;
    uint32_t b = nodes + 0x68u + last * 156u, out = sp + 0xe4u + last * 52u;
    scratch[0]=a; scratch[1]=out+4; scratch[2]=b+4; scratch[3]=a+4;
    scratch[4]=0xA27B6u; scratch[5]=a; scratch[6]=b; scratch[7]=out;
    c->r[0]=count; c->r[1]=count; c->r[2]=a; c->r[6]=count;
    c->preempt -= (int32_t)last;
    X_FLAGS(XK_SUB, count, count, 0, 32);
    c->f_cf=0; c->f_of=0;
    palette_batches++; palette_matrices += count;
#if defined(XV_PALETTE_JOB_PROFILE) && XV_PALETTE_JOB_PROFILE
    palette_sizes[count]++;
#endif
    return 1;
}

void xv_model_palette_report(unsigned frames)
{
    XK_LOG("[model-palette] %u frames batches %u matrices %u declined disabled %u bounds %u budget %u layout %u\n",
        frames,palette_batches,palette_matrices,palette_declined[0],palette_declined[1],
        palette_declined[2],palette_declined[3]);
#if defined(XV_PALETTE_JOB_PROFILE) && XV_PALETTE_JOB_PROFILE
    /* One bounded log call per reporting interval, not one write per bin/job.
     * Worst case: 64 pairs of two-digit size + ':' + ten-digit count + space. */
    char line[1024];
    _Static_assert(64 * 14 + 80 < sizeof line, "palette size report capacity");
    size_t used = (size_t)snprintf(line, sizeof line,
                                 "[model-palette-sizes] %u frames sizes", frames);
    for (unsigned size = 1; size <= 64; ++size) {
        if (!palette_sizes[size]) continue;
        int written = snprintf(line + used, sizeof line - used,
                               " %u:%u", size, palette_sizes[size]);
        if (written < 0 || (size_t)written >= sizeof line - used) break;
        used += (size_t)written;
    }
    XK_LOG("%s\n", line);
    memset(palette_sizes, 0, sizeof palette_sizes);
#endif
    palette_batches=palette_matrices=0;
    memset(palette_declined,0,sizeof palette_declined);
}
#endif
