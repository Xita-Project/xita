/* Halo 3925 child-node hierarchy preparation. This is a synchronous extraction,
 * not a new worker queue. All guest publication stays inside the caller's scope. */
#ifdef XV_NATIVE_MODEL_HIERARCHY
#include "xk.h"
#include "xk_object_jobs.h"
#include <stdlib.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

enum { MAX_NODES=64 };
enum { H_BOUNDS, H_LAYOUT, H_LINKS, H_BUDGET, H_NUMERIC, H_FP, H_REASONS };
static unsigned batches, prepared, declined[H_REASONS];
/* One atomic word publishes the immutable environment choice. A disabled
 * experiment must not add a contended mutex to every original child node. */
static int configured=-1, policy=-1;

int xv_model_hierarchy_available(void)
{
    const char *m=getenv("XV_NATIVE_MATH");
    return !m||atoi(m)!=0;
}

void xv_model_hierarchy_override(int value)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    xv_object_math_report_check();
#endif
    __atomic_store_n(&policy,value<0?-1:!!value,__ATOMIC_RELEASE);
}

static int enabled(void)
{
    int config=__atomic_load_n(&configured,__ATOMIC_ACQUIRE);
    if(config<0) {
        const char *e=getenv("XV_NATIVE_MODEL_HIERARCHY"), *m=getenv("XV_NATIVE_MATH");
        config=(!m||atoi(m)!=0)?(e&&atoi(e)!=0?3:2):0;
        __atomic_store_n(&configured,config,__ATOMIC_RELEASE);
    }
    int selected=__atomic_load_n(&policy,__ATOMIC_ACQUIRE);
    return (config&2)&&(selected<0?(config&1):selected);
}
static int decline(unsigned reason) { declined[reason]++;return 0; }
static void *span(uint32_t address,unsigned bytes)
{
    if((address&3u)||!bytes||(uint64_t)address+bytes>0x100000000ull)return NULL;
    uintptr_t first=(uintptr_t)X_G(address);
    for(uint64_t page=((uint64_t)address&~4095ull)+4096;
        page<(uint64_t)address+bytes;page+=4096)
        if((uintptr_t)X_G((uint32_t)page)!=first+(page-address))return NULL;
    return (void *)first;
}
static int overlap(const void *a,unsigned an,const void *b,unsigned bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+bn&&y<x+an;
}
/* Classification is integer-only. Exceptional or very small/large inputs keep
 * the original path; speculative arithmetic may never leak status on decline. */
static int numeric(const void *p,unsigned words)
{
    const uint8_t *data=p;
    for(unsigned i=0;i<words;i++) {
        uint32_t word;memcpy(&word,data+4*i,4);word&=0x7fffffffu;
        if(word&&word-0x30800000u>0x4e800000u-0x30800000u)return 0;
    }
    return 1;
}
static unsigned fp_read(void)
{
#if defined(__arm__)
    unsigned value;__asm__ volatile("vmrs %0, fpscr":"=r"(value)::"memory");return value;
#elif defined(__x86_64__)
    return _mm_getcsr();
#else
    return 0;
#endif
}
static void fp_restore(unsigned value)
{
#if defined(__arm__)
    __asm__ volatile("vmsr fpscr, %0"::"r"(value):"memory");
#elif defined(__x86_64__)
    _mm_setcsr(value);
#else
    (void)value;
#endif
}
static int fp_allowed(unsigned value)
{
#if defined(__arm__)
    return !(value&0x00009f00u);
#elif defined(__x86_64__)
    return (value&0x1f80u)==0x1f80u;
#else
    (void)value;return 0;
#endif
}

/* Same double intermediates and six float spills as the existing quaternion
 * leaf. Constants are admitted only at their canonical 0/1/2 values. No shared
 * counters, guest pointers or context are accessed by either arithmetic kernel. */
static void local_matrix(const float pose[8],float out[13])
{
    double x=pose[0],y=pose[1],z=pose[2],w=pose[3];
    double norm=((x*x+y*y)+z*z)+w*w;
    double scale=norm!=0.0?2.0/norm:0.0;
    double tx=scale*x;
    float ty=(float)(scale*y),tz=(float)(scale*z);
    float xw=(float)(tx*w),yw=(float)((double)ty*w),zw=(float)((double)tz*w);
    double xx=tx*x,xy=(double)ty*x,xz=(double)tz*x;
    float yy=(float)((double)ty*y),yz=(float)((double)tz*y);
    double zz=(double)tz*z;
    out[1]=(float)(1.0-((double)yy+zz));
    out[2]=(float)(xy-(double)zw);out[3]=(float)(xz+(double)yw);
    out[4]=(float)(xy+(double)zw);out[5]=(float)(1.0-(zz+xx));
    out[6]=(float)((double)yz-(double)xw);out[7]=(float)(xz-(double)yw);
    out[8]=(float)((double)yz+(double)xw);out[9]=(float)(1.0-((double)yy+xx));
    memcpy(out,pose+7,4);memcpy(out+10,pose+4,12);
}
static void compose(const float left[13],const float right[13],float out[13])
{
    for(unsigned row=0;row<3;row++)for(unsigned j=0;j<3;j++) {
        float first=right[1+row*3]*left[1+j];
        float second=right[2+row*3]*left[4+j];
        float third=right[3+row*3]*left[7+j];
        out[1+row*3+j]=(first+second)+third;
    }
    for(unsigned j=0;j<3;j++) {
        float first=right[10]*left[1+j],second=right[11]*left[4+j],third=right[12]*left[7+j];
        float sum=((first+second)+third)*left[0];
        out[10+j]=sum+left[10+j];
    }
    out[0]=(float)((double)left[0]*(double)right[0]);
}

int xv_math_model_hierarchy(xctx *c)
{
    /* Root-only models are common. This thread-local register check can return
     * before configuration or synchronization; root handling stays original. */
    if(!c->r[0])return 0;
    if(!enabled())return 0;
    XV_OBJECT_MATH_GUARD();
    uint32_t sp=c->r[4];
    /* The first iteration prepares the root using the original branches. */
    unsigned first=c->r[0];
    if(!first||first>=MAX_NODES||sp<32||sp>UINT32_MAX-0x1f8u)return decline(H_BOUNDS);
    uint32_t *stack=span(sp,0x1f8u);void *scratch=span(sp-32u,32);
    if(!stack||!scratch)return decline(H_LAYOUT);
    uint32_t model=stack[0x2c/4];
    if(model>UINT32_MAX-0xc0u)return decline(H_BOUNDS);
    const uint32_t *header=span(model+0xb8u,8);
    if(!header)return decline(H_LAYOUT);
    unsigned count=header[0],queued=stack[0x10/4];
    if(count<3||count>MAX_NODES||first>=queued||queued>count)return decline(H_BOUNDS);
    /* Skip the final original iteration and short tails of larger models before
     * copying a whole hierarchy. Most calls enter after the original root. */
    if(first+1u>=count||(count-first-1u)*4u<count)return decline(H_BOUNDS);
    unsigned pose_bytes=count*32u,node_bytes=count*156u,matrix_bytes=count*52u;
    const float (*poses)[8]=span(stack[0x28/4],pose_bytes);
    const uint8_t *nodes=span(header[1],node_bytes);
    float (*output)[13]=span(stack[0x24/4],matrix_bytes);
    if(!poses||!nodes||!output)return decline(H_LAYOUT);
    if(overlap(output,matrix_bytes,poses,pose_bytes)||overlap(output,matrix_bytes,nodes,node_bytes)||
       overlap(output,matrix_bytes,header,8)||overlap(output,matrix_bytes,stack,0x1f8u)||
       overlap(output,matrix_bytes,scratch,32)||overlap(stack,0x1f8u,poses,pose_bytes)||
       overlap(stack,0x1f8u,nodes,node_bytes)||overlap(stack,0x1f8u,header,8)||
       overlap(scratch,32,poses,pose_bytes)||overlap(scratch,32,nodes,node_bytes)||
       overlap(scratch,32,header,8))return decline(H_LAYOUT);

    int16_t order[MAX_NODES],parent[MAX_NODES];
    memcpy(order,(const uint8_t *)stack+0x178u,queued*2u);
    uint64_t seen=0,done=0;
    for(unsigned i=0;i<queued;i++) {
        int n=order[i];if(n<0||(unsigned)n>=count||(seen&(1ull<<n)))return decline(H_LINKS);
        seen|=1ull<<n;if(i<first)done|=1ull<<n;
    }
    if(order[0]!=0)return decline(H_LINKS);
    /* Complete the worklist before any arithmetic or publication. This checks
     * cycles, duplicate visits, parent availability and every appended link. */
    for(unsigned at=first;at<queued;at++) {
        unsigned n=(unsigned)order[at];
        int16_t links[3];memcpy(links,nodes+n*156u+0x20u,6);
        int p=links[2];
        if(!n||p<0||(unsigned)p>=count||!(done&(1ull<<p)))return decline(H_LINKS);
        parent[n]=(int16_t)p;done|=1ull<<n;
        for(unsigned j=0;j<2;j++)if(links[j]!=-1) {
            int link=links[j];
            if(link<0||(unsigned)link>=count||queued>=count||(seen&(1ull<<link)))return decline(H_LINKS);
            seen|=1ull<<link;order[queued++]=(int16_t)link;
        }
    }
    /* Retain the final original iteration: it overwrites live register/FP
     * scratch and the shared call-stack footprint of all skipped iterations. */
    unsigned work=queued-first-1u;
    if(!work)return decline(H_BOUNDS);
    if(c->preempt<=(int32_t)work)return decline(H_BUDGET);
    const uint32_t *zero=span(0x1f0a68u,4),*one=span(0x1f0a78u,4),*two=span(0x1f0b04u,4);
    if(!zero||!one||!two||*zero||*one!=0x3f800000u||*two!=0x40000000u)return decline(H_NUMERIC);
    /* Constants must remain unchanged by publication and the final leaf. */
    const void *constants[]={zero,one,two};
    for(unsigned i=0;i<3;i++)if(overlap(output,matrix_bytes,constants[i],4)||
        overlap(stack,0x1f8u,constants[i],4)||overlap(scratch,32,constants[i],4))return decline(H_LAYOUT);
    float local_poses[MAX_NODES][8],matrices[MAX_NODES][13];
    memcpy(local_poses,poses,pose_bytes);
    /* Only the completed prefix supplies old parent matrices. The validated
     * worklist above requires every later parent to precede its child, so the
     * remaining matrices are produced below before their first read. Avoid
     * copying outputs that this batch will overwrite, under the same guard. */
    for(unsigned i=0;i<first;i++) {
        unsigned n=(unsigned)order[i];
        memcpy(matrices[n],output[n],sizeof matrices[n]);
        if(!numeric(matrices[n],13))return decline(H_NUMERIC);
    }
    for(unsigned i=first;i<queued;i++)if(!numeric(local_poses[order[i]],8))return decline(H_NUMERIC);
    unsigned saved_fp=fp_read();if(!fp_allowed(saved_fp))return decline(H_FP);
    for(unsigned i=first;i<queued-1u;i++) {
        unsigned n=(unsigned)order[i];float local[13];
        local_matrix(local_poses[n],local);
        compose(matrices[parent[n]],local,matrices[n]);
        if(!numeric(matrices[n],13)) { fp_restore(saved_fp);return decline(H_NUMERIC); }
    }
    /* No callback or guest handoff occurs between these writes. The synchronous
     * extraction retains the existing shared guard; no ownership bypass. */
    for(unsigned i=first;i<queued-1u;i++) {
        unsigned n=(unsigned)order[i];memcpy(output[n],matrices[n],52);
    }
    memcpy((uint8_t *)stack+0x178u,order,queued*2u);
    stack[0x10/4]=queued;stack[0x20/4]=queued-1u;c->r[0]=queued-1u;
    c->preempt-=(int32_t)work;batches++;prepared+=work;
    return 1;
}

void xv_model_hierarchy_report(unsigned frames)
{
    XV_OBJECT_MATH_GUARD();
    XK_LOG("[model-hierarchy] %u frames batches %u child nodes %u; declined bounds %u layout %u links %u budget %u numeric %u fp %u\n",
        frames,batches,prepared,declined[H_BOUNDS],declined[H_LAYOUT],
        declined[H_LINKS],declined[H_BUDGET],declined[H_NUMERIC],declined[H_FP]);
    batches=prepared=0;memset(declined,0,sizeof declined);
}
#endif
