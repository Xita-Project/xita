/* Production RTT replay/planner and diagnostic state; GPU notifications are
 * independently delayed by this fixture. No production ownership is mocked by
 * the observer: trace comparison ignores only the added notification argument. */
#include "scene_replay_fixture.inc"
#define SCE_OK 0
static volatile unsigned words[56];
static SceGxmNotification saved[4096];
static const SceGxmNotification *saved_pointer[4096];
static unsigned nsaved;
#ifdef XV_SCENE_CENSUS
static unsigned enabled=1;
#endif
static SceGxmNotification final_fence,world_fence;
static void test_query_notification(const SceGxmNotification *f)
{ assert(nsaved<4096);saved_pointer[nsaved]=f;saved[nsaved++]=*f; }
typedef struct { SceGxmContext *ctx; void *scaled_target; } xv_gfx_t;
static int xv_gfx_upscale(xv_gfx_t *g,unsigned ui,const SceGxmNotification *f)
{
    (void)g;(void)ui;assert(f==&final_fence);
    assert(!sceGxmBeginScene(NULL,0,&targets[0],NULL,NULL,NULL,NULL,NULL));
#ifdef XV_SCENE_CENSUS
    xv_sc_open(9,0,0);
    f=xv_sc_end(9,UINT32_MAX,UINT32_MAX,f,SC_FINAL);
#endif
    int result=sceGxmEndScene(NULL,NULL,f);
#ifdef XV_SCENE_CENSUS
    xv_sc_ended(result);
#endif
    return result;
}
#include "scene_end.inc"
static void reset(unsigned ticket)
{
    memset(lists,0,sizeof lists);memset(g_rt,0,sizeof g_rt);
    for(unsigned i=0;i<8;i++){g_rt[i].rt=&targets[i+1];g_rt[i].w=128*(i+1);g_rt[i].h=64;}
    opened=ends=begins=draws=uis=finishes=fail_begin=fail_end=nsaved=0;
    clock_us=100;report_n=report_lines=0;report[0]=0;
    final_fence=(SceGxmNotification){words+(ticket&3),ticket};
    world_fence=(SceGxmNotification){words+4+(ticket&3),ticket};
    *final_fence.address=*world_fence.address=~ticket;
#ifdef XV_SCENE_CENSUS
    memset(g_sc_packets,0,sizeof g_sc_packets);memset(&g_sc_totals,0,sizeof g_sc_totals);g_sc_current=NULL;
    xv_sc_init(words,enabled?56:0);
#endif
}
static void cmd(cmdlist_t *l,unsigned pass,unsigned query,unsigned indices)
{assert(l->ncmds<XV_MAX_CMDS);l->cmds[l->ncmds++]=(cmd_t){0,pass,query,indices};}
static void ui(cmdlist_t *l,unsigned at,unsigned pass)
{assert(l->nui<1024);unsigned n=l->nui++;l->ui[n].before=at;l->ui[n].target=pass;l->ui[n].batch=n;}
static void signal_all(void)
{for(unsigned i=0;i<nsaved;i++)*saved[i].address=saved[i].value;}
static void run(unsigned ticket,int scaled,int early,int expected)
{
    unsigned frame=ticket%3;cmdlist_t *l=g_lists[frame];before=*l;
    int boundary=xv_d3d_query_boundary_prepare(frame,early);
    if(boundary)xv_d3d_query_boundary_arm(frame,&world_fence);
#ifdef XV_SCENE_CENSUS
    xv_sc_begin(ticket,frame,clock_us,0,1);xv_d3d_scene_census_plan(frame,960,544,scaled);
    xv_sc_packet *p=&g_sc_packets[ticket&3];
#endif
    SceGxmDepthStencilSurface depth={0};
    int result=xv_d3d_render_targets(NULL,frame,&targets[0],NULL,NULL,&depth,960,544,0);
    xv_gfx_t gfx={NULL,scaled?(void*)1:NULL};
    if(!result)result=xv_gfx_end_scenes(&gfx,0,&final_fence,early&&!boundary?&world_fence:NULL);
    assert((result<0)==(expected<0));assert(!memcmp(l,&before,sizeof before));
#ifdef XV_SCENE_CENSUS
    xv_sc_submitted(result<0);
    if(enabled && !result && !p->reason) {
        assert(nsaved==p->count && p->ended==p->count && !p->open);
        assert(saved_pointer[nsaved-1]==&final_fence);
        for(unsigned i=0;i<nsaved;i++) {
            assert(saved[i].value==ticket);
            assert(*saved[i].address!=ticket); /* Includes stale matching words. */
            assert(p->scenes[i].attached);
        }
        if(boundary) {
            unsigned reused=0;
            for(unsigned i=0;i<nsaved;i++)if(p->scenes[i].source==SC_QUERY) {assert(saved_pointer[i]==&g_query_boundary_plans[frame].fence);reused++;}
            assert(reused==1);
        }
        clock_us=200;xv_sc_observe(ticket-1,ticket);
        for(unsigned i=0;i<nsaved;i++)assert(!p->scenes[i].ready && p->scenes[i].negative);
        /* Only the first scene completes; final ownership has not retired. */
        *saved[0].address=ticket;clock_us=300;xv_sc_observe(ticket-1,ticket);
        assert(p->live && p->scenes[0].ready);
        if(nsaved>1)assert(!p->scenes[nsaved-1].ready);
    }
#endif
    signal_all();clock_us=500;
#ifdef XV_SCENE_CENSUS
    xv_sc_observe(ticket-1,ticket);xv_sc_fold(ticket,result<0);
    assert(!p->live);
    if(result<0)assert(g_sc_totals.failed==1);
    else if(enabled && p->count<=12)assert(g_sc_totals.valid==1 && !g_sc_totals.missing && !g_sc_totals.invalid);
    else if(enabled)assert(g_sc_totals.reasons[SC_OVER_CAP]==1);
    else assert(g_sc_totals.reasons[SC_NO_WORDS]==1);
#endif
}
static void cases(void)
{
    for(unsigned scaled=0;scaled<2;scaled++)for(unsigned early=0;early<2;early++) {
        reset(UINT32_MAX);cmdlist_t *l=g_lists[UINT32_MAX%3];
        l->nvisibility=2;l->visibility[0].serial=1;l->visibility[1].serial=2; /* reused result ID */
        cmd(l,0,1,3);cmd(l,0,2,6);cmd(l,1,0,9);ui(l,2,0);ui(l,3,2);cmd(l,0,0,12);
        run(UINT32_MAX,scaled,early,0);
        reset(0);run(0,scaled,early,0); /* empty packet and wrapping ticket */
    }
    reset(7);cmdlist_t *l=g_lists[7%3];l->nvisibility=2;l->visibility[0].serial=1;l->visibility[1].serial=2;
    cmd(l,0,1,3);cmd(l,1,0,6);cmd(l,2,2,9);cmd(l,0,0,12);run(7,0,1,0); /* later view moves query fence */
    reset(5);for(unsigned i=0;i<11;i++)cmd(g_lists[5%3],1+i%2,0,3);run(5,0,0,0); /* exact cap12 */
    reset(6);for(unsigned i=0;i<12;i++)cmd(g_lists[0],1+i%2,0,3);run(6,0,0,0); /* cap13, attach none */
    for(unsigned error=0;error<3;error++) {
        reset(1);cmd(g_lists[1],0,0,3);cmd(g_lists[1],1,0,6);
        if(error==0)fail_begin=2;else if(error==1)fail_end=1;else g_rt[0].rt=NULL;
        run(1,0,0,-1);
    }
    unsigned seed=1;
    for(unsigned trial=0;trial<500;trial++) {
        reset(trial);cmdlist_t *l=g_lists[trial%3];
        for(unsigned j=0;j<8;j++) {seed=1664525u*seed+1013904223u;cmd(l,(seed>>8)%3,0,seed);if(seed&1)ui(l,j,(seed>>12)%3);}
        run(trial,trial&1,0,0);
    }
}
#ifdef XV_SCENE_CENSUS
static void isolated_packet(unsigned ticket,uint64_t begin,unsigned count)
{
    xv_sc_begin(ticket,0,begin,0,1);
    for(unsigned i=0;i<count;i++){xv_sc_shape s={0};s.target=i;s.command=i+1;xv_sc_plan(&s,UINT32_MAX);}
    xv_sc_planned();
    for(unsigned i=0;i<count;i++){
        xv_sc_open(i,0,1);const SceGxmNotification *f=xv_sc_end(i,i+1,0,NULL,SC_ADDED);
        assert(f);xv_sc_ended(0);
    }
    xv_sc_submitted(0);
}
static void lifetime(void)
{
    enabled=1;reset(1);isolated_packet(1,100,2);isolated_packet(2,100,2);isolated_packet(3,100,2);
    xv_sc_packet *older=&g_sc_packets[1],*younger=&g_sc_packets[2];
    for(unsigned i=0;i<2;i++)*younger->scenes[i].fence.address=2;
    clock_us=200;xv_sc_observe(0,3);
    assert(!older->scenes[0].ready && younger->scenes[0].ready);
    assert(older->live && younger->live && !g_sc_totals.retired); /* no younger retirement */
    /* Report reset cannot erase live retained brackets or their words. */
    xv_sc_report();assert(younger->live && younger->scenes[0].upper==201);
    /* Illegal slot reuse declines, leaves the old generation untouched. */
    xv_sc_packet copy=*older;xv_sc_begin(5,0,300,0,1);assert(!g_sc_current);
    assert(!memcmp(&copy,older,sizeof copy) && g_sc_totals.orphans==1);
    /* Missing signal is a diagnostic exclusion, never a new retirement wait. */
    xv_sc_fold(1,0);assert(g_sc_totals.missing==1 && !older->live);
    xv_sc_fold(2,0);assert(g_sc_totals.valid==1);
    xv_sc_fold(3,1);assert(g_sc_totals.failed==1);
    /* New generation initializes every owned word, including UINT wrap. */
    isolated_packet(5,400,2);for(unsigned i=0;i<2;i++)assert(*g_sc_packets[1].scenes[i].fence.address==~5u);
    /* Cancellation: a suspended pending packet remains live, no reuse/free. */
    assert(g_sc_packets[1].live);xv_sc_fold(5,1);
    reset(UINT32_MAX);isolated_packet(UINT32_MAX,UINT64_MAX-100,2);
    clock_us=UINT64_MAX-50;xv_sc_observe(UINT32_MAX-1,UINT32_MAX);
    for(unsigned i=0;i<2;i++)*g_sc_packets[3].scenes[i].fence.address=UINT32_MAX;
    clock_us=20;xv_sc_observe(UINT32_MAX-1,UINT32_MAX);xv_sc_fold(UINT32_MAX,0);
    assert(g_sc_totals.valid==1 && g_sc_totals.row[0].lower==50 && g_sc_totals.row[0].upper==122);
    reset(1);isolated_packet(1,100,1);clock_us=99;xv_sc_observe(0,1);xv_sc_fold(1,0);
    assert(g_sc_totals.reasons[SC_MISMATCH]==1);
    reset(1);isolated_packet(1,100,1);*g_sc_packets[1].scenes[0].fence.address=1;clock_us=200;xv_sc_observe(0,1);xv_sc_fold(1,0);
    isolated_packet(2,300,1);*g_sc_packets[2].scenes[0].fence.address=2;clock_us=400;xv_sc_observe(1,2);
    g_sc_totals.row[0].upper=UINT64_MAX;xv_sc_fold(2,0);assert(g_sc_totals.invalid==1 && g_sc_totals.valid==1);
}
static void malformed(void)
{
    for(unsigned which=0;which<7;which++) {
        reset(1);cmdlist_t *l=g_lists[1];cmd(l,0,0,3);
        unsigned reason=SC_BOUNDS;
        if(which==0)l->ncmds=XV_MAX_CMDS+1;
        if(which==1)l->nui=1025;
        if(which==2){l->cmds[0].kind=2;reason=SC_KIND;}
        if(which==3){l->cmds[0].pass=9;reason=SC_TARGET;}
        if(which==4){ui(l,2,0);reason=SC_UI;}
        if(which==5){ui(l,1,0);ui(l,0,0);reason=SC_UI;}
        if(which==6){ui(l,0,9);reason=SC_TARGET;}
        before=*l;xv_sc_begin(1,1,100,0,1);xv_d3d_scene_census_plan(1,960,544,0);
        assert(g_sc_current->reason==reason && !memcmp(&before,l,sizeof before));
    }
    reset(1);cmdlist_t *l=g_lists[1];
    for(unsigned i=0;i<2048;i++)cmd(l,1+i%2,0,UINT32_MAX);
    for(unsigned i=0;i<1024;i++)ui(l,0,3+i%2);
    xv_sc_begin(1,1,100,0,1);xv_d3d_scene_census_plan(1,960,544,1);
    assert(g_sc_current->reason==SC_OVER_CAP && g_sc_current->count==3074);
    reset(1);for(unsigned i=8;i<56;i++)words[i]=0x12345678;
    for(unsigned i=0;i<12;i++)cmd(g_lists[1],1+i%2,0,3);
    xv_sc_begin(1,1,100,0,1);xv_d3d_scene_census_plan(1,960,544,0);
    for(unsigned i=8;i<56;i++)assert(words[i]==0x12345678);
}
#endif
int main(void)
{
    cases();printf("scene-replay-trace=%016llx\n",(unsigned long long)trace);
#ifdef XV_SCENE_CENSUS
    lifetime();malformed();enabled=0;reset(1);run(1,0,0,0);
    puts("PASS production replay/planner: cap, pointer identity, delayed/younger/coalesced/missing signals, wrap, failure, retained ownership");
#endif
}
