/* Nested primary model timer: owner admission and split-report accounting. */
#define main main_partition_fixture
#include "scene_partition.c"
#undef main
static void open_model(uint64_t *scene_scope,uint64_t *model_scope)
{
    xv_scene_partition_begin(scene_scope,&first.ctx);
    xv_scene_bucket0_step(scene_scope,&first.ctx,3);
    xv_scene_model_begin(model_scope,&first.ctx);
}
int main(int argc,char **argv)
{
    main_partition_fixture(argc,argv);
    if(!atoi(argv[1])) {
        uint64_t t=0;uint64_t before_reads=reads;
        xv_scene_model_begin(&t,&first.ctx);xv_scene_model_step(&t,&first.ctx,1);xv_scene_model_end(&t);
        assert(!t && reads==before_reads);return 0;
    }
    memset(&scene,0,sizeof scene);memset(&detail,0,sizeof detail);
    memset(&model_detail,0,sizeof model_detail);clear_log();
    uint64_t s=0,m=0,nested=0;clock_value=1000;
    xctx before=first.ctx;open_model(&s,&m);assert(m);
    uint64_t r=reads;xv_scene_model_begin(&nested,&first.ctx);assert(!nested && reads==r);
    for(unsigned i=1;i<4;i++) {clock_value+=10;xv_scene_model_step(&m,&first.ctx,i);}
    clock_value+=10;xv_scene_model_end(&m);assert(!m && model_detail.completed==1);
    for(unsigned i=0;i<4;i++)assert(model_detail.elapsed[i]==10);
    xv_scene_partition_end(&s);xv_owner_phase_report(60);clear_log();
    open_model(&s,&m);clock_value+=7;xv_owner_phase_report(60);
    assert(strstr(output,"elapsed-us 7/0/0/0 completed 0 open 1 invalid 0"));clear_log();
    clock_value+=3;xv_scene_model_step(&m,&first.ctx,1);
    assert(model_detail.elapsed[0]==3);
    r=reads;on_worker=1;xv_scene_model_step(&m,&first.ctx,2);on_worker=0;
    assert(reads==r);xv_scene_model_step(&m,&first.ctx,4);assert(reads==r);
    clock_value--;xv_scene_model_step(&m,&first.ctx,2);assert(scene.invalid);
    xv_scene_model_end(&m);xv_scene_partition_end(&s);
    xv_owner_phase_report(60);clear_log();open_model(&s,&m);
    uint64_t stale_model=m;select_owner(&second);select_owner(&first);
    s=m=0;open_model(&s,&m);r=reads;xv_scene_model_end(&stale_model);
    assert(reads==r && model_detail.token==m);
    xv_scene_model_end(&m);xv_scene_partition_end(&s);
    xv_owner_phase_report(60);clear_log();open_model(&s,&m);
    xv_scene_model_step(&m,&first.ctx,1);xv_scene_model_step(&m,&first.ctx,2);
    uint64_t child=0;clock_value+=5;
    xv_scene_model_child_begin(&child,&first.ctx,1);assert(child);
    uint64_t ignored=0;r=reads;
    xv_scene_model_child_begin(&ignored,&first.ctx,2);assert(!ignored && reads==r);
    clock_value+=10;xv_owner_phase_report(60);
    assert(strstr(output,"elapsed-us 5/10/0/0 calls 1/0/0 child-open 1"));clear_log();
    clock_value+=7;xv_scene_model_child_end(&child);
    assert(model_detail.child_elapsed[1]==7 && !model_detail.child);
    for(unsigned k=2;k<4;k++) {
        xv_scene_model_child_begin(&child,&first.ctx,k);clock_value+=k;
        xv_scene_model_child_end(&child);
    }
    clock_value+=4;xv_scene_model_step(&m,&first.ctx,3);
    uint64_t sum=0;for(unsigned k=0;k<4;k++)sum+=model_detail.child_elapsed[k];
    assert(sum==model_detail.elapsed[2]);
    r=reads;xv_scene_model_child_begin(&child,&first.ctx,1);assert(!child && reads==r);
    xv_scene_model_end(&m);xv_scene_partition_end(&s);
    assert(!memcmp(&before,&first.ctx,sizeof before));
    puts("PASS model timer boundaries, splits, invalid clocks, owner rebind, worker rejection");
    return 0;
}
