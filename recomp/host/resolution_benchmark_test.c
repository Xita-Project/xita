#include <assert.h>
#include <stdio.h>
#include "../../runtime/xv_benchmark.c"
void xv_logf(const char *fmt,...) { (void)fmt; }
static int optimization=-1;
static unsigned switches;
void xv_benchmark_optimizations(int enabled) { optimization=enabled;switches++; }
int main(void)
{
    const char *select = getenv("XV_BENCHMARK_DRAW_SCAN");
    int scan = select && atoi(select) != 0;
    select = getenv("XV_BENCHMARK_VERTEX_COPY");
    int copy = select && atoi(select) != 0;
    select = getenv("XV_BENCHMARK_NATIVE_BOUNDS");
    int bounds = select && atoi(select) != 0;
    select = getenv("XV_BENCHMARK_VERTEX_REFERENCES");
    int references = select && atoi(select) != 0;
    select = getenv("XV_BENCHMARK_VERTEX_WORKER");
    int worker = select && atoi(select) != 0;
    select = getenv("XV_BENCHMARK_OBJECT_BASIS");
    int basis = select && atoi(select) != 0;
    select = getenv("XV_BENCHMARK_MODEL_PALETTE");
    int palette = !basis && select && atoi(select) != 0;
    if (basis || palette) worker = references = bounds = copy = scan = 0;
    assert(xv_benchmark_compare_object_basis()==basis);
    assert(xv_benchmark_compare_model_palette()==palette);
    if (worker) references = bounds = copy = scan = 0;
    assert(xv_benchmark_compare_vertex_worker()==worker);
    if (references) bounds = copy = scan = 0;
    if (bounds) copy = scan = 0;
    assert(xv_benchmark_compare_vertex_references() == references);
    if (copy) scan = 0;
    assert(xv_benchmark_compare_native_bounds() == bounds);
    assert(xv_benchmark_compare_vertex_copy() == copy);
    assert(xv_benchmark_compare_draw_scan() == scan);
    float view[6]={1,2,3,0,1,0};uint64_t now=1;unsigned height=480;
    xv_benchmark_toggle();assert(!xv_benchmark_step(now,height,0,view)&&!xv_benchmark_active());
    xv_benchmark_toggle();assert(xv_benchmark_step(now,height,1,view)==544);
    assert(xv_benchmark_active());height=544;xv_benchmark_applied(now,height);
    for(unsigned phase=0;phase<3;phase++)for(unsigned frame=1;frame<=180;frame++) {
        now+=phase==1?50000:100000;
        unsigned next=xv_benchmark_step(now,height,1,view);
        if(frame<180)assert(!next);
        else {assert(next==(phase==0?360:phase==1?544:480));height=next;xv_benchmark_applied(now,height);}
    }
    assert(!xv_benchmark_active()&&!xv_benchmark_status()&&height==480&&b.view_ok);
    assert(b.fps[0]==10&&b.fps[1]==20&&b.fps[2]==10);
    /* A moved camera rejects comparability; cancel and loss of view restore. */
    xv_benchmark_toggle();assert(xv_benchmark_step(now,360,1,view)==544);xv_benchmark_applied(now,544);
    for(unsigned i=0;i<60;i++){now+=100000;assert(!xv_benchmark_step(now,544,1,view));}
    view[0]+=1;now+=100000;assert(!xv_benchmark_step(now,544,1,view)&&!b.view_ok);
    xv_benchmark_toggle();assert(xv_benchmark_step(now,544,1,view)==360);xv_benchmark_applied(now,360);assert(!b.active);
    xv_benchmark_toggle();assert(xv_benchmark_step(now,400,1,view)==544);xv_benchmark_applied(now,544);
    assert(xv_benchmark_step(now,544,0,view)==400);xv_benchmark_applied(now,400);
    /* Failed scaled allocation cancels and restores the original resolution. */
    xv_benchmark_toggle();assert(xv_benchmark_step(now,480,1,view)==544);xv_benchmark_applied(now,544);
    for(unsigned i=0;i<180;i++){now+=100000;unsigned next=xv_benchmark_step(now,544,1,view);assert(next==(i==179?360:0));}
    xv_benchmark_applied(now,544);assert(xv_benchmark_step(now,544,1,view)==480);
    xv_benchmark_applied(now,480);assert(!b.active&&!xv_benchmark_status());
    puts("PASS: timed 544/360/544 phases, warmup excluded, view rejection, cancel/menu/allocation fallback and restoration");
    assert(!switches); /* Resolution testing must not touch CPU switches. */
    int available=1;
#ifndef XV_NATIVE_OBJECT_BASIS
    if (basis) available=0;
#endif
#ifndef XV_NATIVE_MODEL_PALETTE
    if (palette) available=0;
#endif
    select=getenv("XV_NATIVE_MATH");
    if ((basis || palette) && select && !atoi(select)) available=0;
    if (!available) {
        xv_benchmark_compare_toggle();
        assert(!xv_benchmark_step(now,480,1,view));
        assert(!b.active && !b.request && !switches && optimization==-1);
        puts("PASS: unavailable native experiment rejects without changing settings or selecting another test");
        return 0;
    }
    xv_benchmark_compare_toggle();assert(xv_benchmark_step(now,480,1,view)==480);
    assert(!strcmp(tag(), basis ? "object-basis-compare" : palette ? "model-palette-compare" : worker ? "vertex-worker-compare" : references ? "vertex-references-compare" : bounds ? "native-bounds-compare" : copy ? "vertex-copy-compare" : scan ? "draw-scan-compare" : "flare-compare"));
    assert(optimization==0 && (xv_benchmark_status()&(1u<<19)));
    xv_benchmark_applied(now,480);
    for(unsigned phase=0;phase<3;phase++)for(unsigned frame=1;frame<=180;frame++) {
        assert(optimization==(int)(phase==1));
        now+=phase==1?80000:100000;
        unsigned next=xv_benchmark_step(now,480,1,view);
        if(frame<180)assert(!next);
        else {assert(next==480);xv_benchmark_applied(now,480);}
    }
    assert(!b.active&&!xv_benchmark_status()&&optimization==-1&&switches==4);
    assert(b.fps[0]==10&&b.fps[1]==12.5&&b.fps[2]==10&&b.view_ok);
    /* Cancellation during the enabled phase restores configured defaults. */
    xv_benchmark_compare_toggle();assert(xv_benchmark_step(now,360,1,view)==360);xv_benchmark_applied(now,360);
    for(unsigned i=0;i<180;i++){now+=100000;unsigned next=xv_benchmark_step(now,360,1,view);if(next)xv_benchmark_applied(now,next);}
    assert(optimization==1);xv_benchmark_toggle();assert(xv_benchmark_step(now,360,1,view)==360);
    xv_benchmark_applied(now,360);assert(optimization==-1&&!b.active);
    xv_benchmark_compare_toggle();assert(xv_benchmark_step(now,544,1,view)==544);xv_benchmark_applied(now,544);
    assert(xv_benchmark_step(now,544,0,view)==544&&optimization==-1);xv_benchmark_applied(now,544);
    switches=0;xv_benchmark_compare_toggle();assert(!xv_benchmark_step(now,544,0,view)&&!switches&&!b.active);
    puts("PASS: CPU off/on/off uses fixed resolution, excludes warmup and restores overrides on completion, cancellation or lost view");
    xv_benchmark_remote_poll(0);
    assert(xv_benchmark_remote_request(XV_BENCH_RESOLUTION)==-1);
    xv_benchmark_remote_poll(1);
    assert(xv_benchmark_remote_request(0)==-1 && xv_benchmark_remote_request(XV_BENCH_EARLY_VISIBILITY+1)==-1);
    assert(xv_benchmark_remote_request(XV_BENCH_RESOLUTION)==0 && !b.request && !b.active);
    assert(xv_benchmark_remote_request(XV_BENCH_FLARE)==-1);
    xv_benchmark_remote_poll(0);assert(!xv_benchmark_remote_busy());
    xv_benchmark_remote_poll(1);
    assert(xv_benchmark_remote_request(XV_BENCH_RESOLUTION)==0);
    xv_benchmark_remote_poll(1);assert(b.request==1);
    assert(xv_benchmark_step(now,360,1,view)==544);xv_benchmark_applied(now,544);
    assert(xv_benchmark_remote_request(XV_BENCH_FLARE)==-1);
    xv_benchmark_toggle();assert(xv_benchmark_step(now,544,1,view)==360);
    xv_benchmark_applied(now,360);assert(!xv_benchmark_remote_busy() && !remote_kind);
    unsigned kinds[]={XV_BENCH_EARLY_VISIBILITY,XV_BENCH_VERTEX_WORKER,XV_BENCH_FLARE,XV_BENCH_MODEL_PALETTE,XV_BENCH_OBJECT_BASIS};
    for(unsigned i=0;i<sizeof kinds/sizeof *kinds;i++) {
        int compiled=1;
#ifndef XV_NATIVE_OBJECT_BASIS
        if(kinds[i]==XV_BENCH_OBJECT_BASIS)compiled=0;
#endif
#ifndef XV_NATIVE_MODEL_PALETTE
        if(kinds[i]==XV_BENCH_MODEL_PALETTE)compiled=0;
#endif
        assert(xv_benchmark_remote_request(kinds[i])==(compiled?0:-1));
        if(!compiled)continue;
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_compare_early_visibility()==(kinds[i]==XV_BENCH_EARLY_VISIBILITY));
        assert(xv_benchmark_compare_object_basis()==(kinds[i]==XV_BENCH_OBJECT_BASIS));
        assert(xv_benchmark_compare_model_palette()==(kinds[i]==XV_BENCH_MODEL_PALETTE));
        assert(xv_benchmark_compare_vertex_worker()==(kinds[i]==XV_BENCH_VERTEX_WORKER));
        assert(xv_benchmark_step(now,360,1,view)==360 && optimization==0);xv_benchmark_applied(now,360);
        xv_benchmark_compare_toggle();assert(xv_benchmark_step(now,360,1,view)==360 && optimization==-1);
        xv_benchmark_applied(now,360);assert(!xv_benchmark_remote_busy() && !remote_kind);
    }
    puts("PASS: remote admission, guest-owner consumption, menu rejection, explicit candidate priority, unavailable builds and cancellation restore");
}
