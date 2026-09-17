/* Real emitted primary 5D410 CFG; child/HLE bodies are deterministic stand-ins.
 * This tests observer placement and callback-frontier state, not game logic. */
#define main boundary_fixture_main
#include "scene_partition.c"
#undef main
#include <fenv.h>
static unsigned char arena[8*1024*1024], initial[sizeof arena], expected[sizeof arena];
static uint32_t pages[1<<20];
uint8_t *g_xram=arena,*g_img_base=arena;
uint32_t *g_xpt=pages;
static unsigned event_count,lane;
static struct { uint32_t pc; xctx context; uint64_t hash; } events[1024];
static uint64_t bytes_hash(const void *p,size_t n)
{ const unsigned char *s=p;uint64_t h=0xcbf29ce484222325ull;while(n--)h=(h^*s++)*0x100000001b3ull;return h; }
static void callback(xctx *c,uint32_t pc,unsigned pop)
{
    assert(c==&first.ctx && event_count<1024);
    uint64_t hash=bytes_hash(arena,sizeof arena)^bytes_hash(pages,sizeof pages);
    if(!lane) { events[event_count].pc=pc;events[event_count].context=*c;events[event_count].hash=hash; }
    else assert(events[event_count].pc==pc && !memcmp(&events[event_count].context,c,sizeof *c) && events[event_count].hash==hash);
    event_count++;clock_value+=10;
    /* Observable callee effects at each original boundary. */
    c->scratch^=pc;arena[0x600000+(pc&255)]^=(unsigned char)pc;
    c->r[4]+=pop;
}
static void fixture_dispatch(xctx *c,uint32_t pc) {callback(c,pc,4);}
static void fixture_preempt(xctx *c) {callback(c,0xdead0001,0);c->preempt=2;}
#define XV_PHASE_SCOPE(c,id) ((void)0)
#define XV_HLE_CALL(pc,fn) callback(c,pc,28)
#undef X_PREEMPT
#define X_PREEMPT() do {if(--c->preempt<=0)fixture_preempt(c);}while(0)
#define xv_call fixture_dispatch
#include "scene_bodies.inc"
#undef xv_call
int main(void)
{
    uint8_t *const xram_=g_xram,*const imgb_=g_img_base;const uint32_t *const xpt_=g_xpt;
    xv_owner_phase_configure();select_owner(&first);clear_log();
    for(unsigned i=0;i<(1u<<20);i++)pages[i]=(i&2047u)*4096;
    unsigned compared=0,observed=0;
    for(unsigned skip=0;skip<2;skip++)for(unsigned extra=0;extra<2;extra++)
    for(unsigned indirect=0;indirect<3;indirect++)for(unsigned count=0;count<2;count++) {
        memset(arena,0,sizeof arena);memset(&first.ctx,0x5a,sizeof first.ctx);
        first.ctx.fiber=NULL;first.ctx.df=0;first.ctx.f_kind=XK_EXPLICIT;
        first.ctx.f_res=0x246;first.ctx.f_cf_override=1;first.ctx.f_cf=1;
        first.ctx.f_of_override=1;first.ctx.f_of=0;first.ctx.f_bits=32;
        first.ctx.r[4]=0x700000;first.ctx.preempt=1;
        xctx *c=&first.ctx;
        X_M32(c->r[4]+4)=7;
        for(unsigned i=2;i<=5;i++)X_M32(c->r[4]+4*i)=0x500000+0x400*i;
        X_M32(c->r[4]+24)=9;
        X_IMG32(0x2E3198)=skip;X_IMG8(0x2D2B80)=extra;
        X_IMG8(0x1F869C)=extra;X_IMG8(0x2E3190)=extra;
        X_IMG16(0x30BE0C)=count?3:0;
        X_IMG32(0x2F9110)=indirect?0x510000:0;
        X_M32(0x510030)=indirect==2?0xbeef0001:0;
        xctx input=first.ctx;memcpy(initial,arena,sizeof arena);
        event_count=0;lane=0;f_scene_original(c);xctx result=*c;
        unsigned original_events=event_count;memcpy(expected,arena,sizeof arena);
        memcpy(arena,initial,sizeof arena);*c=input;lane=1;event_count=0;
        uint64_t prior=reads;memset(&scene,0,sizeof scene);f_scene_observed(c);
        assert(reads-prior==(skip?3:7) && scene.completed==1 && !scene.token);
        assert(scene.entries[0]==1 && scene.entries[5]==1 && scene.entries[1]==!skip);
        assert(!scene.invalid && !scene.stale && !scene.recursive);
        assert(event_count==original_events && !memcmp(c,&result,sizeof result) && !memcmp(arena,expected,sizeof arena));
        observed+=event_count;compared++;
    }
    printf("PASS %u real primary-CFG cases, %u matching callback/preempt frontiers; complete context/8MiB memory/table hashes; children stubbed, no gameplay claim\n",compared,observed);
}
