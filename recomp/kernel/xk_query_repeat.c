#include "xk_query_repeat.h"
#include <string.h>

static uint32_t hash_word(uint32_t h,uint32_t word)
{return (h^word)*16777619u;}
static uint32_t hash_pointer(uint32_t h,uintptr_t word)
{
    h=hash_word(h,(uint32_t)word);
#if UINTPTR_MAX > UINT32_MAX
    h=hash_word(h,(uint32_t)(word>>32));
#endif
    return h;
}
uint32_t xv_query_repeat_hash(const XvQueryRepeatKey *k)
{
    uint32_t h=2166136261u;
    h=hash_pointer(h,k->arena);h=hash_pointer(h,k->pages);h=hash_pointer(h,k->image);
    h=hash_word(h,k->bsp);h=hash_word(h,k->filter_bits);
    h=hash_word(h,k->filter);h=hash_word(h,k->point);
    for(unsigned i=0;i<3;i++)h=hash_word(h,k->center[i]);
    h=hash_word(h,k->radius);h=hash_word(h,k->zero);
    for(unsigned i=0;i<6;i++)h=hash_word(h,k->selector[i]);
    for(unsigned i=0;i<24;i++)h=hash_word(h,k->geometry[i]);
    h=hash_word(h,k->fcw);
    return hash_word(h,k->fp_control);
}
static int equal_key(const XvQueryRepeatKey *a,const XvQueryRepeatKey *b)
{
    /* Do not hash or compare struct padding. In particular root pointers can
     * be64-bit in the host fixture and32-bit on the device. */
    return a->arena==b->arena&&a->pages==b->pages&&a->image==b->image&&
        a->bsp==b->bsp&&a->filter_bits==b->filter_bits&&a->filter==b->filter&&
        a->point==b->point&&a->center[0]==b->center[0]&&a->center[1]==b->center[1]&&
        a->center[2]==b->center[2]&&a->radius==b->radius&&a->fcw==b->fcw&&
        a->fp_control==b->fp_control&&a->zero==b->zero&&
        !memcmp(a->selector,b->selector,sizeof a->selector)&&
        !memcmp(a->geometry,b->geometry,sizeof a->geometry);
}
void xv_query_repeat_init(XvQueryRepeat *s)
{memset(s,0,sizeof *s);s->epoch=1;}
void xv_query_repeat_invalidate(XvQueryRepeat *s)
{
    /* used excludes stale slots; no old pointer is ever dereferenced. */
    s->used=s->next=0;s->counts.invalidations++;
}
void xv_query_repeat_advance_epoch(XvQueryRepeat *s)
{
    s->counts.epochs++;
    if(++s->epoch==0){xv_query_repeat_invalidate(s);s->epoch=1;}
}
static void count_tier(XvQueryRepeatTier *tier,unsigned within,unsigned prior)
{
    if(within)tier->within_epoch++;
    else if(prior)tier->prior_epoch_only++;
    else tier->misses++;
}
void xv_query_repeat_observe(XvQueryRepeat *s,const XvQueryRepeatKey *key,
    const unsigned char filter[XV_QUERY_REPEAT_FILTER_BYTES])
{
    s->counts.calls++;
    if(!key){s->counts.invalid++;return;}
    s->counts.valid++;
    if(filter)s->counts.filter_valid++;else s->counts.filter_unavailable++;
    uint32_t hash=xv_query_repeat_hash(key);
    unsigned same=0,prior=0,filter_same=0,filter_prior=0;
    for(unsigned i=0;i<s->used;i++) {
        const XvQueryRepeatEntry *e=&s->entries[i];
        if(e->hash!=hash||!equal_key(&e->key,key))continue;
        if(e->epoch==s->epoch)same=1;else prior=1;
        if(filter&&e->filter_valid&&!memcmp(e->filter,filter,XV_QUERY_REPEAT_FILTER_BYTES)) {
            if(e->epoch==s->epoch)filter_same=1;else filter_prior=1;
        }
        if(same&&(!filter||filter_same))break;
    }
    count_tier(&s->counts.input,same,prior);
    if(filter)count_tier(&s->counts.filter,filter_same,filter_prior);
    XvQueryRepeatEntry *e=&s->entries[s->next];
    e->key=*key;e->hash=hash;e->epoch=s->epoch;e->filter_valid=filter!=NULL;
    if(filter)memcpy(e->filter,filter,XV_QUERY_REPEAT_FILTER_BYTES);
    if(s->used<XV_QUERY_REPEAT_ENTRIES)s->used++;else s->counts.evictions++;
    s->next=(s->next+1u)%XV_QUERY_REPEAT_ENTRIES;
}
void xv_query_repeat_take(XvQueryRepeat *s,XvQueryRepeatCounts *out)
{*out=s->counts;memset(&s->counts,0,sizeof s->counts);}
