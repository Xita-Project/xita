#define XV_FLARE_QUERY_OVERLAP 1
#include "recomp/kernel/xk_flare.c"
#include <assert.h>
#include <stdio.h>

uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
volatile uint32_t xv_cur_fn=0x60560;
static xv_visibility_result results[XV_VISIBILITY_IDS];
static uint32_t old_serial[3];
static unsigned old_waits, generation_waits, notifications;
static uint64_t now=1;
void xk_os_log(const char *fmt,...) { (void)fmt; }
uint64_t xk_os_monotonic_us(void) { return ++now; }
void xk_os_scheduler_notify(void) { notifications++; }
int xk_wait_u32(const uint32_t *word,uint32_t value,uint64_t timeout)
{ (void)word;(void)value;(void)timeout;assert(!"unexpected competing guest fiber");return 0; }
void xk_sleep_us(uint64_t delay) { (void)delay;assert(!"unexpected poll"); }
void xk_yield(void) { assert(!"unexpected yield"); }
uint32_t xd3d_r_visibility_result(uint32_t id,uint32_t *pixels)
{ return xv_visibility_read(results,id,pixels); }
uint32_t xd3d_r_visibility_generation(uint32_t id)
{ return xv_visibility_generation(results,id); }
uint32_t xd3d_r_visibility_result_generation(uint32_t id,uint32_t serial,uint32_t *pixels)
{ return xv_visibility_read_generation(results,id,serial,pixels); }
int xd3d_r_visibility_wait(uint32_t id,uint32_t timeout,uint32_t *age,uint32_t *elapsed)
{
    (void)age;(void)elapsed;assert(timeout==100000);old_waits++;
    xv_visibility_publish(results,id,results[id].issued,128);return 1;
}
int xd3d_r_visibility_wait_generation(uint32_t id,uint32_t serial,uint32_t timeout)
{
    assert(id<3 && serial==old_serial[id] && timeout==100000);
    assert(results[id].issued!=serial); /* New ID generation was recorded. */
    generation_waits++;
    xv_visibility_publish(results,id,serial,128);return 1;
}
static xctx seed(int wrap)
{
    memset(g_xram,0xa5,4u<<20);memset(results,0,sizeof results);
    X_IMG32(FLARE_COUNT)=3;
    for(unsigned i=0;i<3;i++) {
        uint32_t row=FLARE_LIST+i*40u;uint16_t slot;
        X_IMG16(row+0x1e)=0x8000;X_IMG16(row+0x20)=i;
        X_IMG8(row+0x22)=1;X_IMG32(row+0x24)=255;
        assert(!xv_visibility_issue(results,i,&slot,&old_serial[i]));
        if(wrap) {
            results[slot].issued=UINT32_MAX-1u;
            assert(!xv_visibility_issue(results,i,&slot,&old_serial[i]));
        }
    }
    xctx c={0};c.r[4]=0x3e1100;c.r[5]=0x3e2300;
    X_M32(c.r[4])=0x8022a;X_M32(c.r[4]+4)=0x5dc0c;X_M16(c.r[5]+0xc)=1;
    c.preempt=10000;c.fcw=0x37f;
    return c;
}
static void replacement_queries(void)
{
    for(unsigned i=0;i<3;i++) {
        xv_flare_barrier(XV_FLARE_QUERY);
        uint16_t slot;uint32_t serial;
        assert(!xv_visibility_issue(results,i,&slot,&serial));
        assert(serial!=old_serial[i]);
        xv_flare_barrier(XV_FLARE_QUERY);
    }
}
int main(void)
{
    g_xram=malloc(4u<<20);g_img_base=g_xram;g_xpt=calloc(1u<<20,4);
    assert(g_xram&&g_xpt);for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
    const char *e=getenv("XV_FLARE_QUERY_OVERLAP");int configured=e&&atoi(e)!=0;
    xctx c=seed(0);assert(xv_flare_defer(&c));assert(pending_overlap==configured);
    for(unsigned i=0;i<3;i++)xv_visibility_publish(results,i,old_serial[i],128);
    xv_flare_barrier(XV_FLARE_PRESENT);assert(!pending);
    /* Every remaining dependency must consume the retained old generation,
     * even if the option changes while the work is pending. */
    const unsigned reasons[]={XV_FLARE_NEXT,XV_FLARE_BRIGHTNESS,XV_FLARE_IDENTITY,XV_FLARE_RESET,XV_FLARE_PRESENT};
    for(unsigned run=0;run<100;run++) {
        c=seed(run&1);xctx before=c;unsigned old=old_waits,gen=generation_waits;
        xv_flare_query_overlap_override(1);assert(xv_flare_defer(&c));
        before.r[4]+=4;assert(!memcmp(&before,&c,sizeof c));
        assert(pending_overlap && !X_IMG32(FLARE_COUNT));
        xv_flare_query_overlap_override(0);
        replacement_queries();assert(pending==3 && generation_waits==gen && old_waits==old);
        for(unsigned i=0;i<3;i++)assert(X_IMG8(FLARE_OBJECT+i*4+1)==0xa5);
        if(run%3==0)for(unsigned i=0;i<3;i++)xv_visibility_publish(results,i,old_serial[i],128);
        xv_flare_barrier(XV_FLARE_COLLECTION);assert(pending==3);
        xv_cur_fn=0x606b0;xv_flare_barrier(reasons[run%5]);assert(xv_cur_fn==0x606b0);
        assert(!pending && !pending_overlap && old_waits==old);
        assert(generation_waits==gen+(run%3?3:0));
        for(unsigned i=0;i<3;i++) {
            assert(X_IMG8(FLARE_OBJECT+i*4+1)==146); /* (165+128)/2 */
            uint32_t value=0xdeadbeef;
            assert(xd3d_r_visibility_result(i,&value)==XV_VISIBILITY_INCOMPLETE && value==0xdeadbeef);
            /* Only after the Present barrier could the new frame complete. */
            xv_visibility_publish(results,i,results[i].issued,255);
            assert(!xd3d_r_visibility_result(i,&value)&&value==255);
        }
    }
    /* Explicit off restores the old first-query barrier. */
    c=seed(0);xv_flare_query_overlap_override(0);assert(xv_flare_defer(&c));
    unsigned old=old_waits;xv_flare_barrier(XV_FLARE_QUERY);assert(!pending && old_waits==old+3);
    xv_flare_query_overlap_override(-1);c=seed(0);assert(xv_flare_defer(&c));assert(pending_overlap==configured);
    for(unsigned i=0;i<3;i++)xv_visibility_publish(results,i,old_serial[i],128);
    xv_flare_barrier(XV_FLARE_PRESENT);
    /* Missing generation falls back before changing guest context or memory. */
    c=seed(0);results[2].used=0;xctx before=c;xv_flare_query_overlap_override(1);
    assert(!xv_flare_defer(&c)&&!memcmp(&c,&before,sizeof c)&&X_IMG32(FLARE_COUNT)==3&&!pending);
    assert(notifications>=103);
    puts("PASS: retained exact generations survive query reuse; all dependency barriers, serial wrap, late completion, override restoration and missing-generation fallback");
    free(g_xpt);free(g_xram);
}
