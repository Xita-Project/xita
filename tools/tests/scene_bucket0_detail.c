/* Reuse the existing admission/cleanup cases, then exercise the shared ledger. */
#define main main_partition_fixture
#include "scene_partition.c"
#undef main
static void detailed_route(int shortcut,int nested,int early)
{
    uint64_t token __attribute__((cleanup(xv_scene_partition_end)))=0;
    xv_scene_partition_begin(&token,&first.ctx);
    clock_value+=10;
    if(nested)detailed_route(0,0,0);
    xv_scene_bucket0_step(&token,&first.ctx,1);clock_value+=10;
    if(early)return;
    if(!shortcut)for(unsigned i=2;i<6;i++) {
        xv_scene_bucket0_step(&token,&first.ctx,i);clock_value+=10;
    }
    for(unsigned i=shortcut?5:1;i<6;i++) {
        xv_scene_partition_step(&token,&first.ctx,i);clock_value+=10;
    }
}
static void reconcile(void)
{
    uint64_t total=0;for(unsigned i=0;i<6;i++)total+=detail.elapsed[i];
    assert(total==scene.elapsed[0]);
}
static void *detail_foreign(void *arg)
{
    uint64_t token=*(uint64_t*)arg;
    for(unsigned k=0;k<2;k++) {
        on_worker=k;
        for(unsigned i=0;i<1000;i++)xv_scene_bucket0_step(&token,&first.ctx,1);
    }
    return NULL;
}
int main(int argc,char **argv)
{
    main_partition_fixture(argc,argv);
    if(!atoi(argv[1]))return 0;
    memset(&scene,0,sizeof scene);memset(&detail,0,sizeof detail);
    reads=clock_reads=0;clock_value=1000;clear_log();
    xctx before=first.ctx;
    detailed_route(0,0,0);assert(reads==12 && detail.completed==1);
    assert(scene.elapsed[0]==60 && scene.entries[0]==1);
    for(unsigned i=0;i<6;i++)assert(detail.entries[i]==1 && detail.elapsed[i]==10);
    reconcile();uint64_t old=reads;
    detailed_route(1,0,0);assert(reads==old+4 && detail.completed==2);reconcile();
    assert(detail.entries[0]==2 && detail.entries[1]==2 && detail.entries[2]==1);
    old=reads;detailed_route(0,1,0);assert(reads==old+12 && scene.recursive==1);reconcile();
    old=reads;detailed_route(0,0,1);assert(reads==old+3 && detail.completed==4);reconcile();
    xv_owner_phase_report(60);clear_log();
    uint64_t token=0;
    clock_value=2000;xv_scene_partition_begin(&token,&first.ctx);
    clock_value=2010;xv_scene_bucket0_step(&token,&first.ctx,1);
    clock_value=2015;old=reads;xv_owner_phase_report(60);assert(reads==old+1);
    assert(strstr(output,"[scene-bucket0-detail] 60 frames 5D410: entries 1/1/0/0/0/0 elapsed-us 10/5/0/0/0/0; completed 0 open 1"));
    assert(strstr(output,"[scene-partition] 60 frames 5D410: entries 1/0/0/0/0/0 elapsed-us 15/0/0/0/0/0"));
    reconcile();clear_log();
    clock_value=2020;xv_scene_bucket0_step(&token,&first.ctx,2);
    clock_value=2030;xv_scene_partition_step(&token,&first.ctx,1);reconcile();
    assert(detail.elapsed[1]==5 && detail.elapsed[2]==10 && detail.completed==1);
    old=reads;unsigned invalid_before=scene.invalid;
    xv_scene_bucket0_step(&token,&first.ctx,3);
    assert(reads==old && scene.invalid==invalid_before+1); /* outside bucket0 */
    xv_scene_partition_end(&token);reconcile();
    xv_scene_partition_begin(&token,&first.ctx);old=reads;
    pthread_t thread;assert(!pthread_create(&thread,NULL,detail_foreign,&token));assert(!pthread_join(thread,NULL));
    assert(reads==old);
    xv_scene_bucket0_step(&token,&first.ctx,6);xv_scene_bucket0_step(&token,&first.ctx,0);
    assert(reads==old);
    first.state=1;xv_scene_bucket0_step(&token,&first.ctx,1);first.state=0;assert(reads==old);
    uint64_t stale_token=token;select_owner(&second);select_owner(&first);
    token=0;xv_scene_partition_begin(&token,&first.ctx);old=reads;
    xv_scene_bucket0_step(&stale_token,&first.ctx,1);assert(reads==old && scene.token==token);
    clock_value--;xv_scene_bucket0_step(&token,&first.ctx,1);reconcile();
    xv_scene_partition_end(&token);reconcile();
    assert(!memcmp(&before,&first.ctx,sizeof before));
    puts("PASS bucket0 detail: exact main-ledger reconciliation, 12 normal / 4 shortcut clocks, early/nested cleanup, split reports, foreign worker, invalid/live state, generation and backwards clock");
}
