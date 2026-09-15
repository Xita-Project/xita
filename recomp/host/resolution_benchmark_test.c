#include <assert.h>
#include <stdio.h>
#include "../../runtime/xv_benchmark.c"
void xv_logf(const char *fmt,...) { (void)fmt; }
static int optimization=-1;
#ifndef TEST_NO_OBJECT_LOCK
static int object_lock_ready=1;
int xv_object_lock_available(void) { return object_lock_ready; }
void xv_object_lock_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_OBJECT_MATH
static int object_math_ready=1;
int xv_object_math_available(void) { return object_math_ready; }
void xv_object_math_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_OBJECT_JOBS
static int object_jobs_ready=1;
int xv_object_jobs_available(void) { return object_jobs_ready; }
void xv_object_jobs_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_VERTEX_PREPARE
static int prepare_ready=1;
int xv_vertex_prepare_available(void) { return prepare_ready; }
void xv_vertex_prepare_override(int enabled,unsigned minimum) { (void)enabled;(void)minimum; }
#endif
static unsigned switches;
#ifndef TEST_NO_DEPTH_PREPARE
static int depth_ready=1;
int xv_depth_prepare_available(void) { return depth_ready; }
void xv_depth_prepare_override(int enabled) { (void)enabled; }
#endif
static int affinity_ok=1;
static int snapshot_worker_ready=1;
#ifndef TEST_NO_GUEST_PHASES
static int phase_ready=1;
void xv_phase_capture_override(int enabled) { (void)enabled; }
int xv_phase_capture_available(void) { return phase_ready; }
#endif
void xv_snapshot_worker_override(int enabled) { (void)enabled; }
int xv_vertex_worker_enabled(void) { return snapshot_worker_ready; }
#ifndef TEST_NO_GUEST_AFFINITY
void xv_guest_affinity_override(int enabled) { (void)enabled; }
int xv_guest_affinity_valid(void) { return affinity_ok; }
#endif
#ifndef TEST_NO_FLARE_QUERY_OVERLAP
void xv_flare_query_overlap_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_POINT_MATH
void xv_point_math_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_MATRIX_NEON
void xv_matrix_neon_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_HLE_DISPATCH
void xv_hle_dispatch_override(int enabled) { (void)enabled; }
#endif
#ifndef TEST_NO_OBJECT_SCAN
void xv_object_scan_override(int enabled) { (void)enabled; }
#endif
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
    assert(xv_benchmark_remote_request(0)==-1 && xv_benchmark_remote_request(XV_BENCH_OBJECT_LOCK+1)==-1);
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
    unsigned kinds[]={XV_BENCH_OBJECT_LOCK,XV_BENCH_OBJECT_MATH,XV_BENCH_DEPTH_PREPARE,XV_BENCH_VERTEX_PREPARE,XV_BENCH_OBJECT_JOBS,XV_BENCH_PREP_BUNDLE,XV_BENCH_GUEST_PHASES,XV_BENCH_SNAPSHOT_WORKER,XV_BENCH_GUEST_AFFINITY,XV_BENCH_FLARE_QUERY_OVERLAP,XV_BENCH_HLE_DISPATCH,XV_BENCH_OBJECT_SCAN,XV_BENCH_MATRIX_NEON,XV_BENCH_TEXTURE_STATE,XV_BENCH_POINT_MATH,XV_BENCH_EARLY_VISIBILITY,XV_BENCH_VERTEX_WORKER,XV_BENCH_FLARE,XV_BENCH_MODEL_PALETTE,XV_BENCH_OBJECT_BASIS};
    for(unsigned i=0;i<sizeof kinds/sizeof *kinds;i++) {
        int compiled=1;
#ifdef TEST_NO_DEPTH_PREPARE
        if(kinds[i]==XV_BENCH_DEPTH_PREPARE)compiled=0;
#endif
#ifdef TEST_NO_VERTEX_PREPARE
        if(kinds[i]==XV_BENCH_VERTEX_PREPARE)compiled=0;
#endif
#ifdef TEST_NO_OBJECT_JOBS
        if(kinds[i]==XV_BENCH_OBJECT_JOBS)compiled=0;
#endif
#ifdef TEST_NO_GUEST_PHASES
        if(kinds[i]==XV_BENCH_GUEST_PHASES)compiled=0;
#endif
#ifdef TEST_NO_GUEST_AFFINITY
        if(kinds[i]==XV_BENCH_GUEST_AFFINITY)compiled=0;
#endif
#ifdef TEST_NO_FLARE_QUERY_OVERLAP
        if(kinds[i]==XV_BENCH_FLARE_QUERY_OVERLAP)compiled=0;
#endif
#ifdef TEST_NO_POINT_MATH
        if(kinds[i]==XV_BENCH_POINT_MATH)compiled=0;
#endif
#ifdef TEST_NO_MATRIX_NEON
        if(kinds[i]==XV_BENCH_MATRIX_NEON||kinds[i]==XV_BENCH_PREP_BUNDLE)compiled=0;
#endif
#ifdef TEST_NO_HLE_DISPATCH
        if(kinds[i]==XV_BENCH_HLE_DISPATCH)compiled=0;
#endif
#ifdef TEST_NO_OBJECT_SCAN
        if(kinds[i]==XV_BENCH_OBJECT_SCAN||kinds[i]==XV_BENCH_PREP_BUNDLE)compiled=0;
#endif
#ifndef XV_NATIVE_OBJECT_BASIS
        if(kinds[i]==XV_BENCH_OBJECT_BASIS)compiled=0;
#endif
#ifndef XV_NATIVE_MODEL_PALETTE
        if(kinds[i]==XV_BENCH_MODEL_PALETTE)compiled=0;
#endif
#ifdef TEST_NO_OBJECT_MATH
        if(kinds[i]==XV_BENCH_OBJECT_MATH)compiled=0;
#endif
#ifdef TEST_NO_OBJECT_LOCK
        if(kinds[i]==XV_BENCH_OBJECT_LOCK)compiled=0;
#endif
        assert(xv_benchmark_remote_request(kinds[i])==(compiled?0:-1));
        if(!compiled)continue;
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_compare_point_math()==(kinds[i]==XV_BENCH_POINT_MATH));
        assert(xv_benchmark_compare_matrix_neon()==(kinds[i]==XV_BENCH_MATRIX_NEON));
        assert(xv_benchmark_compare_object_lock()==(kinds[i]==XV_BENCH_OBJECT_LOCK));
        assert(xv_benchmark_compare_object_math()==(kinds[i]==XV_BENCH_OBJECT_MATH));
        assert(xv_benchmark_compare_depth_prepare()==(kinds[i]==XV_BENCH_DEPTH_PREPARE));
        assert(xv_benchmark_compare_vertex_prepare()==(kinds[i]==XV_BENCH_VERTEX_PREPARE));
        assert(xv_benchmark_compare_object_jobs()==(kinds[i]==XV_BENCH_OBJECT_JOBS));
        assert(xv_benchmark_compare_prep_bundle()==(kinds[i]==XV_BENCH_PREP_BUNDLE));
        assert(xv_benchmark_compare_guest_phases()==(kinds[i]==XV_BENCH_GUEST_PHASES));
        assert(xv_benchmark_compare_snapshot_worker()==(kinds[i]==XV_BENCH_SNAPSHOT_WORKER));
        assert(xv_benchmark_compare_guest_affinity()==(kinds[i]==XV_BENCH_GUEST_AFFINITY));
        assert(xv_benchmark_compare_flare_query_overlap()==(kinds[i]==XV_BENCH_FLARE_QUERY_OVERLAP));
        assert(xv_benchmark_compare_hle_dispatch()==(kinds[i]==XV_BENCH_HLE_DISPATCH));
        assert(xv_benchmark_compare_object_scan()==(kinds[i]==XV_BENCH_OBJECT_SCAN));
        assert(xv_benchmark_compare_texture_state()==(kinds[i]==XV_BENCH_TEXTURE_STATE));
        assert(xv_benchmark_compare_early_visibility()==(kinds[i]==XV_BENCH_EARLY_VISIBILITY));
        assert(xv_benchmark_compare_object_basis()==(kinds[i]==XV_BENCH_OBJECT_BASIS));
        assert(xv_benchmark_compare_model_palette()==(kinds[i]==XV_BENCH_MODEL_PALETTE));
        assert(xv_benchmark_compare_vertex_worker()==(kinds[i]==XV_BENCH_VERTEX_WORKER));
        assert(xv_benchmark_step(now,360,1,view)==360 && optimization==0);xv_benchmark_applied(now,360);
        xv_benchmark_compare_toggle();assert(xv_benchmark_step(now,360,1,view)==360 && optimization==-1);
        xv_benchmark_applied(now,360);assert(!xv_benchmark_remote_busy() && !remote_kind);
    }
#ifndef TEST_NO_VERTEX_PREPARE
    for(unsigned stop=0;stop<3;stop++) {
        assert(!xv_benchmark_remote_request(XV_BENCH_VERTEX_PREPARE));
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_step(now,480,1,view)==480);
        assert(!strcmp(tag(),"vertex-prepare-compare") && optimization==0);
        xv_benchmark_applied(now,480);
        for(unsigned i=0;i<180;i++) {
            now+=100000;unsigned next=xv_benchmark_step(now,480,1,view);
            if(next)xv_benchmark_applied(now,480);
        }
        assert(optimization==1);
        if(stop) {
            if(stop==1)xv_benchmark_toggle();
            assert(xv_benchmark_step(now,480,stop==1,view)==480);
            xv_benchmark_applied(now,480);
        } else for(unsigned i=0;i<360;i++) {
            now+=100000;unsigned next=xv_benchmark_step(now,480,1,view);
            if(next)xv_benchmark_applied(now,480);
        }
        assert(optimization==-1 && !xv_benchmark_active() && !remote_kind);
    }
    prepare_ready=0;
    unsigned before_prepare=switches;
    assert(!xv_benchmark_remote_request(XV_BENCH_VERTEX_PREPARE));
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(now,360,1,view));
    assert(switches==before_prepare && !xv_benchmark_remote_busy());
    prepare_ready=1;
#endif
#ifndef TEST_NO_FLARE_QUERY_OVERLAP
    const char *keys[]={"XV_FLARE_DEFER","XV_VIS_STALE"};
    for(unsigned i=0;i<2;i++) {
        setenv(keys[i],i?"1":"0",1);
        unsigned previous_switches=switches;
        assert(!xv_benchmark_remote_request(XV_BENCH_FLARE_QUERY_OVERLAP));
        xv_benchmark_remote_poll(1);
        assert(!xv_benchmark_step(now,360,1,view));
        assert(switches==previous_switches && !xv_benchmark_remote_busy() && !b.active);
        unsetenv(keys[i]);
    }
#endif
#ifndef TEST_NO_GUEST_AFFINITY
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_remote_request(XV_BENCH_GUEST_AFFINITY));xv_benchmark_remote_poll(1);
    assert(xv_benchmark_step(now,360,1,view)==360);xv_benchmark_applied(now,360);
    affinity_ok=0;assert(xv_benchmark_step(now,360,1,view)==360);
    assert(optimization==-1);xv_benchmark_applied(now,360);assert(!xv_benchmark_active());affinity_ok=1;
#endif
#ifndef TEST_NO_GUEST_PHASES
    phase_ready=0;assert(xv_benchmark_remote_request(XV_BENCH_GUEST_PHASES)==-1);
    phase_ready=1;assert(!xv_benchmark_remote_request(XV_BENCH_GUEST_PHASES));
    xv_benchmark_remote_poll(1);phase_ready=0;switches=0;
    assert(!xv_benchmark_step(now,360,1,view)&&!switches&&!xv_benchmark_remote_busy());
    phase_ready=1;
#endif
#if !defined(TEST_NO_MATRIX_NEON) && !defined(TEST_NO_OBJECT_SCAN)
    /* Bundle completion and every enabled-phase exit release all overrides.
     * Preserve independently configured member defaults and the established
     * vertex/worker/flare baseline rather than rewriting saved settings. */
    const char *members[]={"XV_NATIVE_MATRIX_NEON","XV_NATIVE_OBJECT_SCAN","XV_TEXTURE_STATE_CACHE"};
    for(unsigned i=0;i<3;i++)setenv(members[i],i==1?"0":"1",1);
    for(unsigned stop=0;stop<3;stop++) {
        unsigned initial_switches=switches;
        assert(!xv_benchmark_remote_request(XV_BENCH_PREP_BUNDLE));
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_step(now,360,1,view)==360 && optimization==0);
        assert(!strcmp(tag(),"prep-bundle-compare"));
        xv_benchmark_applied(now,360);
        unsigned count=stop?181:540;
        for(unsigned i=0;i<count;i++) {
            now+=100000;
            unsigned next=xv_benchmark_step(now,360,1,view);
            if(next)xv_benchmark_applied(now,next);
        }
        if(stop) {
            assert(optimization==1);
            if(stop==1)xv_benchmark_compare_toggle();
            assert(xv_benchmark_step(now,360,stop==1,view)==360);
            xv_benchmark_applied(now,360);
        }
        assert(optimization==-1 && !xv_benchmark_active() && !remote_kind);
        assert(switches-initial_switches==(stop?3u:4u));
        for(unsigned i=0;i<3;i++)assert(!strcmp(getenv(members[i]),i==1?"0":"1"));
    }
    for(unsigned i=0;i<3;i++)unsetenv(members[i]);
    setenv("XV_NATIVE_MATH","0",1);
    unsigned bundle_switches=switches;
    assert(!xv_benchmark_remote_request(XV_BENCH_PREP_BUNDLE));
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(now,360,1,view));
    assert(switches==bundle_switches && !xv_benchmark_active());
    unsetenv("XV_NATIVE_MATH");
    puts("PASS: combined candidate completes, cancels, loses view and restores without rewriting independently configured defaults");
#endif
#ifndef TEST_NO_OBJECT_JOBS
    object_jobs_ready=0;
    unsigned jobs_switches=switches;
    assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_JOBS));
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(now,360,1,view));
    assert(switches==jobs_switches&&!xv_benchmark_active());
    object_jobs_ready=1;
    for(unsigned stop=0;stop<3;stop++) {
        unsigned initial_switches=switches;
        assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_JOBS));
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_step(now,360,1,view)==360&&optimization==0);
        xv_benchmark_applied(now,360);
        for(unsigned i=0;i<(stop?181u:540u);i++) {
            now+=100000;unsigned next=xv_benchmark_step(now,360,1,view);
            if(next)xv_benchmark_applied(now,next);
        }
        if(stop) {
            assert(optimization==1);
            if(stop==1)xv_benchmark_compare_toggle();
            assert(xv_benchmark_step(now,360,stop==1,view)==360);
            xv_benchmark_applied(now,360);
        }
        assert(optimization==-1&&!xv_benchmark_active()&&!remote_kind);
        assert(switches-initial_switches==(stop?3u:4u));
    }
    puts("PASS: object jobs reject failed initialization and restore on completion, cancellation and lost view");
#endif
#ifndef TEST_NO_OBJECT_MATH
    object_math_ready=0;
    unsigned math_switches=switches;
    assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_MATH));
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(now,360,1,view));
    assert(switches==math_switches&&!xv_benchmark_active());
    object_math_ready=1;
    for(unsigned stop=0;stop<3;stop++) {
        unsigned initial_switches=switches;
        assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_MATH));
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_step(now,360,1,view)==360&&optimization==0);
        xv_benchmark_applied(now,360);
        for(unsigned i=0;i<(stop?181u:540u);i++) {
            now+=100000;unsigned next=xv_benchmark_step(now,360,1,view);
            if(next)xv_benchmark_applied(now,next);
        }
        if(stop) {
            assert(optimization==1);
            if(stop==1)xv_benchmark_compare_toggle();
            assert(xv_benchmark_step(now,360,stop==1,view)==360);
            xv_benchmark_applied(now,360);
        }
        assert(optimization==-1&&!xv_benchmark_active()&&!remote_kind);
        assert(switches-initial_switches==(stop?3u:4u));
    }
    puts("PASS: private math rejects inactive workers and restore on completion, cancellation and lost view");
#endif
#ifndef TEST_NO_OBJECT_LOCK
    object_lock_ready=0;
    unsigned lock_switches=switches;
    assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_LOCK));
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(now,360,1,view));
    assert(switches==lock_switches&&!xv_benchmark_active());
    object_lock_ready=1;
    for(unsigned stop=0;stop<3;stop++) {
        unsigned initial_switches=switches;
        assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_LOCK));
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_step(now,360,1,view)==360&&optimization==0);
        xv_benchmark_applied(now,360);
        for(unsigned i=0;i<(stop?181u:540u);i++) {
            now+=100000;unsigned next=xv_benchmark_step(now,360,1,view);
            if(next)xv_benchmark_applied(now,next);
        }
        if(stop) {
            assert(optimization==1);
            if(stop==1)xv_benchmark_compare_toggle();
            assert(xv_benchmark_step(now,360,stop==1,view)==360);
            xv_benchmark_applied(now,360);
        }
        assert(optimization==-1&&!xv_benchmark_active()&&!remote_kind);
        assert(switches-initial_switches==(stop?3u:4u));
    }
    puts("PASS: lightweight lock rejects unavailable backend/workers and restore on completion, cancellation and lost view");
#endif
#ifdef TEST_NO_DEPTH_PREPARE
    assert(xv_benchmark_remote_request(XV_BENCH_DEPTH_PREPARE)==-1);
#else
    depth_ready=0;
    assert(xv_benchmark_remote_request(XV_BENCH_DEPTH_PREPARE)==-1);
    depth_ready=1;
    for(unsigned stop=0;stop<3;stop++) {
        unsigned initial_switches=switches;
        assert(!xv_benchmark_remote_request(XV_BENCH_DEPTH_PREPARE));
        xv_benchmark_remote_poll(1);
        assert(xv_benchmark_step(now,360,1,view)==360&&optimization==0);
        assert(!strcmp(tag(),"depth-prepare-compare"));
        xv_benchmark_applied(now,360);
        for(unsigned i=0;i<(stop?181u:540u);i++) {
            now+=100000;unsigned next=xv_benchmark_step(now,360,1,view);
            if(next)xv_benchmark_applied(now,next);
        }
        if(stop) {
            assert(optimization==1);
            if(stop==1)xv_benchmark_compare_toggle();
            assert(xv_benchmark_step(now,360,stop==1,view)==360);
            xv_benchmark_applied(now,360);
        }
        assert(optimization==-1&&!xv_benchmark_active()&&!remote_kind);
        assert(switches-initial_switches==(stop?3u:4u));
    }
    puts("PASS: depth preparation rejects incompatible modes and restores on completion, cancellation and lost view");
#endif
    snapshot_worker_ready=0;
    assert(!xv_benchmark_remote_request(XV_BENCH_SNAPSHOT_WORKER));xv_benchmark_remote_poll(1);
    unsigned unchanged=switches;
    assert(!xv_benchmark_step(now,360,1,view) && switches==unchanged && !xv_benchmark_active());
    snapshot_worker_ready=1;
    puts("PASS: remote admission, guest-owner consumption, menu rejection, explicit candidate priority, incompatible modes, unavailable builds and cancellation restore");
}
