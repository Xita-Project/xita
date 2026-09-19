/* Shared-owner ledger and invalid-admission checks for ordered pass timing. */
#define main main_partition_fixture
#include "scene_partition.c"
#undef main
uint64_t xv_scene_draw_completed_us;
static void reconcile1(void)
{
    uint64_t sum=0;for(unsigned i=0;i<12;i++)sum+=detail1.elapsed[i];
    assert(sum==scene.elapsed[1]);
}
static void *foreign1(void *arg)
{
    uint64_t token=*(uint64_t*)arg;
    for(unsigned k=0;k<2;k++) {
        on_worker=k;
        for(unsigned i=0;i<1000;i++)xv_scene_bucket1_step(&token,&first.ctx,1);
    }
    return NULL;
}
int main(int argc,char **argv)
{
    main_partition_fixture(argc,argv);
    if(!atoi(argv[1]))return 0;
    memset(&scene,0,sizeof scene);memset(&detail1,0,sizeof detail1);
    reads=clock_reads=0;clock_value=1000;clear_log();xctx before=first.ctx;
    uint64_t token=0;xv_scene_partition_begin(&token,&first.ctx);
    uint64_t old=reads;xv_scene_bucket1_step(&token,&first.ctx,1);assert(reads==old);
    xv_scene_partition_step(&token,&first.ctx,1);
    clock_value+=10;
    for(unsigned i=1;i<12;i++) {xv_scene_bucket1_step(&token,&first.ctx,i);clock_value+=10;}
    xv_scene_partition_step(&token,&first.ctx,2);reconcile1();
    assert(detail1.completed==1 && scene.elapsed[1]==120);
    for(unsigned i=0;i<12;i++)assert(detail1.entries[i]==1 && detail1.elapsed[i]==10);
    xv_scene_partition_end(&token);
    xv_owner_phase_report(60);clear_log();
    xv_scene_partition_begin(&token,&first.ctx);xv_scene_partition_step(&token,&first.ctx,1);
    clock_value+=10;xv_scene_bucket1_step(&token,&first.ctx,5);
    clock_value+=5;old=reads;xv_owner_phase_report(60);assert(reads==old+1);
    assert(strstr(output,"entries 1/0/0/0/0/1/0/0/0/0/0/0 elapsed-us 10/0/0/0/0/5/0/0/0/0/0/0; completed 0 open 1"));
    reconcile1();clock_value+=5;xv_scene_partition_end(&token);reconcile1();
    assert(detail1.completed==1 && detail1.elapsed[5]==5);
    xv_scene_partition_begin(&token,&first.ctx);xv_scene_partition_step(&token,&first.ctx,1);
    old=reads;pthread_t thread;assert(!pthread_create(&thread,NULL,foreign1,&token));assert(!pthread_join(thread,NULL));assert(reads==old);
    xv_scene_bucket1_step(&token,&first.ctx,12);xv_scene_bucket1_step(&token,&first.ctx,0);assert(reads==old);
    first.state=1;xv_scene_bucket1_step(&token,&first.ctx,1);first.state=0;assert(reads==old);
    uint64_t stale_token=token;select_owner(&second);select_owner(&first);
    token=0;xv_scene_partition_begin(&token,&first.ctx);xv_scene_partition_step(&token,&first.ctx,1);old=reads;
    xv_scene_bucket1_step(&stale_token,&first.ctx,1);assert(reads==old && scene.token==token);
    clock_value--;xv_scene_bucket1_step(&token,&first.ctx,1);reconcile1();
    xv_scene_partition_end(&token);reconcile1();
    /* Existing draw clocks, cumulative counter, report split and reset guards. */
    memset(&scene,0,sizeof scene);memset(&detail1,0,sizeof detail1);
    memset(&draw_detail1,0,sizeof draw_detail1);clear_log();
    xv_scene_draw_completed_us=1000;clock_value=2000;
    xv_scene_partition_begin(&token,&first.ctx);
    xv_scene_partition_step(&token,&first.ctx,1);
    for(unsigned i=1;i<12;i++) {
        clock_value+=10;xv_scene_draw_completed_us+=3;
        xv_scene_bucket1_step(&token,&first.ctx,i);
    }
    clock_value+=10;xv_scene_draw_completed_us+=3;
    xv_scene_partition_end(&token);
    for(unsigned i=0;i<12;i++)assert(draw_detail1.elapsed[i]==3);
    assert(!draw_detail1.invalid);
    old=reads;xv_owner_phase_report(60);assert(reads==old);
    assert(strstr(output,"completed-draw-us 3/3/3/3/3/3/3/3/3/3/3/3; available 1 invalid 0"));
    clear_log();xv_scene_partition_begin(&token,&first.ctx);
    xv_scene_partition_step(&token,&first.ctx,1);
    clock_value+=10;xv_scene_draw_completed_us+=4;xv_owner_phase_report(60);
    assert(strstr(output,"completed-draw-us 4/0/0/0/0/0/0/0/0/0/0/0; available 1 invalid 0"));
    clock_value+=10;xv_scene_draw_completed_us+=2;
    xv_scene_bucket1_step(&token,&first.ctx,5);
    assert(draw_detail1.elapsed[0]==2);
    clock_value+=10;xv_scene_draw_completed_us=0;
    xv_scene_bucket1_step(&token,&first.ctx,6);assert(draw_detail1.invalid==1);
    clock_value+=10;xv_scene_draw_completed_us=11;
    xv_scene_bucket1_step(&token,&first.ctx,7);assert(draw_detail1.invalid==2);
    clock_value+=10;xv_scene_draw_completed_us+=1;
    xv_scene_partition_end(&token);assert(draw_detail1.elapsed[7]==1);
    assert(!memcmp(&before,&first.ctx,sizeof before));
    puts("PASS bucket1 detail: ordered/skipped cuts, main-ledger reconciliation, split reports, early cleanup, foreign worker, invalid/live state, stale generation, backwards clock");
}
