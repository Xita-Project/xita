#define main placement_fixture_main
#include "visibility_placement.c"
#undef main
static unsigned notification_count,notification_cmd,notification_ui;
static SceGxmNotification captured;
void test_query_notification(const SceGxmNotification *f)
{notification_count++;assert(notification_count==1);captured=*f;notification_cmd=draws;notification_ui=uis;}
#define XV_VISIBILITY_GPU_CORES 4u
#define XV_VISIBILITY_WORDS (4u*512u)
static xv_visibility_result g_visibility_results[512];
static uint32_t gpu_memory[3*XV_VISIBILITY_WORDS],*g_visibility_memory=gpu_memory;
static unsigned notifications;
void xk_os_scheduler_notify(void) {notifications++;}
#define XV_VP_COMPLETE(l,f) ((void)0)
#include "query_complete.inc"
static void begin_case(void)
{
    reset(0);memset(g_query_boundary_plans,0,sizeof g_query_boundary_plans);
    memset(g_query_boundary_reasons,0,sizeof g_query_boundary_reasons);
    g_query_boundary_attached=g_query_boundary_failed=0;
    notification_count=notification_cmd=notification_ui=0;memset(&captured,0,sizeof captured);
}
static void execute(int on,int expect)
{
    cmdlist_t *l=&lists[0];before=*l;
    volatile unsigned query_word=0xABCDE;
    SceGxmNotification fence={&query_word,0}; /* zero ticket is valid on wrap */
    int eligible=xv_d3d_query_boundary_prepare(0,on);
    assert(eligible==(on && expect));
    if(eligible)xv_d3d_query_boundary_arm(0,&fence);
    assert(!memcmp(l,&before,sizeof *l));
    SceGxmDepthStencilSurface depth={0};
    int err=xv_d3d_render_targets(NULL,0,&targets[0],NULL,NULL,&depth,960,544);
    if(err>=0) {
        assert(notification_count==(unsigned)eligible);
        if(eligible) {
            unsigned last=0;
            for(unsigned i=0;i<l->ncmds;i++)if(!l->cmds[i].kind && l->cmds[i].visibility)last=i;
            assert(notification_cmd>last && notification_cmd==g_query_boundary_plans[0].command);
            assert(notification_ui==g_query_boundary_plans[0].ui);
            assert(captured.address==&query_word && captured.value==0 && query_word==0xABCDE);
            /* The word is written by simulated fragment completion, never by
             * the planner or CPU EndScene callback. It remains live to final. */
            *captured.address=captured.value;assert(query_word==0);
            assert(!qb_notification(0,notification_cmd,notification_ui));
        }
        sceGxmEndScene(NULL,NULL,NULL);
    }
    assert(!memcmp(l,&before,sizeof *l));
}
static void build(unsigned scenario)
{
    begin_case();cmdlist_t *l=&lists[0];slot(l,0,5,11);slot(l,1,5,12);
    if(scenario==0) {cmd(l,0,1,6);cmd(l,1,0,9);cmd(l,0,0,12);ui(l,1,0);ui(l,2,1);}
    if(scenario==1) {cmd(l,0,1,6);cmd(l,1,0,9);cmd(l,0,2,12);} /* later view */
    if(scenario==2) {cmd(l,1,1,0);} /* zero-index potential writer, empty final */
    if(scenario==3) {cmd(l,1,1,6);ui(l,1,2);ui(l,1,0);}
    if(scenario==4) {cmd(l,1,1,6);cmd(l,0,1,12);} /* same slot writes again */
    if(scenario==5) {cmd(l,0,1,6);cmd(l,1,2,12);cmd(l,0,0,9);} /* repeated ID */
    if(scenario==6) {l->nvisibility=0;cmd(l,0,0,6);}
    if(scenario==7) {cmd(l,0,0,6);} /* query slots but no writers: fallback */
    if(scenario==8) {cmd(l,0,1,6);l->cmds[0].kind=1;} /* tagged clear */
}
static void exact_results(void)
{
    xv_visibility_result reference[512];
    for(unsigned variant=0;variant<4;variant++)for(unsigned on=0;on<=1;on++) {
        build(5);cmdlist_t *l=&lists[0];memset(g_visibility_results,0,sizeof g_visibility_results);
        uint16_t result;uint32_t serial[2];
        if(variant==3) { /* exact history across serial wrap, zero is skipped */
            g_visibility_results[0].used=1;g_visibility_results[0].id=5;
            g_visibility_results[0].issued=UINT32_MAX-1u;
        }
        g_visibility_memory=variant==2?NULL:gpu_memory;
        l->visibility_gpu_ready=variant!=1;
        for(unsigned i=0;i<2;i++) {
            assert(!xv_visibility_issue(g_visibility_results,5,&result,&serial[i]));
            l->visibility[i].result_slot=result;l->visibility[i].serial=serial[i];
            l->visibility[i].guest_area=l->visibility[i].render_area=960*544;
        }
        /* An issued query with no recorded draws must publish exact zero; an
         * unissued/error slot is ignored exactly as the original completer. */
        uint32_t zero_serial;assert(!xv_visibility_issue(g_visibility_results,777,&result,&zero_serial));
        slot(l,2,result,zero_serial);slot(l,3,0,0);
        memset(gpu_memory,0,sizeof gpu_memory);notifications=0;
        for(unsigned core=0;core<4;core++) {
            gpu_memory[core*512]=10+core;gpu_memory[core*512+1]=20+core;
        }
        if(variant==3)assert(serial[0]==UINT32_MAX && serial[1]==1);
        execute(on,1);
        uint32_t pixels=999;
        assert(xv_visibility_read_generation(g_visibility_results,5,serial[0],&pixels)==XV_VISIBILITY_INCOMPLETE);
        clock_us=100;complete_exact(0);assert(notifications==1);
        assert(!xv_visibility_read_generation(g_visibility_results,5,serial[0],&pixels)&&pixels==((variant==1 || variant==2)?0u:46u));
        assert(!xv_visibility_read_generation(g_visibility_results,5,serial[1],&pixels)&&pixels==((variant==1 || variant==2)?0u:86u));
        assert(!xv_visibility_read_generation(g_visibility_results,777,zero_serial,&pixels)&&!pixels);
        assert(xv_visibility_read_generation(g_visibility_results,99999,1,&pixels)==XV_VISIBILITY_INVALID_ARGUMENT);
        if(!on)memcpy(reference,g_visibility_results,sizeof reference);
        else assert(!memcmp(reference,g_visibility_results,sizeof reference));
    }
    g_visibility_memory=gpu_memory;
}
int main(void)
{
    const int eligible[]={1,0,1,1,0,1,0,0,0};
    for(unsigned scenario=0;scenario<9;scenario++) {
        uint64_t observed[2];
        for(int on=0;on<=1;on++) {build(scenario);trace=0;execute(on,eligible[scenario]);observed[on]=trace;}
        assert(observed[0]==observed[1]); /* only added fragment notification differs */
    }
    for(unsigned fault=0;fault<3;fault++) {
        build(0);if(fault==0)fail_begin=2;if(fault==1)fail_end=1;if(fault==2)g_rt[0].rt=NULL;
        if(fault==2)assert(!xv_d3d_query_boundary_prepare(0,1));
        else execute(1,1);
        if(fault==1)assert(g_query_boundary_failed==1 && !notification_count);
    }
    /* Declines inspect the complete packet even after an otherwise valid cut. */
    for(unsigned bad=0;bad<9;bad++) {
        build(0);cmdlist_t *l=&lists[0];
        if(bad==0)l->ncmds=2049;if(bad==1)l->nvisibility=513;if(bad==2)l->nui=1025;
        if(bad==3)l->active_visibility=1;if(bad==4)l->cmds[2].visibility=3;
        if(bad==5)l->visibility[0].serial=0;if(bad==6)l->cmds[2].kind=2;
        if(bad==7)ui(l,0,0);if(bad==8)l->visibility[1].result_slot=512;
        before=*l;assert(!xv_d3d_query_boundary_prepare(0,1));assert(!memcmp(l,&before,sizeof *l));
    }
    build(0);assert(xv_d3d_query_boundary_prepare(0,1));volatile unsigned word=~123u;
    SceGxmNotification f={&word,123};xv_d3d_query_boundary_arm(0,&f);
    /* A later OFF packet in the same retired slot cancels the old plan. */
    assert(!xv_d3d_query_boundary_prepare(3,0));assert(!qb_notification(0,1,0));assert(!qb_notification(3,1,0));
    /* Maximum legal storage, with a last writer at the last recorded draw;
     * the existing final RTT-to-backbuffer transition is still admissible. */
    begin_case();
    for(unsigned i=0;i<512;i++)slot(&lists[0],i,i,i+1);
    for(unsigned i=0;i<2048;i++)cmd(&lists[0],1,i%512+1,3);
    for(unsigned u=0;u<1024;u++)ui(&lists[0],u*2,1);
    execute(1,1);assert(notification_cmd==2048 && notification_ui==1024);
    /* Frame identities can wrap while storage remains indexed by list slot. */
    build(0);assert(UINT32_MAX%XV_NUM_LISTS==0);
    assert(xv_d3d_query_boundary_prepare(UINT32_MAX,1));
    xv_d3d_query_boundary_arm(UINT32_MAX,&f);
    assert(!qb_notification(0,1,0));
    assert(qb_notification(UINT32_MAX,g_query_boundary_plans[0].command,g_query_boundary_plans[0].ui));
    assert(!xv_d3d_query_boundary_prepare(0,0));
    assert(!qb_notification(UINT32_MAX,1,0));
    exact_results();
    /* Result-ID pressure retains original OUT_OF_MEMORY without mutation. */
    memset(g_visibility_results,0,sizeof g_visibility_results);
    uint16_t result_slot;uint32_t issued;
    for(unsigned id=0;id<512;id++)assert(!xv_visibility_issue(g_visibility_results,id,&result_slot,&issued));
    xv_visibility_result full[512];memcpy(full,g_visibility_results,sizeof full);
    assert(xv_visibility_issue(g_visibility_results,90000,&result_slot,&issued)==XV_VISIBILITY_OUT_OF_MEMORY);
    assert(!memcmp(full,g_visibility_results,sizeof full));
    build(0);execute(1,1);xv_d3d_query_boundary_report();
    assert(strstr(report,"packets off/ready/no-query/no-writer/no-boundary 0/1/0/0/0"));
    assert(strstr(report,"existing-EndScene attached/failed 1/0"));
    assert(!g_query_boundary_attached && !g_query_boundary_reasons[QB_READY]);
    puts("PASS: actual RTT notification placement, OFF/ON trace identity, later/reused writers, exact zero/error/history results, failures, fallback and plan reset");
}
