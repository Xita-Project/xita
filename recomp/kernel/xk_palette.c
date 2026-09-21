/* Experimental Halo 3925 model-palette batch, 0xA2781..0xA27C5.
 * The original loop remains the fallback. No worker ownership is introduced.
 * Keep this out of ordinary builds until hardware comparisons justify it. */
#ifndef XV_PALETTE_PREFIX_REUSE
#define XV_PALETTE_PREFIX_REUSE 0
#endif
#if XV_PALETTE_PREFIX_REUSE != 0 && XV_PALETTE_PREFIX_REUSE != 1
#error XV_PALETTE_PREFIX_REUSE must be 0 or 1
#endif
#if XV_PALETTE_PREFIX_REUSE && !defined(XV_NATIVE_MODEL_PALETTE)
#error XV_PALETTE_PREFIX_REUSE requires XV_NATIVE_MODEL_PALETTE
#endif
/* non-ARM hosts use the scalar equal_words fallback below */
#ifdef XV_NATIVE_MODEL_PALETTE
#include "xk.h"
#include "xk_object_jobs.h"
#include <stdlib.h>
#if defined(__arm__)
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <xmmintrin.h>
#endif
#if defined(XV_PALETTE_JOB_PROFILE) && XV_PALETTE_JOB_PROFILE
#include <stdio.h>
/* Accepted serial batches only. Owner-thread counters; never a worker queue. */
static unsigned palette_sizes[65];
#endif

static unsigned palette_batches, palette_matrices, palette_declined[5];
/* Guest-owner benchmark override; -1 restores the configured default. */
static int palette_override = -1;
void xv_model_palette_override(int value)
{
    palette_override = value < 0 ? -1 : value != 0;
}
static int palette_enabled(void)
{
    static int enabled = -1, math_allowed;
    if (enabled < 0) {
        const char *batch = getenv("XV_NATIVE_MODEL_PALETTE");
        const char *math = getenv("XV_NATIVE_MATH");
        enabled = batch && atoi(batch) != 0;
        math_allowed = !math || atoi(math) != 0;
    }
    return math_allowed && (palette_override < 0 ? enabled : palette_override);
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
#if XV_POSE_PIPELINE
#include "xk_pose_pipeline.h"
#include "xk_owner_phase.h"
#include "xk_constant_pack.h"
static unsigned pose_scope_depth, pose_scope_datum;
static xctx *pose_scope_context;
unsigned xv_pose_scope_begin(void *context)
{
    xctx *c=context;uint32_t generation=0;
    if(xv_owner_phase_active(c,XV_OWNER_SCENE,&generation)!=1||
       !xv_object_jobs_native_idle())return 0;
    if(pose_scope_depth) {pose_scope_depth++;return 1;}
    const uint32_t *entry=palette_span(c->r[7],12);
    if(!entry||entry[0]==UINT32_MAX)return 0;
    pose_scope_datum=entry[0];pose_scope_context=c;pose_scope_depth=1;return 1;
}
void xv_pose_scope_end(unsigned *token)
{
    if(*token&&pose_scope_depth) {
        if(!--pose_scope_depth)pose_scope_context=NULL;
    }
}
static int pose_scope_active(xctx *c)
{
    static int math_allowed=-1;
    if(math_allowed<0) {const char *m=getenv("XV_NATIVE_MATH");math_allowed=!m||atoi(m)!=0;}
    if(!math_allowed)return 0;
    uint32_t generation=0;
    return pose_scope_depth==1&&pose_scope_context==c&&
        xv_owner_phase_active(c,XV_OWNER_SCENE,&generation)==1&&xv_object_jobs_native_idle();
}
#endif

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

#if XV_POSE_PIPELINE
static void pose_product(const float *a,const float *b,float *out)
{product(NULL,a,b,out);}
#endif

/* The batch can omit intermediate register-only arithmetic. Bound each input
 * to signed zero or magnitude [2^-30, 2^30] before writing anything: useful
 * products stay finite and normal. Unusual cases retain the existing per-call
 * matrix path, including its established NaN operand priority. Integer NEON
 * comparisons classify four input words at a time without changing FPSCR. */
static int palette_numeric(const float *left, const uint8_t *right, unsigned count)
{
#if defined(__arm__)
    unsigned fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr) :: "memory");
    if (fpscr & 0x00009f00u) return 0;
#elif defined(__x86_64__)
    if ((_mm_getcsr() & 0x1f80u) != 0x1f80u) return 0;
#else
    return 0;
#endif
    for (unsigned n=0;n<count;n++) {
        const uint32_t *a=(const uint32_t *)(left+n*13u);
        const uint32_t *b=(const uint32_t *)(right+n*156u);
#if defined(__arm__)
        uint32x4_t bad=vdupq_n_u32(0),zero=vdupq_n_u32(0);
        const uint32x4_t mask=vdupq_n_u32(0x7fffffffu);
        const uint32x4_t low=vdupq_n_u32(0x30800000u);
        const uint32x4_t range=vdupq_n_u32(0x4e800000u-0x30800000u);
        for (unsigned j=0;j<12;j+=4) {
            uint32x4_t av=vandq_u32(vld1q_u32(a+j),mask),bv=vandq_u32(vld1q_u32(b+j),mask);
            bad=vorrq_u32(bad,vbicq_u32(vcgtq_u32(vsubq_u32(av,low),range),vceqq_u32(av,zero)));
            bad=vorrq_u32(bad,vbicq_u32(vcgtq_u32(vsubq_u32(bv,low),range),vceqq_u32(bv,zero)));
        }
        uint32x2_t half=vorr_u32(vget_low_u32(bad),vget_high_u32(bad));
        if (vget_lane_u32(half,0)|vget_lane_u32(half,1)) return 0;
        unsigned j=12;
#else
        for (unsigned j=0;j<13;j++)
#endif
        {
            uint32_t av=a[j]&0x7fffffffu, bv=b[j]&0x7fffffffu;
            if ((av && av-0x30800000u > 0x4e800000u-0x30800000u) ||
                (bv && bv-0x30800000u > 0x4e800000u-0x30800000u)) return 0;
        }
    }
    return 1;
}

#if XV_PALETTE_PREFIX_REUSE
/* Exact arithmetic reuse only. Addresses locate candidates; live contents and
 * FP control prove every hit. The enclosing math guard owns this bounded pool. */
typedef struct { unsigned count, control, raised, model, pose, nodes; float left[64*13], right[64*13], output[64*13]; } prefix_entry;
static prefix_entry entries[32];
static unsigned next_way[8];
static unsigned prefix_evictions;
static const unsigned prefix_storage_bytes=sizeof entries+sizeof next_way;
static unsigned prefix_set(unsigned model,unsigned pose,unsigned nodes) {
    unsigned h=model^(pose*0x9e3779b1u)^(nodes*0x85ebca6bu);
    h^=h>>16;h*=0x7feb352du;h^=h>>15;return h&7u;
}
static prefix_entry *find_prefix(unsigned model,unsigned pose,unsigned nodes) {
    unsigned set=prefix_set(model,pose,nodes),base=set*4u;
    for(unsigned i=0;i<4;i++) {
        prefix_entry *p=&entries[base+i];
        if(p->count && p->model==model && p->pose==pose && p->nodes==nodes)return p;
    }
    for(unsigned i=0;i<4;i++)if(!entries[base+i].count)return &entries[base+i];
    prefix_evictions++;unsigned way=next_way[set];next_way[set]=(way+1u)&3u;
    entries[base+way].count=0;return &entries[base+way];
}
static unsigned prefix_hits, prefix_misses, prefix_hit_matrices, prefix_miss_matrices;
#if !defined(__arm__)
static int equal_words(const void *av,const void *bv,unsigned words)   /* HOST FALLBACK: scalar, same result */
{
    const uint32_t *a=av,*b=bv;unsigned d=0;for(unsigned i=0;i<words;i++)d|=a[i]^b[i];return d==0;
}
#else
static int equal_words(const void *av,const void *bv,unsigned words)
{
    const uint32_t *a=av,*b=bv;uint32x4_t diff=vdupq_n_u32(0);
    unsigned i=0;for(;i+4<=words;i+=4)diff=vorrq_u32(diff,veorq_u32(vld1q_u32(a+i),vld1q_u32(b+i)));
    uint32x2_t halves=vorr_u32(vget_low_u32(diff),vget_high_u32(diff));
    unsigned d=vget_lane_u32(halves,0)|vget_lane_u32(halves,1);
    for(;i<words;i++)d|=a[i]^b[i];return d==0;
}
#endif
#if defined(__arm__)
static unsigned read_fp(void) { unsigned v; __asm__ volatile("vmrs %0, fpscr":"=r"(v)::"memory");return v; }
static void write_fp(unsigned v) { __asm__ volatile("vmsr fpscr, %0"::"r"(v):"memory"); }
#else   /* HOST FALLBACK: no FPSCR on the host build */
static unsigned read_fp(void) { return 0u; }
static void write_fp(unsigned v) { (void)v; }
#endif
#endif

int xv_math_model_palette(xctx *c)
{
    XV_OBJECT_MATH_GUARD();
#if XV_POSE_PIPELINE
    int pose_trial=pose_scope_active(c);
    if (!palette_enabled() && !pose_trial) return decline(0);
#else
    if (!palette_enabled()) return decline(0);
#endif
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
    if (!palette_numeric(left,right,count)) return decline(4);
    uint32_t last = count - 1u;
#if XV_POSE_PIPELINE
    int previous_pose=pose_trial&&xv_pose_pipeline_try(pose_scope_datum,model,pose,nodes,
        count,left,right,156,pose_product,output);
    if(!previous_pose) {
#endif
#if XV_PALETTE_PREFIX_REUSE
    if(last) {
        prefix_entry *prefix=find_prefix(model,pose,nodes);
        unsigned fp=read_fp(), control=fp&~0x9fu;
        int hit=prefix->count==last && prefix->control==control &&
            equal_words(prefix->left,left,last*13u);
        if(hit)for(unsigned i=0;i<last;i++)
            if(!equal_words(prefix->right+i*13u,right+i*156u,13u)){hit=0;break;}
        if(hit) {
            memcpy(output,prefix->output,last*52u);
            write_fp(fp|prefix->raised);prefix_hits++;prefix_hit_matrices+=last;
        } else {
            /* Clear only sticky flags while computing this private prefix so
             * saved arithmetic status is independent of incoming sticky bits. */
            write_fp(fp&~0x9fu);
            for(unsigned i=0;i<last;i++)product(NULL,left+i*13u,(const float *)(right+i*156u),output+i*13u);
            unsigned after=read_fp();prefix->raised=after&0x9fu;
            write_fp(after|(fp&0x9fu));prefix->control=control;
            memcpy(prefix->left,left,last*52u);
            for(unsigned i=0;i<last;i++)memcpy(prefix->right+i*13u,right+i*156u,52u);
            memcpy(prefix->output,output,last*52u);prefix->model=model;prefix->pose=pose;prefix->nodes=nodes;prefix->count=last;prefix_misses++;prefix_miss_matrices+=last;
        }
    }
#else
    for (unsigned i = 0; i < last; i++)
        product(NULL, left + i * 13u, (const float *)(right + i * 156u), output + i * 13u);
#endif
    product(c, left + last * 13u, (const float *)(right + last * 156u), output + last * 13u);
#if XV_POSE_PIPELINE
    } else {
        /* Preserve the current call's register/scratch continuation without
         * mixing its final bone into a previous-frame complete visual pose. */
        float register_only[13];
        product(c,left+last*13u,(const float *)(right+last*156u),register_only);
    }
#endif
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
    XV_OBJECT_MATH_GUARD();
    XK_LOG("[model-palette] %u frames batches %u matrices %u declined disabled %u bounds %u budget %u layout %u numeric/fp %u\n",
        frames,palette_batches,palette_matrices,palette_declined[0],palette_declined[1],
        palette_declined[2],palette_declined[3],palette_declined[4]);
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
#if XV_PALETTE_PREFIX_REUSE
    XK_LOG("[palette-prefix] %u frames hits %u misses %u evictions %u hit/miss matrices %u/%u storage %u; exact contents and FP control, final matrix retained\n",
        frames,prefix_hits,prefix_misses,prefix_evictions,prefix_hit_matrices,prefix_miss_matrices,prefix_storage_bytes);
    prefix_hits=prefix_misses=prefix_evictions=prefix_hit_matrices=prefix_miss_matrices=0;
#endif
    palette_batches=palette_matrices=0;
    memset(palette_declined,0,sizeof palette_declined);
}
#endif
