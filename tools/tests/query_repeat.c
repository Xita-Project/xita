#include "xk_query_repeat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static XvQueryRepeatKey key(void)
{
    XvQueryRepeatKey k={.arena=0x100000,.pages=0x200000,.image=0x300000,
        .bsp=0x400000,.filter_bits=256,.filter=0x500001,.point=0x600000,
        .center={0x3f800000,0x40000000,0x40400000},.radius=0x3f000000,
        .fcw=0x23f,.fp_control=0x03000000};
    for(unsigned i=0;i<6;i++)k.selector[i]=i;
    for(unsigned i=0;i<24;i++)k.geometry[i]=0x700000+4096*i;
    return k;
}
static void totals(const XvQueryRepeatCounts *c)
{
    assert(c->calls==c->valid+c->invalid);
    assert(c->valid==c->filter_valid+c->filter_unavailable);
    assert(c->valid==c->input.within_epoch+c->input.prior_epoch_only+c->input.misses);
    assert(c->filter_valid==c->filter.within_epoch+c->filter.prior_epoch_only+c->filter.misses);
}
static void owned_and_invalid(void)
{
    XvQueryRepeat s;XvQueryRepeatKey k=key(),saved=k;unsigned char bits[32]={0},saved_bits[32]={0};
    bits[0]=saved_bits[0]=0x55;
    xv_query_repeat_init(&s);
    xv_query_repeat_observe(&s,NULL,bits);
    assert(s.used==0&&s.counts.invalid==1);
    xv_query_repeat_observe(&s,&k,bits);
    memset(&k,0,sizeof k);memset(bits,0,sizeof bits);
    xv_query_repeat_observe(&s,&saved,saved_bits);
    assert(s.counts.input.within_epoch==1&&s.counts.filter.within_epoch==1);
    assert(s.counts.input.misses==1&&s.counts.filter.misses==1);totals(&s.counts);
}
static void field_change(XvQueryRepeatKey original,XvQueryRepeatKey changed)
{
    XvQueryRepeat s;unsigned char bits[32]={0};xv_query_repeat_init(&s);
    xv_query_repeat_observe(&s,&original,bits);xv_query_repeat_observe(&s,&changed,bits);
    assert(s.counts.input.misses==2&&s.counts.filter.misses==2);totals(&s.counts);
}
static void all_key_fields(void)
{
    XvQueryRepeatKey a=key(),b;
#define CHANGE(field) do{b=a;b.field^=1;field_change(a,b);}while(0)
    CHANGE(arena);CHANGE(pages);CHANGE(image);CHANGE(bsp);CHANGE(filter_bits);
    CHANGE(filter);CHANGE(point);CHANGE(radius);CHANGE(fcw);CHANGE(fp_control);CHANGE(zero);
    for(unsigned i=0;i<3;i++)CHANGE(center[i]);
    for(unsigned i=0;i<6;i++)CHANGE(selector[i]);
    for(unsigned i=0;i<24;i++)CHANGE(geometry[i]);
#if UINTPTR_MAX > UINT32_MAX
    b=a;b.arena^=(uintptr_t)1<<40;field_change(a,b);
    b=a;b.pages^=(uintptr_t)1<<40;field_change(a,b);
    b=a;b.image^=(uintptr_t)1<<40;field_change(a,b);
#endif
#undef CHANGE
    /* Exact bit patterns: no epsilon, signed-zero, NaN or control normalization
     * occurs in the module; caller owns any explicit census masking. */
    b=a;a.center[0]=0;b.center[0]=0x80000000;field_change(a,b);
    a.center[0]=0x7fc00001;b.center[0]=0x7fc00002;field_change(a,b);
}
static uint32_t before_fcw(uint32_t h,const XvQueryRepeatKey *k)
{
    const uint32_t inverse=0x359c449bu;
    assert(inverse*16777619u==1u);
    return ((h*inverse)^k->fp_control)*inverse^k->fcw;
}
static void exact_hash_collision(void)
{
    XvQueryRepeatKey a=key(),b=a;
    b.center[0]^=1;b.geometry[23]=0;
    uint32_t target=before_fcw(xv_query_repeat_hash(&a),&a);
    uint32_t partial=before_fcw(xv_query_repeat_hash(&b),&b);
    b.geometry[23]=(partial*0x359c449bu)^(target*0x359c449bu);
    assert(xv_query_repeat_hash(&a)==xv_query_repeat_hash(&b));
    assert(a.center[0]!=b.center[0]);field_change(a,b);
}
static void epochs_filters_and_report(void)
{
    XvQueryRepeat s;XvQueryRepeatCounts report;XvQueryRepeatKey a=key();
    unsigned char bits[32]={0},other[32]={0};other[31]=1;
    xv_query_repeat_init(&s);
    xv_query_repeat_observe(&s,&a,NULL); /* Valid input; strict unavailable. */
    xv_query_repeat_observe(&s,&a,bits); /* Strict cold despite input hit. */
    assert(s.counts.input.within_epoch==1&&s.counts.filter.misses==1);
    xv_query_repeat_advance_epoch(&s);
    xv_query_repeat_observe(&s,&a,other); /* Input prior-only; strict miss. */
    xv_query_repeat_observe(&s,&a,bits); /* Input same; strict prior-only. */
    assert(s.counts.input.prior_epoch_only==1&&s.counts.input.within_epoch==2);
    assert(s.counts.filter.prior_epoch_only==1&&s.counts.filter.within_epoch==0);
    xv_query_repeat_observe(&s,&a,NULL);
    xv_query_repeat_observe(&s,&a,bits); /* Same takes precedence over prior. */
    assert(s.counts.filter.within_epoch==1&&s.counts.filter.prior_epoch_only==1);
    assert(s.counts.filter_unavailable==2);totals(&s.counts);
    xv_query_repeat_take(&s,&report);totals(&report);assert(s.counts.calls==0&&s.used==6&&s.epoch==2);
    xv_query_repeat_observe(&s,&a,bits);assert(s.counts.input.within_epoch==1&&s.counts.filter.within_epoch==1);
    xv_query_repeat_advance_epoch(&s);xv_query_repeat_observe(&s,&a,bits);
    assert(s.counts.input.prior_epoch_only==1&&s.counts.filter.prior_epoch_only==1);
    xv_query_repeat_invalidate(&s);assert(s.used==0&&s.next==0&&s.counts.invalidations==1);
    xv_query_repeat_observe(&s,&a,bits);assert(s.counts.input.misses==1&&s.counts.filter.misses==1);
    totals(&s.counts);
}
static void eviction_and_wrap(void)
{
    XvQueryRepeat s;XvQueryRepeatKey a=key();unsigned char bits[32]={0};
    xv_query_repeat_init(&s);
    for(unsigned i=0;i<64;i++){a.radius=i;xv_query_repeat_observe(&s,&a,bits);}
    assert(s.used==64&&s.next==0&&s.counts.evictions==0);
    a.radius=63;xv_query_repeat_observe(&s,&a,bits);
    assert(s.counts.input.within_epoch==1&&s.counts.evictions==1);
    a.radius=0;xv_query_repeat_observe(&s,&a,bits); /* Evicted by repeated63. */
    assert(s.counts.input.misses==65&&s.counts.evictions==2);
    a.radius=1;xv_query_repeat_observe(&s,&a,bits);assert(s.counts.input.misses==66);
    totals(&s.counts);
    s.epoch=UINT64_MAX;xv_query_repeat_observe(&s,&a,bits);
    xv_query_repeat_advance_epoch(&s);
    assert(s.epoch==1&&s.used==0&&s.counts.invalidations==1);
    uint64_t misses=s.counts.input.misses;xv_query_repeat_observe(&s,&a,bits);
    assert(s.counts.input.misses==misses+1);totals(&s.counts);
    xv_query_repeat_init(&s);assert(s.counts.calls==0&&s.used==0&&s.epoch==1);
}
int main(void)
{
    owned_and_invalid();all_key_fields();exact_hash_collision();epochs_filters_and_report();eviction_and_wrap();
    printf("PASS: owned query keys, all fields/roots/constants, exact hash collision, filter variants/unavailability, epoch tiers, report/lifetime resets, ring eviction and epoch wrap; state %zu bytes\n",sizeof(XvQueryRepeat));
    return 0;
}
