/* Halo 3925 child-node hierarchy preparation. This is a synchronous extraction,
 * not a new worker queue. All guest publication stays inside the caller's scope. */
#ifdef XV_NATIVE_MODEL_HIERARCHY
#include "xk.h"
#include "xk_object_jobs.h"
#if XV_HIERARCHY_SNAPSHOT || XV_HIERARCHY_ASSIST
#include "xk_hierarchy_runtime.h"
#endif
#if XV_HIERARCHY_SNAPSHOT
static unsigned snapshot_batches,snapshot_nodes;
#endif
#include <stdlib.h>
#ifndef XV_HIERARCHY_FINAL_NORMAL
#define XV_HIERARCHY_FINAL_NORMAL 0
#endif
#if XV_HIERARCHY_FINAL_NORMAL != 0 && XV_HIERARCHY_FINAL_NORMAL != 1
#error "XV_HIERARCHY_FINAL_NORMAL must be 0 or 1"
#endif
#ifndef XV_HIERARCHY_MATRIX_NORMAL
#define XV_HIERARCHY_MATRIX_NORMAL 0
#endif
#if XV_HIERARCHY_MATRIX_NORMAL != 0 && XV_HIERARCHY_MATRIX_NORMAL != 1
#error "XV_HIERARCHY_MATRIX_NORMAL must be 0 or 1"
#endif
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

enum { MAX_NODES=64 };
enum { H_BOUNDS, H_LAYOUT, H_LINKS, H_BUDGET, H_NUMERIC, H_FP, H_REASONS };
static unsigned batches, prepared, declined[H_REASONS];
/* Distinguish intentional retained tails from unsupported input. Counts are
 * attempts, not skipped nodes or a timing estimate; guarded like declined[]. */
enum { HB_ENTRY, HB_MODEL, HB_COUNT, HB_TAIL, HB_EMPTY, HB_REASONS };
static unsigned bounds_stages[HB_REASONS];
static unsigned numeric_stages[4],output_computed,output_discarded,output_salvageable;
static unsigned final_normal_batches;
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
static int bounds_decline(unsigned stage)
{
    bounds_stages[stage]++;
    return decline(H_BOUNDS);
}
static int numeric_decline(unsigned stage,unsigned computed)
{
    numeric_stages[stage]++;
    if(stage==3) {
        output_computed+=computed;
        output_discarded+=computed-1u;
        output_salvageable+=computed>2u?computed-2u:0;
    }
    return decline(H_NUMERIC);
}
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
/* Parent and produced matrices may contain tiny normal cancellation terms.
 * Keep subnormals/exceptional values excluded and the pose domain unchanged.
 * A failed output check still restores all speculative FP status. */
static int numeric_matrix(const void *p,unsigned words)
{
#if XV_HIERARCHY_MATRIX_NORMAL
    const uint8_t *data=p;
    for(unsigned i=0;i<words;i++) {
        uint32_t word;memcpy(&word,data+4*i,4);word&=0x7fffffffu;
        if(word&&word-0x00800000u>0x4e800000u-0x00800000u)return 0;
    }
    return 1;
#else
    return numeric(p,words);
#endif
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

/* Independent local transforms form the assistance boundary. A worker may
 * eventually compute a disjoint range from these owned poses. Composition
 * below remains ordered because children consume previously produced parents.
 * This function must never read guest memory or publish shared object state. */
static void hierarchy_locals(unsigned begin,unsigned end,
    const int16_t order[MAX_NODES],const float poses[MAX_NODES][8],
    float locals[MAX_NODES][13])
{
    for(unsigned i=begin;i<end;i++) {
        unsigned n=(unsigned)order[i];
        local_matrix(poses[n],locals[n]);
    }
}

#if XV_HIERARCHY_ASSIST
#if defined(__arm__)
#define H_FP_STATUS 0x9fu
#elif defined(__x86_64__)
#define H_FP_STATUS 0x3fu
#else
#error Hierarchy assistance requires explicit native FP state support
#endif
struct hierarchy_local_task {
    unsigned begin,end,mode,status;
    const int16_t *order;
    const float (*poses)[8];
    float (*locals)[13];
};
static void hierarchy_local_task_run(void *argument)
{
    struct hierarchy_local_task *task=argument;
    unsigned saved=fp_read();
    fp_restore(task->mode&~H_FP_STATUS);
    hierarchy_locals(task->begin,task->end,task->order,task->poses,task->locals);
    task->status=fp_read()&H_FP_STATUS;
    fp_restore(saved);
}
#endif

/* Private arithmetic boundary: no guest pointers, context, shared counters or
 * lock operations. Inputs and completed parent matrices were captured above;
 * each produced parent precedes its children in the validated worklist.
 * Returns the one-based failed work item, leaving publication to the caller.
 * A failure may alter private matrices/FP status, never guest state. */
static unsigned hierarchy_snapshot(xctx *c,int guard,unsigned first,unsigned queued,
    const int16_t order[MAX_NODES],const int16_t parent[MAX_NODES],
    const float poses[MAX_NODES][8],float matrices[MAX_NODES][13])
{
    float locals[MAX_NODES][13];
#if XV_HIERARCHY_ASSIST
    unsigned end=queued-1u,mid=first+(end-first)/2u;
    struct hierarchy_local_task task={mid,end,fp_read(),0,order,poses,locals};
    /* Tiny batches cannot amortize a handoff. Failed admission stays serial. */
    int assist=end-first>=4?xv_object_hierarchy_offer(c,guard,hierarchy_local_task_run,&task):0;
    if(assist) {
        hierarchy_locals(first,mid,order,poses,locals);
        xv_object_hierarchy_join(assist);
        fp_restore(fp_read()|task.status);
    } else hierarchy_locals(first,end,order,poses,locals);
#else
    (void)c;(void)guard;
    hierarchy_locals(first,queued-1u,order,poses,locals);
#endif
    for(unsigned i=first;i<queued-1u;i++) {
        unsigned n=(unsigned)order[i];
        compose(matrices[parent[n]],locals[n],matrices[n]);
        if(!numeric_matrix(matrices[n],13))return i-first+1u;
    }
    return 0;
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
    if(!first||first>=MAX_NODES||sp<32||sp>UINT32_MAX-0x1f8u)return bounds_decline(HB_ENTRY);
    uint32_t *stack=span(sp,0x1f8u);void *scratch=span(sp-32u,32);
    if(!stack||!scratch)return decline(H_LAYOUT);
    uint32_t model=stack[0x2c/4];
    if(model>UINT32_MAX-0xc0u)return bounds_decline(HB_MODEL);
    const uint32_t *header=span(model+0xb8u,8);
    if(!header)return decline(H_LAYOUT);
    unsigned count=header[0],queued=stack[0x10/4];
    if(count<3||count>MAX_NODES||first>=queued||queued>count)return bounds_decline(HB_COUNT);
    /* Skip the final original iteration and short tails of larger models before
     * copying a whole hierarchy. Most calls enter after the original root. */
    if(first+1u>=count||(count-first-1u)*4u<count)return bounds_decline(HB_TAIL);
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
    if(!work)return bounds_decline(HB_EMPTY);
    if(c->preempt<=(int32_t)work)return decline(H_BUDGET);
    const uint32_t *zero=span(0x1f0a68u,4),*one=span(0x1f0a78u,4),*two=span(0x1f0b04u,4);
    if(!zero||!one||!two||*zero||*one!=0x3f800000u||*two!=0x40000000u)return numeric_decline(0,0);
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
        if(!numeric_matrix(matrices[n],13))return numeric_decline(1,0);
    }
    unsigned final_normal=0;
    for(unsigned i=first;i<queued;i++) {
#if XV_HIERARCHY_FINAL_NORMAL
        if(i+1u==queued) {
            /* This pose is processed only by the retained original iteration.
             * Do not broaden the arithmetic domain of any skipped node. */
            for(unsigned j=0;j<8;j++) {
                uint32_t w;memcpy(&w,&local_poses[order[i]][j],4);w&=0x7fffffffu;
                if(w&&(w<0x00800000u||w>0x4e800000u))return numeric_decline(2,0);
                if(w&&w<0x30800000u)final_normal=1;
            }
        } else
#endif
        if(!numeric(local_poses[order[i]],8))return numeric_decline(2,0);
    }
    unsigned saved_fp=fp_read();if(!fp_allowed(saved_fp))return decline(H_FP);
#if XV_HIERARCHY_SNAPSHOT
    int token=xv_object_hierarchy_suspend(c,xv_object_math_locked_,stack[0x24/4],matrix_bytes);
#endif
    int assist_guard=0;
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    assist_guard=xv_object_math_locked_;
#endif
    unsigned failed=hierarchy_snapshot(c,assist_guard,first,queued,order,parent,local_poses,matrices);
#if XV_HIERARCHY_SNAPSHOT
    xv_object_hierarchy_resume(token);
    if(token) {snapshot_batches++;snapshot_nodes+=work;}
#endif
    if(failed) { fp_restore(saved_fp);return numeric_decline(3,failed); }
    /* No callback or guest handoff occurs between these writes. The synchronous
     * guard is held again here, including all declines and shared counters. */
    for(unsigned i=first;i<queued-1u;i++) {
        unsigned n=(unsigned)order[i];memcpy(output[n],matrices[n],52);
    }
    memcpy((uint8_t *)stack+0x178u,order,queued*2u);
    stack[0x10/4]=queued;stack[0x20/4]=queued-1u;c->r[0]=queued-1u;
    c->preempt-=(int32_t)work;batches++;prepared+=work;
    final_normal_batches+=final_normal;
    return 1;
}

void xv_model_hierarchy_report(unsigned frames)
{
#if XV_HIERARCHY_ASSIST
    xv_object_hierarchy_assist_report(frames);
#endif
#if XV_HIERARCHY_SNAPSHOT
    XK_LOG("[hierarchy-snapshot] %u frames private-compute batches %u nodes %u; publication under original guard\n",frames,snapshot_batches,snapshot_nodes);
    snapshot_batches=snapshot_nodes=0;
    xv_object_hierarchy_report(frames);
#endif
    XV_OBJECT_MATH_GUARD();
    XK_LOG("[model-hierarchy] %u frames batches %u child nodes %u; declined bounds %u layout %u links %u budget %u numeric %u fp %u\n",
        frames,batches,prepared,declined[H_BOUNDS],declined[H_LAYOUT],
        declined[H_LINKS],declined[H_BUDGET],declined[H_NUMERIC],declined[H_FP]);
    XK_LOG("[model-hierarchy-bounds] %u frames entry/model/count/tail/empty %u/%u/%u/%u/%u; retry-inclusive attempts, not missed nodes\n",
        frames,bounds_stages[HB_ENTRY],bounds_stages[HB_MODEL],bounds_stages[HB_COUNT],
        bounds_stages[HB_TAIL],bounds_stages[HB_EMPTY]);
    XK_LOG("[model-hierarchy-numeric] %u frames constants/prefix/pose/output %u/%u/%u/%u; output computed %u discarded-success %u salvageable-prefix %u; retry-inclusive attempts\n",
        frames,numeric_stages[0],numeric_stages[1],numeric_stages[2],numeric_stages[3],
        output_computed,output_discarded,output_salvageable);
    XK_LOG("[model-hierarchy-final] %u frames enabled %u recovered-batches %u; final node still original\n",
        frames,XV_HIERARCHY_FINAL_NORMAL,final_normal_batches);
    XK_LOG("[model-hierarchy-matrix] %u frames normal-range enabled %u; pose domain unchanged\n",
        frames,XV_HIERARCHY_MATRIX_NORMAL);
    batches=prepared=0;memset(declined,0,sizeof declined);
    memset(numeric_stages,0,sizeof numeric_stages);
    memset(bounds_stages,0,sizeof bounds_stages);
    output_computed=output_discarded=output_salvageable=final_normal_batches=0;
}
#endif
