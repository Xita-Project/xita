#include "xv_benchmark.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

void xv_logf(const char *fmt,...);
void xv_benchmark_optimizations(int enabled) __attribute__((weak));
void xv_point_math_override(int enabled) __attribute__((weak));
void xv_matrix_neon_override(int enabled) __attribute__((weak));
void xv_object_scan_override(int enabled) __attribute__((weak));
void xv_hle_dispatch_override(int enabled) __attribute__((weak));
void xv_flare_query_overlap_override(int enabled) __attribute__((weak));
void xv_guest_affinity_override(int) __attribute__((weak));
int xv_guest_affinity_valid(void) __attribute__((weak));
enum { SETTLE=60, MEASURE=120, PHASES=3 };
static const unsigned heights[PHASES]={544,360,544};
static struct {
    int active,request,cancel,configuring,restoring,view_ok,compare;
    unsigned original,phase,frames;
    uint64_t measure_start;
    float view[6];
    double fps[PHASES];
} b;
static uint32_t status;
static unsigned request_state, remote_ready, remote_kind;
#define BENCH_RUNNING UINT32_MAX
unsigned xv_benchmark_remote_busy(void) {return __atomic_load_n(&request_state,__ATOMIC_ACQUIRE)!=0;}
int xv_benchmark_remote_request(unsigned kind)
{
    if(kind<XV_BENCH_OBJECT_BASIS||kind>XV_BENCH_GUEST_AFFINITY||!__atomic_load_n(&remote_ready,__ATOMIC_ACQUIRE))return -1;
#ifndef XV_NATIVE_OBJECT_BASIS
    if(kind==XV_BENCH_OBJECT_BASIS)return -1;
#endif
#ifndef XV_NATIVE_MODEL_PALETTE
    if(kind==XV_BENCH_MODEL_PALETTE)return -1;
#endif
    if(kind==XV_BENCH_POINT_MATH&&!xv_point_math_override)return -1;
    if(kind==XV_BENCH_MATRIX_NEON&&!xv_matrix_neon_override)return -1;
    if(kind==XV_BENCH_OBJECT_SCAN&&!xv_object_scan_override)return -1;
    if(kind==XV_BENCH_FLARE_QUERY_OVERLAP&&!xv_flare_query_overlap_override)return -1;
    if(kind==XV_BENCH_HLE_DISPATCH&&!xv_hle_dispatch_override)return -1;
    if(kind==XV_BENCH_GUEST_AFFINITY&&(!xv_guest_affinity_override||!xv_guest_affinity_valid))return -1;
    unsigned expected=0;
    return __atomic_compare_exchange_n(&request_state,&expected,kind,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)?0:-1;
}
void xv_benchmark_remote_poll(int control)
{
    __atomic_store_n(&remote_ready,!!control,__ATOMIC_RELEASE);
    unsigned kind=__atomic_load_n(&request_state,__ATOMIC_ACQUIRE);
    if(!kind||kind==BENCH_RUNNING)return;
    if(!__atomic_compare_exchange_n(&request_state,&kind,BENCH_RUNNING,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))return;
    if(!control) {
        xv_logf("[remote-benchmark] request rejected: first-person control unavailable\n");
        __atomic_store_n(&request_state,0,__ATOMIC_RELEASE);return;
    }
    remote_kind=kind;b.request=kind==XV_BENCH_RESOLUTION?1:2;
}
static void toggle(int compare)
{
    if(b.active) {b.cancel=1;return;}
    if(b.request) {b.request=0;__atomic_store_n(&request_state,0,__ATOMIC_RELEASE);return;}
    unsigned expected=0;
    if(__atomic_compare_exchange_n(&request_state,&expected,BENCH_RUNNING,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) {
        remote_kind=0;b.request=compare?2:1;
    }
}
void xv_benchmark_toggle(void) {toggle(0);}
void xv_benchmark_compare_toggle(void) {toggle(1);}
int xv_benchmark_active(void) { return b.active||b.request||xv_benchmark_remote_busy(); }
uint32_t xv_benchmark_status(void) { return __atomic_load_n(&status,__ATOMIC_ACQUIRE); }
static unsigned phase_height(void) { return b.compare?b.original:heights[b.phase]; }
int xv_benchmark_compare_texture_state(void) { return remote_kind==XV_BENCH_TEXTURE_STATE; }
int xv_benchmark_compare_point_math(void) { return remote_kind==XV_BENCH_POINT_MATH; }
int xv_benchmark_compare_matrix_neon(void) { return remote_kind==XV_BENCH_MATRIX_NEON; }
int xv_benchmark_compare_object_scan(void) { return remote_kind==XV_BENCH_OBJECT_SCAN; }
int xv_benchmark_compare_flare_query_overlap(void) { return remote_kind==XV_BENCH_FLARE_QUERY_OVERLAP; }
int xv_benchmark_compare_guest_affinity(void) { return remote_kind==XV_BENCH_GUEST_AFFINITY; }
int xv_benchmark_compare_hle_dispatch(void) { return remote_kind==XV_BENCH_HLE_DISPATCH; }
int xv_benchmark_compare_early_visibility(void) { return remote_kind==XV_BENCH_EARLY_VISIBILITY; }
int xv_benchmark_compare_object_basis(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_OBJECT_BASIS;
    static int selected=-1;
    if (selected<0) {
        const char *e=getenv("XV_BENCHMARK_OBJECT_BASIS"); selected=e && atoi(e)!=0;
    }
    return selected;
}
int xv_benchmark_compare_model_palette(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_MODEL_PALETTE;
    static int selected=-1;
    if (selected<0) {
        const char *e=getenv("XV_BENCHMARK_MODEL_PALETTE"); selected=e && atoi(e)!=0;
    }
    return selected && !xv_benchmark_compare_object_basis();
}
static int native_math_selected(void)
{
    return xv_benchmark_compare_object_basis() || xv_benchmark_compare_model_palette() || xv_benchmark_compare_point_math() || xv_benchmark_compare_matrix_neon();
}
static int candidate_available(void)
{
    if (xv_benchmark_compare_guest_affinity()) return xv_guest_affinity_override && xv_guest_affinity_valid;
    if (xv_benchmark_compare_object_scan()) return xv_object_scan_override != 0;
    if (xv_benchmark_compare_flare_query_overlap()) {
        const char *defer=getenv("XV_FLARE_DEFER"), *stale=getenv("XV_VIS_STALE");
        return xv_flare_query_overlap_override && (!defer || atoi(defer)!=0) &&
               (!stale || atoi(stale)==0);
    }
    if (xv_benchmark_compare_hle_dispatch()) return xv_hle_dispatch_override != 0;
    if (!native_math_selected()) return 1;
    if (xv_benchmark_compare_point_math() && !xv_point_math_override) return 0;
    if (xv_benchmark_compare_matrix_neon() && !xv_matrix_neon_override) return 0;
    const char *math=getenv("XV_NATIVE_MATH");
    if (math && !atoi(math)) return 0;
#ifndef XV_NATIVE_OBJECT_BASIS
    if (xv_benchmark_compare_object_basis()) return 0;
#endif
#ifndef XV_NATIVE_MODEL_PALETTE
    if (xv_benchmark_compare_model_palette()) return 0;
#endif
    return 1;
}
int xv_benchmark_compare_vertex_worker(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_VERTEX_WORKER;
    static int selected=-1;
    if (selected<0) {
        const char *e=getenv("XV_BENCHMARK_VERTEX_WORKER"); selected=e && atoi(e)!=0;
    }
    return selected && !native_math_selected();
}
int xv_benchmark_compare_vertex_references(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_VERTEX_REFERENCES;
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_VERTEX_REFERENCES"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker();
}
int xv_benchmark_compare_native_bounds(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_NATIVE_BOUNDS;
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_NATIVE_BOUNDS"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker() && !xv_benchmark_compare_vertex_references();
}
int xv_benchmark_compare_vertex_copy(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_VERTEX_COPY;
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_VERTEX_COPY"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker() && !xv_benchmark_compare_native_bounds() && !xv_benchmark_compare_vertex_references();
}
int xv_benchmark_compare_draw_scan(void)
{
    if(remote_kind)return remote_kind==XV_BENCH_DRAW_SCAN;
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_DRAW_SCAN"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker() && !xv_benchmark_compare_vertex_copy() && !xv_benchmark_compare_native_bounds() && !xv_benchmark_compare_vertex_references();
}
static const char *tag(void) { return b.compare ?
    (xv_benchmark_compare_guest_affinity() ? "guest-affinity-compare" :
     xv_benchmark_compare_flare_query_overlap() ? "flare-query-overlap-compare" :
     xv_benchmark_compare_hle_dispatch() ? "hle-dispatch-compare" :
     xv_benchmark_compare_object_scan() ? "object-scan-compare" :
     xv_benchmark_compare_texture_state() ? "texture-state-compare" :
     xv_benchmark_compare_matrix_neon() ? "matrix-neon-compare" :
     xv_benchmark_compare_point_math() ? "point-math-compare" :
     xv_benchmark_compare_early_visibility() ? "early-visibility-compare" :
     xv_benchmark_compare_object_basis() ? "object-basis-compare" :
     xv_benchmark_compare_model_palette() ? "model-palette-compare" :
     xv_benchmark_compare_vertex_worker() ? "vertex-worker-compare" :
     xv_benchmark_compare_vertex_references() ? "vertex-references-compare" :
     xv_benchmark_compare_native_bounds() ? "native-bounds-compare" :
     xv_benchmark_compare_vertex_copy() ? "vertex-copy-compare" :
     xv_benchmark_compare_draw_scan() ? "draw-scan-compare" : "flare-compare") : "resolution-test"; }
static void publish(void)
{
    unsigned progress=b.frames*100/(SETTLE+MEASURE);
    __atomic_store_n(&status,b.active&&!b.restoring ? phase_height()|(progress<<10)|((b.phase+1)<<17)|((unsigned)b.compare<<19):0,__ATOMIC_RELEASE);
}
static unsigned restore(const char *why)
{
    xv_logf("[%s] %s; restoring %up\n",tag(),why,b.original);
    if(b.compare)xv_benchmark_optimizations(-1);
    b.restoring=b.configuring=1;b.cancel=0;publish();return b.original;
}
unsigned xv_benchmark_step(uint64_t now,unsigned height,int valid,const float view[6])
{
    if(b.request) {
        int compare=b.request==2;
        memset(&b,0,sizeof b);
        b.compare=compare;
        if(!valid || (compare&&!xv_benchmark_optimizations)) {xv_logf("[%s] start requires a loaded first-person view and available test hooks\n",tag());__atomic_store_n(&request_state,0,__ATOMIC_RELEASE);return 0;}
        if(compare && !candidate_available()) {
            xv_logf("[%s] selected experiment is unavailable or its required mode is disabled; no settings changed\n",tag());
            __atomic_store_n(&request_state,0,__ATOMIC_RELEASE);
            return 0;
        }
        b.active=b.configuring=b.view_ok=1;b.original=height;
        if(compare) {
            xv_benchmark_optimizations(0);
            if (xv_benchmark_compare_guest_affinity())
                xv_logf("[guest-affinity-compare] start original/core-2/original at %up; presenting guest thread only; exact original mask restored; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_flare_query_overlap())
                xv_logf("[flare-query-overlap-compare] start off/on/off at %up; retain exact query generations across new query recording; brightness, identity and Present still drain; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_hle_dispatch())
                xv_logf("[hle-dispatch-compare] start off/on/off at %up; reuse immutable HLE lookup results; guest-first priority, callbacks and tracing retained; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_object_scan())
                xv_logf("[object-scan-compare] start off/on/off at %up; batch zero object identifiers only; active callbacks, update order and scheduling budget retained; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_texture_state())
                xv_logf("[texture-state-compare] start off/on/off at %up; only identical resolved mesh texture bindings are cached within uninterrupted ranges; draw order, shaders, geometry, workers and settings unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_early_visibility())
                xv_logf("[early-visibility-compare] start off/on/off at %up; final/world/final fragment fence publishes exact query results; frame storage retains final-fence ownership; scenes, draws, workers and settings unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (native_math_selected())
                xv_logf("[%s] start off/on/off at %up; only selected native helper changes; other math, workers, resolution, shaders, queue policy and frame cap unchanged; %u settle + %u measured frames each\n",tag(),height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_vertex_worker())
                xv_logf("[vertex-worker-compare] start off/on/off at %up; caller/core-0/caller GPU copies from immutable vertex snapshots; resolution, shaders, queue policy and frame cap unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_vertex_references())
                xv_logf("[vertex-references-compare] start off/on/off at %up; full/indexed/full vertex validation; exact retained indices and referenced records, owned uploads and all other settings unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_native_bounds())
                xv_logf("[native-bounds-compare] start off/on/off at %up; original/native/original bounding-box visibility; resolution, draw preparation, effects, visibility waits, queue policy and frame cap unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_vertex_copy())
                xv_logf("[vertex-copy-compare] start off/on/off at %up; two-copy/fused/two-copy owned vertex snapshots; scan, visibility, vertex comparison, residency, queue mode, shaders and settings unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else if (xv_benchmark_compare_draw_scan())
                xv_logf("[draw-scan-compare] start off/on/off at %up; scalar/NEON/scalar exact index copy and constant equality; visibility, vertex comparison, upload residency, queue mode, shaders and settings unchanged; %u settle + %u measured frames each\n",height,SETTLE,MEASURE);
            else xv_logf("[flare-compare] start off/on/off at %up; eager/deferred/eager exact results; vertex comparison, upload residency, configured queue mode and shaders unchanged; %u settle + %u measured frames each; effects and frame cap unchanged\n",height,SETTLE,MEASURE);
        } else xv_logf("[resolution-test] start 544/360/544; %u settle + %u measured frames each; effects and frame cap unchanged\n",SETTLE,MEASURE);
        publish();return phase_height();
    }
    if(!b.active||b.configuring)return 0;
    if(xv_benchmark_compare_guest_affinity() && !xv_guest_affinity_valid())return restore("affinity API failure");
    if(b.cancel||!valid)return restore(b.cancel?"cancelled":"view unavailable");
    b.frames++;
    if(b.frames==SETTLE) {
        if(!b.phase)memcpy(b.view,view,sizeof b.view);
        b.measure_start=now;
        xv_logf("[%s] phase %u %up measure begin view %.4f %.4f %.4f / %.5f %.5f %.5f\n",
            tag(),b.phase+1,height,view[0],view[1],view[2],view[3],view[4],view[5]);
    }
    if(b.frames>=SETTLE)for(unsigned i=0;i<6;i++)
        if(!isfinite(view[i])||fabsf(view[i]-b.view[i])>(i<3?.05f:.005f))b.view_ok=0;
    if(b.frames==SETTLE+MEASURE) {
        uint64_t elapsed=now-b.measure_start;
        b.fps[b.phase]=elapsed?MEASURE*1e6/(double)elapsed:0;
        xv_logf("[%s] phase %u %up %u frames elapsed-us %llu fps %.3f view-ok %d\n",
            tag(),b.phase+1,height,MEASURE,(unsigned long long)elapsed,b.fps[b.phase],b.view_ok);
        if(++b.phase==PHASES) {
            if(b.compare)xv_logf("[%s] result off-before %.3f on %.3f off-after %.3f fps comparable-view %d\n",
                tag(),b.fps[0],b.fps[1],b.fps[2],b.view_ok);
            else xv_logf("[resolution-test] result 544-before %.3f 360 %.3f 544-after %.3f fps comparable-view %d\n",
                b.fps[0],b.fps[1],b.fps[2],b.view_ok);
            return restore("complete");
        }
        if(b.compare)xv_benchmark_optimizations(b.phase==1);
        b.configuring=1;publish();return phase_height();
    }
    publish();return 0;
}
void xv_benchmark_applied(uint64_t now,unsigned height)
{
    (void)now;
    if(!b.active||!b.configuring)return;
    if(b.restoring) {
        xv_logf("[%s] restored %up (requested %up)\n",tag(),height,b.original);
        b.active=b.configuring=b.restoring=0;publish();remote_kind=0;
        __atomic_store_n(&request_state,0,__ATOMIC_RELEASE);return;
    }
    if(height!=phase_height()) {
        xv_logf("[%s] allocation fallback %up; cancel requested\n",tag(),height);
        b.configuring=0;b.cancel=1;return;
    }
    b.configuring=0;b.frames=0;
    xv_logf("[%s] phase %u %up settling\n",tag(),b.phase+1,height);publish();
}
