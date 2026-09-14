#include "xv_benchmark.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

void xv_logf(const char *fmt,...);
void xv_benchmark_optimizations(int enabled) __attribute__((weak));
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
void xv_benchmark_toggle(void) { if(b.active)b.cancel=1;else b.request=1; }
void xv_benchmark_compare_toggle(void) { if(b.active)b.cancel=1;else b.request=2; }
int xv_benchmark_active(void) { return b.active||b.request; }
uint32_t xv_benchmark_status(void) { return __atomic_load_n(&status,__ATOMIC_ACQUIRE); }
static unsigned phase_height(void) { return b.compare?b.original:heights[b.phase]; }
int xv_benchmark_compare_object_basis(void)
{
    static int selected=-1;
    if (selected<0) {
        const char *e=getenv("XV_BENCHMARK_OBJECT_BASIS"); selected=e && atoi(e)!=0;
    }
    return selected;
}
int xv_benchmark_compare_model_palette(void)
{
    static int selected=-1;
    if (selected<0) {
        const char *e=getenv("XV_BENCHMARK_MODEL_PALETTE"); selected=e && atoi(e)!=0;
    }
    return selected && !xv_benchmark_compare_object_basis();
}
static int native_math_selected(void)
{
    return xv_benchmark_compare_object_basis() || xv_benchmark_compare_model_palette();
}
static int candidate_available(void)
{
    if (!native_math_selected()) return 1;
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
    static int selected=-1;
    if (selected<0) {
        const char *e=getenv("XV_BENCHMARK_VERTEX_WORKER"); selected=e && atoi(e)!=0;
    }
    return selected && !native_math_selected();
}
int xv_benchmark_compare_vertex_references(void)
{
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_VERTEX_REFERENCES"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker();
}
int xv_benchmark_compare_native_bounds(void)
{
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_NATIVE_BOUNDS"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker() && !xv_benchmark_compare_vertex_references();
}
int xv_benchmark_compare_vertex_copy(void)
{
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_VERTEX_COPY"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker() && !xv_benchmark_compare_native_bounds() && !xv_benchmark_compare_vertex_references();
}
int xv_benchmark_compare_draw_scan(void)
{
    static int selected = -1;
    if (selected < 0) {
        const char *e = getenv("XV_BENCHMARK_DRAW_SCAN"); selected = e && atoi(e) != 0;
    }
    return selected && !native_math_selected() && !xv_benchmark_compare_vertex_worker() && !xv_benchmark_compare_vertex_copy() && !xv_benchmark_compare_native_bounds() && !xv_benchmark_compare_vertex_references();
}
static const char *tag(void) { return b.compare ?
    (xv_benchmark_compare_object_basis() ? "object-basis-compare" :
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
        if(!valid || (compare&&!xv_benchmark_optimizations)) {xv_logf("[%s] start requires a loaded first-person view and available test hooks\n",tag());return 0;}
        if(compare && !candidate_available()) {
            xv_logf("[%s] selected native math experiment is not compiled in or XV_NATIVE_MATH is disabled; no settings changed\n",tag());
            return 0;
        }
        b.active=b.configuring=b.view_ok=1;b.original=height;
        if(compare) {
            xv_benchmark_optimizations(0);
            if (native_math_selected())
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
        b.active=b.configuring=b.restoring=0;publish();return;
    }
    if(height!=phase_height()) {
        xv_logf("[%s] allocation fallback %up; cancel requested\n",tag(),height);
        b.configuring=0;b.cancel=1;return;
    }
    b.configuring=0;b.frames=0;
    xv_logf("[%s] phase %u %up settling\n",tag(),b.phase+1,height);publish();
}
