/* Diagnostic only: completed/partial outer elapsed on the presenting owner.
 * Includes blocking, callbacks, scheduling and nested work, never CPU self time.
 * Generated call sites are only FA920 and BCB30; no guest state is modified. */
#include "xk.h"
#include "xk_owner_phase.h"
#include "xk_object_jobs.h"
#include "xk_model_uv.h"
#if XV_MODEL_UV_CROSS_MODEL
#define MODEL_UV_BOUNDARY(context) xk_model_uv_owner_boundary(context)
#else
#define MODEL_UV_BOUNDARY(context) ((void)0)
#endif
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
typedef SceUID phase_thread;
static phase_thread current_thread(void) { return sceKernelGetThreadId(); }
#else
#include <pthread.h>
typedef pthread_t phase_thread;
static phase_thread current_thread(void) { return pthread_self(); }
#endif
#ifndef XV_OWNER_PHASE_DEFAULT
#define XV_OWNER_PHASE_DEFAULT 0
#endif
#if XV_OWNER_PHASE_DEFAULT != 0 && XV_OWNER_PHASE_DEFAULT != 1
#error XV_OWNER_PHASE_DEFAULT must be 0 or 1
#endif
extern int xv_object_is_worker_thread(void) __attribute__((weak));
int xv_owner_phase_enabled;
static int configured;
static uintptr_t owner_context;
static phase_thread owner_thread;
static unsigned owner_valid, generation, generation_exhausted;
static struct { unsigned depth; uint64_t start, entries, completed, recursive, elapsed; } phases[XV_OWNER_PHASES];
static uint64_t clock_reads;
static unsigned warmup, foreign, invalid, rebinds, abandoned, stale;

#ifndef XV_SCENE_PARTITION
#define XV_SCENE_PARTITION 0
#endif
#if XV_SCENE_PARTITION != 0 && XV_SCENE_PARTITION != 1
#error XV_SCENE_PARTITION must be 0 or 1
#endif
#if XV_SCENE_PARTITION && !defined(XV_OWNER_PHASE)
#error XV_SCENE_PARTITION requires XV_OWNER_PHASE
#endif
#ifndef XV_SCENE_BUCKET0_DETAIL
#define XV_SCENE_BUCKET0_DETAIL 0
#endif
#if XV_SCENE_BUCKET0_DETAIL != 0 && XV_SCENE_BUCKET0_DETAIL != 1
#error XV_SCENE_BUCKET0_DETAIL must be 0 or 1
#endif
#if XV_SCENE_BUCKET0_DETAIL && !XV_SCENE_PARTITION
#error XV_SCENE_BUCKET0_DETAIL requires XV_SCENE_PARTITION
#endif
#ifndef XV_SCENE_BUCKET1_DETAIL
#define XV_SCENE_BUCKET1_DETAIL 0
#endif
#if XV_SCENE_BUCKET1_DETAIL != 0 && XV_SCENE_BUCKET1_DETAIL != 1
#error XV_SCENE_BUCKET1_DETAIL must be 0 or 1
#endif
#if XV_SCENE_BUCKET1_DETAIL && !XV_SCENE_PARTITION
#error XV_SCENE_BUCKET1_DETAIL requires XV_SCENE_PARTITION
#endif
#if XV_SCENE_PARTITION
/* One outer primary 5D410 invocation on the presenting owner. Nested primary
 * entries are explicitly declined: their elapsed stays in the outer bucket.
 * No pointer into a guest/native stack is retained here. */
static struct {
    uint64_t token, start, elapsed[6], entries[6], completed;
    unsigned bucket, serial, exhausted, recursive, invalid, abandoned, stale;
} scene;
#if XV_SCENE_BUCKET0_DETAIL
/* Shares the enclosing scene token and timestamp; no independent lifetime. */
static struct { uint64_t elapsed[6], entries[6], completed; unsigned bucket; } detail;
/* Primary 5B760 only, nested in bucket0/detail3. Shared timestamps avoid
 * counting the same elapsed interval twice when reports split a live scope. */
static struct { uint64_t token, elapsed[4], completed; unsigned bucket, invalid, child; uint64_t child_elapsed[4], child_calls[4], child_draw[4]; unsigned draw_invalid; } model_detail;

#endif
#if XV_SCENE_BUCKET1_DETAIL
static struct { uint64_t elapsed[12], entries[12], completed; unsigned bucket; } detail1;
extern uint64_t xv_scene_draw_completed_us __attribute__((weak));
static struct { uint64_t start, elapsed[12]; unsigned invalid; } draw_detail1;
static uint64_t draw_completed(void)
{
    return &xv_scene_draw_completed_us ? xv_scene_draw_completed_us : 0;
}

#endif
static void scene_account(uint64_t end)
{
#if XV_SCENE_BUCKET1_DETAIL
    uint64_t draw_end=draw_completed();
    if(scene.bucket==1) {
        /* Counter regressions and impossible deltas are reported, not silently
         * clamped into a claim about CPU self time or removable waits. */
        if(end<scene.start || draw_end<draw_detail1.start ||
           draw_end-draw_detail1.start>end-scene.start)draw_detail1.invalid++;
        else draw_detail1.elapsed[detail1.bucket]+=draw_end-draw_detail1.start;
    }
#if XV_SCENE_BUCKET0_DETAIL
    if(model_detail.token && model_detail.bucket==2) {
        if(end<scene.start || draw_end<draw_detail1.start || draw_end-draw_detail1.start>end-scene.start)
            model_detail.draw_invalid++;
        else model_detail.child_draw[model_detail.child]+=draw_end-draw_detail1.start;
    }
#endif
    draw_detail1.start=draw_end;
#endif
    if(end<scene.start)scene.invalid++;
    else {
        scene.elapsed[scene.bucket]+=end-scene.start;
#if XV_SCENE_BUCKET0_DETAIL
        if(scene.bucket==0) {
            detail.elapsed[detail.bucket]+=end-scene.start;
            if(model_detail.token) {
                model_detail.elapsed[model_detail.bucket]+=end-scene.start;
                if(model_detail.bucket==2)model_detail.child_elapsed[model_detail.child]+=end-scene.start;
            }
        }
#endif
#if XV_SCENE_BUCKET1_DETAIL
        if(scene.bucket==1)detail1.elapsed[detail1.bucket]+=end-scene.start;
#endif
    }
    scene.start=end;
}
#endif

static int worker(void)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if(!xv_object_is_worker_thread)return 1; /* missing admission API fails closed */
#endif
    return xv_object_is_worker_thread && xv_object_is_worker_thread();
}
static int same_thread(void)
{
    if(!__atomic_load_n(&owner_valid,__ATOMIC_ACQUIRE))return 0;
    phase_thread expected=__atomic_load_n(&owner_thread,__ATOMIC_ACQUIRE);
#ifdef __vita__
    return current_thread()==expected;
#else
    return pthread_equal(current_thread(),expected);
#endif
}
static int marked(const xctx *c)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    return xv_is_object_job(c);
#else
    (void)c;return 0;
#endif
}
static int live(void *context)
{
    xctx *c=context;
    return c && xk_cur && c==&xk_cur->ctx && !marked(c) &&
        xk_cur->fiber && xk_os_fiber_current()==xk_cur->fiber && xk_cur->state==0;
}
static uint64_t now(void) { clock_reads++;return xk_os_monotonic_us(); }
static void account(unsigned phase,uint64_t end)
{
    if(end<phases[phase].start)__atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);
    else phases[phase].elapsed+=end-phases[phase].start;
    phases[phase].start=end;
}
void xv_owner_phase_configure(void)
{
    if(configured)return;
    const char *e=getenv("XV_OWNER_PHASE");
    xv_owner_phase_enabled=e?atoi(e)!=0:XV_OWNER_PHASE_DEFAULT;
    configured=1;
    XK_LOG("[owner-phase] process-start %d; FA920/BCB30 outer elapsed only; first Present binds owner, no phase/worker control\n",xv_owner_phase_enabled);
#if XV_SCENE_PARTITION
    XK_LOG("[scene-partition] process-start %d; primary 5D410 six ordered elapsed buckets; workers unchanged\n",xv_owner_phase_enabled);
#endif
#if XV_SCENE_BUCKET0_DETAIL
    XK_LOG("[scene-bucket0-detail] process-start %d; five additional shared-clock boundaries, no worker-policy change\n",xv_owner_phase_enabled);
#endif
}
void xv_owner_phase_present(void *context)
{
    if(!xv_owner_phase_enabled)return;
    /* Called only at real guest Present/Swap. Worker rejection precedes any
     * owner-only scheduler access; these HLEs already require guest ownership. */
    if(worker()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(!live(context)) { __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return; }
    if(generation_exhausted)return;
    if(same_thread() && __atomic_load_n(&owner_context,__ATOMIC_ACQUIRE)==(uintptr_t)context){MODEL_UV_BOUNDARY(context);return;}
    /* A different presenting fiber starts a new observation generation. A
     * suspended old scope is explicitly abandoned, never dereferenced/reused. */
    __atomic_store_n(&owner_valid,0,__ATOMIC_RELEASE);
#if XV_SCENE_PARTITION
    if(scene.token)scene.abandoned++;
#if XV_SCENE_BUCKET0_DETAIL
    if(model_detail.token)model_detail.invalid++;
    model_detail.token=0;
#endif
    scene.token=0;
#endif
    for(unsigned i=0;i<XV_OWNER_PHASES;i++) {
        if(phases[i].depth)abandoned++;
        phases[i].depth=0;
    }
    /* Never let an ancient suspended token become valid again after wrap. */
    if(generation==UINT32_MAX) {
        generation_exhausted=1;
        XK_LOG("[owner-phase] owner generation exhausted; further scopes declined\n");
        return;
    }
    generation++;
    rebinds++;
    __atomic_store_n(&owner_context,(uintptr_t)context,__ATOMIC_RELEASE);
    __atomic_store_n(&owner_thread,current_thread(),__ATOMIC_RELEASE);
    __atomic_store_n(&owner_valid,1,__ATOMIC_RELEASE);
    MODEL_UV_BOUNDARY(context);
}
int xv_owner_phase_active(void *context,unsigned phase,uint32_t *generation_token)
{
    if(!xv_owner_phase_enabled || !generation_token || worker() || !same_thread())return -1;
    if(__atomic_load_n(&owner_context,__ATOMIC_ACQUIRE)!=(uintptr_t)context ||
       !live(context) || phase>=XV_OWNER_PHASES || generation_exhausted || !generation ||
       (*generation_token && *generation_token!=generation))return -1;
    *generation_token=generation;
    return !!phases[phase].depth;
}
#if XV_SCENE_PARTITION
static int scene_live(void *context)
{
    uint32_t token=0;
    return xv_owner_phase_active(context,XV_OWNER_SCENE,&token)>=0;
}
void xv_scene_partition_begin(uint64_t *scope,void *context)
{
    if(!xv_owner_phase_enabled)return;
    if(!scene_live(context)) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(scene.token) { scene.recursive++;return; }
    if(scene.exhausted)return;
    if(scene.serial==UINT32_MAX) { scene.exhausted=1;return; }
    *scope=scene.token=((uint64_t)generation<<32)|++scene.serial;
    scene.bucket=0;scene.entries[0]++;scene.start=now();
#if XV_SCENE_BUCKET1_DETAIL
    draw_detail1.start=draw_completed();
#endif
#if XV_SCENE_BUCKET0_DETAIL
    detail.bucket=0;detail.entries[0]++;
#endif
}
void xv_scene_partition_step(uint64_t *scope,void *context,unsigned bucket)
{
    if(!*scope)return;
    if(!scene_live(context)) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(*scope!=scene.token || (unsigned)(*scope>>32)!=generation) { scene.stale++;return; }
    if(bucket>=6 || bucket<=scene.bucket) { scene.invalid++;return; }
    /* Exactly one clock accounts the old interval and starts the next. */
    scene_account(now());
#if XV_SCENE_BUCKET0_DETAIL
    if(scene.bucket==0)detail.completed++;
#endif
#if XV_SCENE_BUCKET1_DETAIL
    if(scene.bucket==1)detail1.completed++;
#endif
    scene.bucket=bucket;scene.entries[bucket]++;
#if XV_SCENE_BUCKET1_DETAIL
    if(bucket==1) { detail1.bucket=0;detail1.entries[0]++; }
#endif
}
#if XV_SCENE_BUCKET0_DETAIL
void xv_scene_model_begin(uint64_t *scope,void *context)
{
    if(!xv_owner_phase_enabled || !scene_live(context))return;
    if(!scene.token || scene.bucket!=0 || detail.bucket!=3 || model_detail.token)return;
    scene_account(now());
    model_detail.bucket=0;model_detail.child=0;model_detail.token=scene.token;*scope=scene.token;
}
void xv_scene_model_child_begin(uint64_t *scope,void *context,unsigned child)
{
    if(!xv_owner_phase_enabled || !scene_live(context))return;
    if(!model_detail.token || model_detail.token!=scene.token || scene.bucket!=0 ||
       detail.bucket!=3 || model_detail.bucket!=2 || model_detail.child || !child || child>=4)return;
    scene_account(now());model_detail.child=child;model_detail.child_calls[child]++;
    *scope=model_detail.token;
}
void xv_scene_model_child_end(uint64_t *scope)
{
    uint64_t token=*scope;*scope=0;
    if(!token || worker() || !same_thread())return;
    if(token!=scene.token || token!=model_detail.token || !model_detail.child) {
        model_detail.invalid++;return;
    }
    if(!scene_live((void *)owner_context) || scene.bucket!=0 || detail.bucket!=3 || model_detail.bucket!=2) {
        model_detail.invalid++;model_detail.child=0;return;
    }
    scene_account(now());model_detail.child=0;
}
void xv_scene_model_step(uint64_t *scope,void *context,unsigned bucket)
{
    if(!*scope || !scene_live(context))return;
    if(*scope!=scene.token || *scope!=model_detail.token ||
       scene.bucket!=0 || detail.bucket!=3 || bucket>=4 || bucket<=model_detail.bucket) {
        model_detail.invalid++;return;
    }
    scene_account(now());model_detail.bucket=bucket;
}
void xv_scene_model_end(uint64_t *scope)
{
    uint64_t token=*scope;*scope=0;
    if(!token || worker() || !same_thread())return;
    if(token!=scene.token || token!=model_detail.token) { model_detail.invalid++;return; }
    if(!scene_live((void *)owner_context) || scene.bucket!=0 || detail.bucket!=3) {
        model_detail.invalid++;model_detail.token=0;return;
    }
    scene_account(now());model_detail.completed++;model_detail.token=0;
}
void xv_scene_bucket0_step(uint64_t *scope,void *context,unsigned bucket)
{
    if(!*scope)return;
    if(!scene_live(context)) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(*scope!=scene.token || (unsigned)(*scope>>32)!=generation) { scene.stale++;return; }
    if(scene.bucket!=0 || bucket>=6 || bucket<=detail.bucket) { scene.invalid++;return; }
    scene_account(now());detail.bucket=bucket;detail.entries[bucket]++;
}
#endif
#if XV_SCENE_BUCKET1_DETAIL
void xv_scene_bucket1_step(uint64_t *scope,void *context,unsigned bucket)
{
    if(!*scope)return;
    if(!scene_live(context)) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(*scope!=scene.token || (unsigned)(*scope>>32)!=generation) { scene.stale++;return; }
    if(scene.bucket!=1 || bucket>=12 || bucket<=detail1.bucket) { scene.invalid++;return; }
    scene_account(now());detail1.bucket=bucket;detail1.entries[bucket]++;
}
#endif
void xv_scene_partition_end(uint64_t *scope)
{
    uint64_t token=*scope;
    if(!token)return;
    *scope=0;
    if(worker() || !same_thread()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(token!=scene.token || (unsigned)(token>>32)!=generation) { scene.stale++;return; }
    if(!scene_live((void *)__atomic_load_n(&owner_context,__ATOMIC_ACQUIRE))) {
        scene.invalid++;scene.abandoned++;scene.token=0;return;
    }
    scene_account(now());
#if XV_SCENE_BUCKET0_DETAIL
    if(scene.bucket==0)detail.completed++;
#endif
#if XV_SCENE_BUCKET1_DETAIL
    if(scene.bucket==1)detail1.completed++;
#endif
    scene.completed++;scene.token=0;
}
#endif
void xv_owner_phase_begin(xv_owner_phase_scope *scope,void *context,unsigned phase)
{
    if(!xv_owner_phase_enabled)return;
    if(worker()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(!__atomic_load_n(&owner_valid,__ATOMIC_ACQUIRE)) { __atomic_fetch_add(&warmup,1,__ATOMIC_RELAXED);return; }
    if(!same_thread() || __atomic_load_n(&owner_context,__ATOMIC_ACQUIRE)!=(uintptr_t)context) {
        __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return;
    }
    if(phase>=XV_OWNER_PHASES || !live(context) || phases[phase].depth==UINT32_MAX) { __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return; }
    *scope=((uint64_t)generation<<32)|(phase+1u);
    if(phases[phase].depth++)phases[phase].recursive++;
    else { if(phase==XV_OWNER_SCENE)MODEL_UV_BOUNDARY(context);phases[phase].entries++;phases[phase].start=now(); }
}
void xv_owner_phase_end(xv_owner_phase_scope *scope)
{
    uint64_t token=*scope;
    if(!token)return;
    *scope=0;
    if(!same_thread()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if((unsigned)(token>>32)!=generation) { stale++;return; }
    unsigned phase=(unsigned)token-1;
    if(phase>=XV_OWNER_PHASES || !phases[phase].depth) { __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return; }
    if(!--phases[phase].depth) { if(phase==XV_OWNER_SCENE)MODEL_UV_BOUNDARY((void *)__atomic_load_n(&owner_context,__ATOMIC_ACQUIRE));account(phase,now());phases[phase].completed++; }
}
void xv_owner_phase_report(unsigned frames)
{
    if(!xv_owner_phase_enabled || !frames || !same_thread())return;
    if(!live((void *)__atomic_load_n(&owner_context,__ATOMIC_ACQUIRE))) {
        __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return;
    }
#ifdef XV_NATIVE_CONSTANT_PACK
    { extern void xv_constant_pack_stats(unsigned *);
      unsigned p[7]; xv_constant_pack_stats(p);
      XK_LOG("[constant-pack] %u frames accepted %u prefix-matrices %u declined-size %u budget %u mapping %u alias %u watch %u\n",
          frames,p[0],p[1],p[2],p[3],p[4],p[5],p[6]); }
#endif
    /* Split a still-open scope at this report boundary. It can contain
     * Present, yielding, or another selected scope; none is called CPU self. */
    if(phases[0].depth || phases[1].depth
#if XV_SCENE_PARTITION
       || scene.token
#endif
    ) {
        uint64_t end=now();
        for(unsigned i=0;i<XV_OWNER_PHASES;i++)if(phases[i].depth)account(i,end);
#if XV_SCENE_PARTITION
        if(scene.token)scene_account(end);
#endif
    }
    for(unsigned i=0;i<XV_OWNER_PHASES;i++) {
        XK_LOG("[owner-phase] %u frames %s: entries %llu completed %llu recursive %llu open %u elapsed-us %llu; nested/waits included, not CPU self\n",
            frames,i?"BCB30":"FA920",(unsigned long long)phases[i].entries,
            (unsigned long long)phases[i].completed,(unsigned long long)phases[i].recursive,
            phases[i].depth,(unsigned long long)phases[i].elapsed);
        phases[i].entries=phases[i].completed=phases[i].recursive=phases[i].elapsed=0;
    }
#if XV_SCENE_PARTITION
#if XV_SCENE_BUCKET0_DETAIL
    XK_LOG("[scene-bucket0-detail] %u frames 5D410: entries %llu/%llu/%llu/%llu/%llu/%llu elapsed-us %llu/%llu/%llu/%llu/%llu/%llu; completed %llu open %u bucket %u; disjoint within main bucket0, shared timestamps\n",
        frames,(unsigned long long)detail.entries[0],(unsigned long long)detail.entries[1],
        (unsigned long long)detail.entries[2],(unsigned long long)detail.entries[3],
        (unsigned long long)detail.entries[4],(unsigned long long)detail.entries[5],
        (unsigned long long)detail.elapsed[0],(unsigned long long)detail.elapsed[1],
        (unsigned long long)detail.elapsed[2],(unsigned long long)detail.elapsed[3],
        (unsigned long long)detail.elapsed[4],(unsigned long long)detail.elapsed[5],
        (unsigned long long)detail.completed,!!scene.token && scene.bucket==0,detail.bucket);
    XK_LOG("[model-route-detail] %u frames 5B760 elapsed-us %llu/%llu/%llu/%llu completed %llu open %u invalid %u; list-build/secondary/list-loop/tail, inclusive elapsed\n",
        frames,(unsigned long long)model_detail.elapsed[0],(unsigned long long)model_detail.elapsed[1],
        (unsigned long long)model_detail.elapsed[2],(unsigned long long)model_detail.elapsed[3],
        (unsigned long long)model_detail.completed,!!model_detail.token,model_detail.invalid);
    XK_LOG("[model-route-children] %u frames elapsed-us %llu/%llu/%llu/%llu calls %llu/%llu/%llu child-open %u; remainder/cache-refresh/model-packets/distance, nested inclusive elapsed\n",
        frames,(unsigned long long)model_detail.child_elapsed[0],(unsigned long long)model_detail.child_elapsed[1],
        (unsigned long long)model_detail.child_elapsed[2],(unsigned long long)model_detail.child_elapsed[3],
        (unsigned long long)model_detail.child_calls[1],(unsigned long long)model_detail.child_calls[2],
        (unsigned long long)model_detail.child_calls[3],!!model_detail.token && !!model_detail.child);
#if XV_SCENE_BUCKET1_DETAIL
    XK_LOG("[model-route-draw] %u frames completed-draw-us %llu/%llu/%llu/%llu available %u invalid %u; subset of model children, includes recording/waits, not GPU service\n",
        frames,(unsigned long long)model_detail.child_draw[0],(unsigned long long)model_detail.child_draw[1],
        (unsigned long long)model_detail.child_draw[2],(unsigned long long)model_detail.child_draw[3],
        !!&xv_scene_draw_completed_us,model_detail.draw_invalid);
#endif
    memset(model_detail.child_draw,0,sizeof model_detail.child_draw);model_detail.draw_invalid=0;
    memset(model_detail.child_elapsed,0,sizeof model_detail.child_elapsed);
    memset(model_detail.child_calls,0,sizeof model_detail.child_calls);
    memset(model_detail.elapsed,0,sizeof model_detail.elapsed);model_detail.completed=0;model_detail.invalid=0;
    memset(detail.entries,0,sizeof detail.entries);memset(detail.elapsed,0,sizeof detail.elapsed);
    detail.completed=0;
#endif
#if XV_SCENE_BUCKET1_DETAIL
    /* One joined row; no per-draw clocks or formatting. */
    XK_LOG("[scene-bucket1-detail] %u frames entries %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu elapsed-us %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu; completed %llu open %u bucket %u; ordered pass intervals, nested/waits included\n",
        frames,(unsigned long long)detail1.entries[0],(unsigned long long)detail1.entries[1],(unsigned long long)detail1.entries[2],(unsigned long long)detail1.entries[3],(unsigned long long)detail1.entries[4],(unsigned long long)detail1.entries[5],(unsigned long long)detail1.entries[6],(unsigned long long)detail1.entries[7],(unsigned long long)detail1.entries[8],(unsigned long long)detail1.entries[9],(unsigned long long)detail1.entries[10],(unsigned long long)detail1.entries[11],
        (unsigned long long)detail1.elapsed[0],(unsigned long long)detail1.elapsed[1],(unsigned long long)detail1.elapsed[2],(unsigned long long)detail1.elapsed[3],(unsigned long long)detail1.elapsed[4],(unsigned long long)detail1.elapsed[5],(unsigned long long)detail1.elapsed[6],(unsigned long long)detail1.elapsed[7],(unsigned long long)detail1.elapsed[8],(unsigned long long)detail1.elapsed[9],(unsigned long long)detail1.elapsed[10],(unsigned long long)detail1.elapsed[11],
        (unsigned long long)detail1.completed,!!scene.token && scene.bucket==1,detail1.bucket);
    XK_LOG("[scene-bucket1-draw] %u frames completed-draw-us %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu; available %u invalid %u; subset elapsed, includes draw recording/waits, not GPU service or CPU self\n",
        frames,(unsigned long long)draw_detail1.elapsed[0],(unsigned long long)draw_detail1.elapsed[1],(unsigned long long)draw_detail1.elapsed[2],(unsigned long long)draw_detail1.elapsed[3],(unsigned long long)draw_detail1.elapsed[4],(unsigned long long)draw_detail1.elapsed[5],(unsigned long long)draw_detail1.elapsed[6],(unsigned long long)draw_detail1.elapsed[7],(unsigned long long)draw_detail1.elapsed[8],(unsigned long long)draw_detail1.elapsed[9],(unsigned long long)draw_detail1.elapsed[10],(unsigned long long)draw_detail1.elapsed[11],
        !!&xv_scene_draw_completed_us,draw_detail1.invalid);
    memset(draw_detail1.elapsed,0,sizeof draw_detail1.elapsed);draw_detail1.invalid=0;
    memset(detail1.entries,0,sizeof detail1.entries);memset(detail1.elapsed,0,sizeof detail1.elapsed);
    detail1.completed=0;
#endif
    XK_LOG("[scene-partition] %u frames 5D410: entries %llu/%llu/%llu/%llu/%llu/%llu elapsed-us %llu/%llu/%llu/%llu/%llu/%llu; completed %llu open %u bucket %u recursive-declined %u invalid %u abandoned %u stale %u exhausted %u; disjoint outer buckets, nested/waits included\n",
        frames,(unsigned long long)scene.entries[0],(unsigned long long)scene.entries[1],
        (unsigned long long)scene.entries[2],(unsigned long long)scene.entries[3],
        (unsigned long long)scene.entries[4],(unsigned long long)scene.entries[5],
        (unsigned long long)scene.elapsed[0],(unsigned long long)scene.elapsed[1],
        (unsigned long long)scene.elapsed[2],(unsigned long long)scene.elapsed[3],
        (unsigned long long)scene.elapsed[4],(unsigned long long)scene.elapsed[5],
        (unsigned long long)scene.completed,!!scene.token,scene.bucket,
        scene.recursive,scene.invalid,scene.abandoned,scene.stale,scene.exhausted);
    memset(scene.entries,0,sizeof scene.entries);memset(scene.elapsed,0,sizeof scene.elapsed);
    scene.completed=0;scene.recursive=scene.invalid=scene.abandoned=scene.stale=0;
#endif
#if defined(XV_MODEL_UV) && XV_MODEL_UV
    { extern void xk_model_uv_report(unsigned); xk_model_uv_report(frames); }
#endif
#if defined(XV_MODEL_FOG) && XV_MODEL_FOG
    { extern void xk_model_fog_report(unsigned); xk_model_fog_report(frames); }
#endif
    XK_LOG("[owner-phase-status] clocks %llu warmup %u foreign %u invalid %u rebinds %u abandoned %u stale %u; native owner/context only, clock calls exclude report formatting\n",
        (unsigned long long)clock_reads,__atomic_exchange_n(&warmup,0,__ATOMIC_RELAXED),
        __atomic_exchange_n(&foreign,0,__ATOMIC_RELAXED),__atomic_exchange_n(&invalid,0,__ATOMIC_RELAXED),rebinds,abandoned,stale);
    clock_reads=0;rebinds=abandoned=stale=0;
}
