#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
int xv_phase_enabled;
static int gameplay_ready=1;
int xd3d_object_jobs_ready(void) {return gameplay_ready;}
static unsigned writes[300],active,peak,allocations;
static unsigned shared_guarded_value;
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
#include "object_solver_scope.inc"
#endif
#ifdef XV_NATIVE_MODEL_HIERARCHY
int xv_math_model_hierarchy(xctx *);
void xv_model_hierarchy_override(int);
void xv_model_hierarchy_report(unsigned);
static void hierarchy_job(xctx *c,unsigned id)
{
    /* Each callback owns this guest stack region. Constants are shared read-only.
     * Run outside any outer math scope, exercising the production guard itself. */
    xctx saved=*c;
    unsigned sp=c->r[4]-8192u,model=sp+512u,poses=sp+768u;
    unsigned nodes=sp+1024u,matrices=sp+2560u;
    memset(X_G(sp),0,4096);
    X_M32(model+0xb8)=8;X_M32(model+0xbc)=nodes;
    X_M32(sp+0x24)=matrices;X_M32(sp+0x28)=poses;X_M32(sp+0x2c)=model;
    X_M32(sp+0x10)=2;X_M16(sp+0x178)=0;X_M16(sp+0x17a)=1;
    for(unsigned n=0;n<8;n++) {
        X_M16(nodes+n*156+0x20)=n==7?0xffff:n+1;
        X_M16(nodes+n*156+0x22)=0xffff;
        X_M16(nodes+n*156+0x24)=n?n-1:0xffff;
        X_MF32(poses+n*32+12)=1;X_MF32(poses+n*32+28)=1;
        X_MF32(poses+n*32+16)=(float)id;
    }
    X_MF32(matrices)=X_MF32(matrices+4)=X_MF32(matrices+20)=X_MF32(matrices+36)=1;
    c->r[4]=sp;c->r[0]=1;c->preempt=100;
    assert(xv_math_model_hierarchy(c));
    assert(c->r[0]==7&&c->preempt==94&&X_M32(sp+0x10)==8);
    for(unsigned n=1;n<7;n++) {
        float expected[13]={1,1,0,0,0,1,0,0,0,1,(float)(n*id),0,0};
        assert(!memcmp(X_G(matrices+n*52),expected,sizeof expected));
    }
    for(unsigned n=0;n<13;n++)assert(X_M32(matrices+7*52+n*4)==0);
    *c=saved;
}
#endif
static void nested_guard_return(void)
{
    XV_OBJECT_MATH_GUARD();
#ifdef XV_OBJECT_HOLD_PROFILE
    extern unsigned xv_object_hold_child_begin(int);
    /* A nested callback must not double count the parent's active sample. */
    assert(!xv_object_hold_child_begin(xv_object_math_locked_));
#endif
    unsigned before=shared_guarded_value;
    { XV_OBJECT_MATH_GUARD(); shared_guarded_value=before+1; }
    /* Returning from nested cleanup must leave the caller's lock held. */
    struct timespec delay={0,50000};nanosleep(&delay,NULL);
    assert(shared_guarded_value==before+1);
}
static unsigned event_calls,event_value;
static unsigned event_seen[1200];
static pthread_t guest_owner;
static unsigned io_calls, io_seen[300], audio_calls, volume_calls, commit_calls, stop_calls;
static unsigned frequency_calls, parameter_calls;
void object_test_audio_owner(void)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    /* Reading unguarded worker outputs tests the all-lanes-parked publication. */
    for(unsigned i=0;i<300;i++)io_seen[i]=writes[i];
}
void object_test_audio_pump(xctx *c);
static void submit_audio(xctx *c)
{
    unsigned sp=c->r[4];X_PUSH32(0x29427u);
    xv_object_job_hle(c,0x193E27u,object_test_audio_pump);
    assert(c->r[4]==sp);
    __atomic_add_fetch(&audio_calls,1,__ATOMIC_RELAXED);
}
void object_test_stream_volume(xctx *c);
static void submit_volume(xctx *c,unsigned id)
{
    unsigned sp=c->r[4];
    X_PUSH32(id%3==0 ? -10000 : id%3==1 ? -1026 : 0);
    X_PUSH32(id%4 ? 0x1234 : 0x5678);X_PUSH32(id%4<2 ? 0x2982Fu : 0x292FBu);
    xv_object_job_hle(c,0x193D4Fu,object_test_stream_volume);
    assert(c->r[4]==sp&&c->r[0]==0);
    __atomic_add_fetch(&volume_calls,1,__ATOMIC_RELAXED);
}
void object_test_audio_commit(xctx *c);
static void submit_commit(xctx *c,unsigned id)
{
    unsigned sp=c->r[4];X_PUSH32(0x03D07280);X_PUSH32(id%4<2?0x291EFu:0x28BB6u);
    xv_object_job_hle(c,0x193C1Bu,object_test_audio_commit);
    assert(c->r[4]==sp&&c->r[0]==0);
    __atomic_add_fetch(&commit_calls,1,__ATOMIC_RELAXED);
}
void object_test_stream_status(xctx *c);
void object_test_stream_packet(xctx *c);
static unsigned status_calls,packet_calls;
static void submit_refill(xctx *c,unsigned id)
{
    uint32_t sp=c->r[4],packet=sp-128,done=sp-64,state=sp-60;
    X_PUSH32(state);X_PUSH32(id%4<2?1:0x7654);X_PUSH32(0x28B35);
    xv_object_job_hle(c,0x19384Fu,object_test_stream_status);
    assert(c->r[4]==sp&&c->r[0]==0&&X_M32(state)==1);
    __atomic_add_fetch(&status_calls,1,__ATOMIC_RELAXED);
    X_M32(packet)=0xC0000;X_M32(packet+4)=192000;X_M32(packet+8)=done;
    X_M32(packet+12)=state;X_M32(packet+16)=0xABAB;
    X_M32(done)=0;X_M32(state)=99;
    X_PUSH32(0);X_PUSH32(packet);X_PUSH32(id%4<2?1:0x7654);X_PUSH32(0x289FD);
    xv_object_job_hle(c,0x193884u,object_test_stream_packet);
    assert(c->r[4]==sp&&c->r[0]==0);
    assert(X_M32(state)==(id%4<2?1u:0u));
    assert(X_M32(done)==(id%4<2?0u:192000u));
    __atomic_add_fetch(&packet_calls,1,__ATOMIC_RELAXED);
}
void object_test_voice_stop(xctx *c);
static void submit_stop(xctx *c,unsigned id)
{
    static const uint32_t objects[]={0xB1000,0xB2000,0xB3000,0xB4000,0,0xB5000,0xB6000,0xB7000};
    unsigned sp=c->r[4];X_PUSH32(objects[id%8]);X_PUSH32(0x28745u);
    xv_object_job_hle(c,0x19C5FFu,object_test_voice_stop);
    assert(c->r[4]==sp&&c->r[0]==0);
    __atomic_add_fetch(&stop_calls,1,__ATOMIC_RELAXED);
}
void object_test_stream_frequency(xctx *c);
void object_test_stream_parameter(xctx *c);
static void submit_parameters(xctx *c,unsigned id)
{
    static const unsigned rates[]={0,99,100,22050,44100,96000,192000,192001};
    unsigned sp=c->r[4];
    X_PUSH32(rates[id%8]);X_PUSH32(id%3?0x1234:0x5678);X_PUSH32(0x29898);
    xv_object_job_hle(c,0x194470u,object_test_stream_frequency);
    assert(c->r[4]==sp&&c->r[0]==0);
    __atomic_add_fetch(&frequency_calls,1,__ATOMIC_RELAXED);
    static const unsigned calls[][3]={{0x193D9B,0x298E7,3},{0x193DB3,0x2992A,3},
        {0x193D68,0x2999D,4},{0x193D96,0x299F0,3},{0x193E22,0x2979A,3}};
    for(unsigned j=0;j<5;j++) {
        for(unsigned i=calls[j][2];i>0;i--)X_PUSH32(0x12340000u+i);
        X_PUSH32(calls[j][1]);xv_object_job_hle(c,calls[j][0],object_test_stream_parameter);
        assert(c->r[4]==sp&&c->r[0]==0);
        __atomic_add_fetch(&parameter_calls,1,__ATOMIC_RELAXED);
    }
}
/* Read callback outputs without the math lock: the service protocol must have
 * parked every lane, including one waiting for another worker's held mutex. */
int xk_object_io_step(void)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    for(unsigned i=0;i<300;i++)io_seen[i]=writes[i];
    io_calls++;return 1;
}
static void submit_yield(xctx *c,unsigned caller)
{
    uint32_t sp=c->r[4];X_PUSH32(caller);X_PUSH32(0x12AA9u);
    extern void should_not_call_yield(xctx *);
    xv_object_job_hle(c,0x1D6640u,should_not_call_yield);
    assert(c->r[0]==0&&c->r[4]==sp-4);c->r[4]+=4;
}
void should_not_call_yield(xctx *c) {(void)c;assert(0);}

static void event_service(xctx *c)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    assert(X_M32(c->r[4]+4)==0xfaceu);
    X_M32(X_M32(c->r[4]+8))=event_value++;
    event_calls++;c->r[0]=0x12340000u;c->r[4]+=12;
}
static void submit_event(xctx *c,uint32_t output)
{
    uint32_t sp=c->r[4];X_PUSH32(output);X_PUSH32(0xfaceu);X_PUSH32(0x12ccfu);
    xv_object_job_hle(c,0x1D665Cu,event_service);
    assert(c->r[4]==sp&&c->r[0]==0x12340000u);
    unsigned previous=X_M32(output);assert(previous<1200);
    __atomic_add_fetch(&event_seen[previous],1,__ATOMIC_RELAXED);
}
static unsigned query_calls;
static void resource_query(xctx *c)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    /* Both answers must survive owner dispatch; the bridge cannot hardcode
     * an idle resource or change its one-argument stack convention. */
    c->r[0]=(X_M32(c->r[4]+4)&1);c->r[4]+=8;query_calls++;
}
static void submit_query(xctx *c,unsigned id)
{
    unsigned sp=c->r[4];X_PUSH32(id);X_PUSH32(0x32084u);
    xv_object_job_hle(c,0x184A20u,resource_query);
    assert(c->r[4]==sp&&c->r[0]==(id&1));
}
static unsigned register_calls;
static void register_resource(xctx *c)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    /* No math lock: ThreadSanitizer checks publication from every parked lane. */
    for(unsigned i=0;i<300;i++)io_seen[i]=writes[i];
    unsigned header=X_M32(c->r[4]+4),base=X_M32(c->r[4]+8);
    X_M32(header+4)=(base+X_M32(header+4))&0x03ffffffu;
    X_M32(header)=(X_M32(header)&~0xffffu)|1u;
    c->r[0]=0;c->r[4]+=12;register_calls++;
}
static void submit_register(xctx *c,unsigned id)
{
    unsigned sp=c->r[4],h=0x80000+id*16;
    X_M32(h)=0xaabb0012;X_M32(h+4)=id;
    X_PUSH32(0x80200000);X_PUSH32(h);X_PUSH32(0x3223Fu);
    xv_object_job_hle(c,0x184AB0u,register_resource);
    assert(c->r[0]==0&&c->r[4]==sp&&X_M32(h)==0xaabb0001&&X_M32(h+4)==0x200000+id);
}
static unsigned vertex_lock_calls;
void object_test_d3d_count(const char *name)
{ assert(!strcmp(name,"D3DVertexBuffer_Lock"));vertex_lock_calls++; }
void xv_hle_D3DVertexBuffer_Lock(xctx *);
static void vertex_lock(xctx *c)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    for(unsigned i=0;i<300;i++)io_seen[i]=writes[i];
    assert(X_M32(c->r[4]+12)==64&&X_M32(c->r[4]+20)==0x80);
    xv_hle_D3DVertexBuffer_Lock(c);
}
static void submit_vertex_lock(xctx *c,unsigned id)
{
    unsigned sp=c->r[4],header=0x90000+id*8;
    X_M32(header+4)=0x123000;X_PUSH32(0);unsigned output=c->r[4];
    X_PUSH32(0x80);X_PUSH32(output);X_PUSH32(64);X_PUSH32(id*64);X_PUSH32(header);X_PUSH32(0x116240);
    xv_object_job_hle(c,0x1858D0u,vertex_lock);
    assert(c->r[0]==0&&c->r[4]==sp-4&&X_M32(output)==0x80123000+id*64);
    c->r[4]+=4;
}
int xv_math_point_transform(xctx *c);
uint64_t xk_os_monotonic_us(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
void xk_os_log(const char *fmt,...)
{ va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a); }
uint32_t xk_mem_alloc(uint32_t size,uint32_t align,uint32_t low,uint32_t high,int top)
{ (void)align;(void)low;(void)high;(void)top;assert(size==XV_OBJECT_JOB_STACK_BYTES);return 0x100000+(allocations++)*XV_OBJECT_JOB_STACK_BYTES; }
int xk_mem_free(uint32_t a) {assert(a>=0x100000&&a<0x100000+3*XV_OBJECT_JOB_STACK_BYTES);return 1;}
void f_0008FB70(xctx *c)
{
    assert(xv_is_object_job(c));assert(c->r[1]<300);
    unsigned n=__atomic_add_fetch(&active,1,__ATOMIC_SEQ_CST),p=__atomic_load_n(&peak,__ATOMIC_SEQ_CST);
    while(n>p&&!__atomic_compare_exchange_n(&peak,&p,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {}
    /* Simulate substantial work while exercising nested native-helper locks. */
    struct timespec delay={0,1000000};nanosleep(&delay,NULL);
    unsigned id=c->r[1];
#ifdef XV_OBJECT_HOLD_PROFILE
    extern unsigned xv_object_motion_begin(xctx *,unsigned);
    extern void xv_object_motion_end(unsigned *);
    assert(!xv_object_motion_begin(c,1)); /* no held sampled child */
#endif
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
    solver_fixture_scope(c);
#endif
#ifdef XV_NATIVE_MODEL_HIERARCHY
    hierarchy_job(c,id);
#endif
    {
        XV_OBJECT_MATH_GUARD();
#ifdef XV_OBJECT_HOLD_PROFILE
        extern unsigned xv_object_hold_child_begin(int);
        extern void xv_object_hold_child_end(unsigned,unsigned);
        unsigned child_sample=xv_object_hold_child_begin(xv_object_math_locked_);
        if(child_sample)assert(!xv_object_hold_child_begin(xv_object_math_locked_));
        xctx snapshot=*c;
        unsigned motion0=xv_object_motion_begin(c,0);
        assert(!!motion0==!!child_sample);
        assert(!xv_object_motion_begin(c,0)); /* same-function recursion */
        assert(!xv_object_motion_begin(&snapshot,1)); /* not the live context */
        assert(!xv_object_motion_begin(c,6)); /* invalid site */
        assert(!memcmp(c,&snapshot,sizeof snapshot));
#endif
        unsigned before=shared_guarded_value;
        nested_guard_return();
#ifdef XV_OBJECT_HOLD_PROFILE
        xv_object_motion_end(&motion0);assert(!motion0);
        xv_object_hold_child_end(child_sample,8);
        unsigned service_sample=xv_object_hold_child_begin(xv_object_math_locked_);
        assert(child_sample==service_sample);
        unsigned motion1=xv_object_motion_begin(c,1);
        unsigned motion2=xv_object_motion_begin(c,2);
        unsigned motion3=xv_object_motion_begin(c,3);
        assert(!!motion1==!!service_sample&&!!motion2==!!motion1&&!!motion3==!!motion1);
#endif
        c->r[0]=0x60000+id*16;c->r[1]=0x30000;c->r[2]=0x50000+id*12;
        X_PUSH32(0x123456u);
        assert(xv_math_point_transform(c));
        assert(X_MF32(0x60000+id*16)==(float)id);
        assert(X_MF32(0x60004+id*16)==1.0f);
        assert(X_MF32(0x60008+id*16)==2.0f);
        { XV_OBJECT_MATH_GUARD(); writes[id]++; }
#ifdef XV_OBJECT_HOLD_PROFILE
        xv_object_motion_end(&motion3);
        unsigned motion4=xv_object_motion_begin(c,4);
#endif
        /* Hold the shared callback lock while parking for a real owner service.
         * The owner must not run a job that blocks on this same mutex. */
        submit_event(c,0x70000+id*8);
        if(id%3==0)submit_yield(c,0x32B60u);
        submit_query(c,id);
        if(id%2==0)submit_vertex_lock(c,id);
        if(id%2==0)submit_audio(c);
        if(id%2==0)submit_commit(c,id);
        if(id%2==0)submit_refill(c,id);
        if(id%2==0)submit_volume(c,id);
        if(id%2==0)submit_stop(c,id);
        if(id%2==0)submit_parameters(c,id);
        assert(shared_guarded_value==before+1);
#ifdef XV_OBJECT_HOLD_PROFILE
        xv_object_motion_end(&motion4);
        xv_object_motion_end(&motion2);
        unsigned motion5=xv_object_motion_begin(c,5);
        xv_object_motion_end(&motion5);
        xv_object_motion_end(&motion1);
        xv_object_hold_child_end(service_sample,18);
#endif
    }
    /* Requests can also arrive simultaneously, outside the shared lock. */
    submit_event(c,0x70004+id*8);
    if(id%3==1)submit_yield(c,0x3268Au);
    if(id%3==2)submit_register(c,id);
    if(id%3==2)submit_yield(c,0x17A804u);
    if(id%2==1)submit_vertex_lock(c,id);
    if(id%2==1)submit_audio(c);
    if(id%2==1)submit_commit(c,id);
    if(id%2==1)submit_refill(c,id);
    if(id%2==1)submit_volume(c,id);
    if(id%2==1)submit_stop(c,id);
    if(id%2==1)submit_parameters(c,id);
    __atomic_sub_fetch(&active,1,__ATOMIC_SEQ_CST);c->r[4]+=4;
}
int main(int argc,char **argv)
{
    (void)argv;
    guest_owner=pthread_self();
    g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
    solver_fixture_constants();
#endif
#ifdef XV_NATIVE_MODEL_HIERARCHY
    X_M32(0x1f0a68)=0;X_M32(0x1f0a78)=0x3f800000;X_M32(0x1f0b04)=0x40000000;
    xv_model_hierarchy_override(1);
#endif
    X_MF32(0x30000)=1;X_MF32(0x30004)=1;X_MF32(0x30014)=1;X_MF32(0x30024)=1;
    for(unsigned i=0;i<300;i++) {X_MF32(0x50000+i*12)=(float)i;X_MF32(0x50004+i*12)=1;X_MF32(0x50008+i*12)=2;}
    setenv("XV_EXPERIMENTAL_OBJECT_JOBS","0",1);
    xctx c={0};c.r[4]=0x20000;c.fs_base=0x10000;
    if(argc>1&&!strcmp(argv[1],"default-on")) {
        unsetenv("XV_EXPERIMENTAL_OBJECT_JOBS");
        assert(xv_object_jobs_begin(&c));xv_object_jobs_finish(&c);xv_object_jobs_shutdown();
        puts("PASS: dedicated experimental build starts object workers by default");
        free(g_xram);free(g_xpt);return 0;
    }
    assert(!xv_object_jobs_begin(&c));
    xv_object_jobs_override(0);assert(allocations==3);assert(!xv_object_jobs_begin(&c));
    int idle_lock=xv_object_math_lock();
    const char *fast=getenv("XV_OBJECT_LOCK_FAST_PATH");
    assert(idle_lock==((!fast||atoi(fast))?0:1));
    xv_object_math_unlock(&idle_lock);
    xv_object_jobs_override(1);
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
    assert(!xv_object_solver_begin(&c));
    if(!getenv("OBJECT_SOLVER_TEST_OFF"))xv_object_solver_override(1);
#endif
#ifdef XV_OBJECT_HOLD_PROFILE
    assert(!xv_object_holds_enabled());
    if(getenv("OBJECT_HOLD_TEST"))xv_object_holds_override(1);
    assert(xv_object_holds_enabled()==!!getenv("OBJECT_HOLD_TEST"));
#endif
    gameplay_ready=0;assert(!xv_object_jobs_begin(&c));gameplay_ready=1;
    xv_phase_enabled=1;assert(!xv_object_jobs_begin(&c));xv_phase_enabled=0;
    assert(xv_object_jobs_begin(&c));assert(!xv_object_jobs_begin(&c));
    X_M32(c.r[4])=0x902DF;assert(!xv_object_jobs_queue(&c));
    if(argc>1&&!strcmp(argv[1],"unsupported-yield")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0x12AA9;X_M32(c.r[4]+4)=0xDEADBEEF;
        xv_object_job_hle(&c,0x1D6640u,should_not_call_yield);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-vertex-lock")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0xDEADBEEF;
        xv_object_job_hle(&c,0x1858D0u,vertex_lock);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-audio")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0xDEADBEEF;
        xv_object_job_hle(&c,0x193E27u,object_test_audio_pump);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-volume")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0xDEADBEEF;
        xv_object_job_hle(&c,0x193D4Fu,object_test_stream_volume);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-commit")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0xDEADBEEF;
        xv_object_job_hle(&c,0x193C1Bu,object_test_audio_commit);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-stop")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0xDEADBEEF;
        xv_object_job_hle(&c,0x19C5FFu,object_test_voice_stop);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-stop-null")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0x28745;
        xv_object_job_hle(&c,0x19C5FFu,NULL);assert(0);
    }
    if(argc>2&&!strcmp(argv[1],"unsupported-parameter")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0xDEADBEEF;
        xv_object_job_hle(&c,(unsigned)strtoul(argv[2],NULL,16),object_test_stream_parameter);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-stream")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0x28B36;
        xv_call(&c,0x19384Fu);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-stream-process")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0x289FE;
        xv_call(&c,0x193884u);assert(0);
    }
    if(argc>1&&!strcmp(argv[1],"unsupported-nested-audio")) {
        setenv("OBJECT_AUDIO_NESTED_FAIL","1",1);argc=1;
    }
    if(argc>1) {c.fiber=(void *)&xv_object_job_marker;xv_object_job_hle(&c,0x1D66EC,NULL);assert(0);}
    for(unsigned round=0;round<2;round++) {
        if(round)assert(xv_object_jobs_begin(&c));
        for(unsigned i=0;i<300;i++) {
            c.r[1]=i;c.r[0]=0xabcdef88;c.r[4]=0x20000;X_M32(c.r[4])=0x90299;
            assert(xv_object_jobs_queue(&c));assert(c.r[4]==0x20004&&c.r[0]==0xabcdef01);
        }
        xv_object_jobs_join();assert(!active);
        for(unsigned i=0;i<300;i++)assert(writes[i]==round+1);
        /* A report attempted before scope retirement must not clear counters. */
        xv_object_jobs_report(999);
        xctx *scope=&c;xv_object_jobs_end(&scope);
    }
    const char *workers=getenv("XV_OBJECT_JOB_WORKERS");
    unsigned expected=workers?(unsigned)atoi(workers):2u;if(!expected)expected=1;
    assert(peak==expected);
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
    int solver_expected=(!workers||atoi(workers)!=0)&&(!fast||atoi(fast))&&!getenv("OBJECT_HOLD_TEST")&&
        !getenv("OBJECT_SOLVER_TEST_OFF")&&!getenv("OBJECT_SOLVER_TEST_BAD_CONSTANT");
    assert(!solver_running&&solver_entered==(solver_expected?600u:0u));
    if(solver_expected)assert(solver_peak==expected);
    xv_object_solver_override(-1);
    printf("PASS: solver boundary entered %u scopes; peak %u private solves; admission, cleanup, owner parking\n",solver_entered,solver_peak);
#endif
    assert(vertex_lock_calls==600);assert(register_calls==200);assert(query_calls==600);assert(event_calls==1200&&event_value==1200);assert(io_calls>=300&&io_calls<=600);
    assert(audio_calls==600&&volume_calls==600&&commit_calls==600&&stop_calls==600);
    assert(status_calls==600&&packet_calls==600);
    assert(frequency_calls==600&&parameter_calls==3000);
    assert(shared_guarded_value==600);
    for(unsigned i=0;i<1200;i++)assert(event_seen[i]==1);
    xv_object_jobs_override(-1);assert(!xv_object_jobs_begin(&c));
#ifdef XV_NATIVE_MODEL_HIERARCHY
    xv_model_hierarchy_override(0);
    xv_model_hierarchy_report(3);xv_model_hierarchy_report(3);
#endif
    xv_object_jobs_report(3);xv_object_jobs_report(3);xv_object_jobs_shutdown();
    printf("PASS: 600 callbacks and native point transforms exactly once; 600 quiescent vertex locks, 200 registrations, 600 resource queries, 1,200 owner-thread events and 600 cache yields (including 200 preload yields) with quiescent owner I/O reads, results and stack cleanup, with/without shared locks; peak %u jobs, overflow joins and restore\n",peak);
    free(g_xram);free(g_xpt);
}
