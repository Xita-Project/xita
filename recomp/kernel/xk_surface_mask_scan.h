#pragma once
#include "../xv_x86rt.h"

/* Experimental entry at 53ED0, ordinary immutable scene RAM only. Skip a
 * bounded run of unset visibility bits; retain the last bit and every yield
 * in the original loop. Not installed in production. Captured mapping roots
 * must match the containing lift. All observable context/scratch is retained. */
static inline unsigned xv_surface_mask_zero_run(xctx *c, uint8_t *arena,
                                                const uint32_t *pages,
                                                const uint8_t *root_slot)
{
    uint32_t bit=c->r[1], index=c->r[2], sp=c->r[4];
    if(bit>=30 || index>0x7fffffffu || c->preempt<=2 ||
       sp>0xffffffe3u || ((sp+0x18u)&4095)>4088) return 0;
    uint32_t root,mask_address,count,mask;
    memcpy(&root,root_slot,4);
    if(root>0xffffff07u || ((root+0xf8u)&4095)>4092) return 0;
    const uint8_t *bound=arena+pages[(root+0xf8u)>>12]+((root+0xf8u)&4095);
    memcpy(&count,bound,4);
    if((int32_t)count<=(int32_t)index) return 0;
    uint8_t *scratch=arena+pages[(sp+0x18u)>>12]+((sp+0x18u)&4095);
    uint32_t counter; memcpy(&counter,scratch,4);
    if(counter!=bit) return 0; /* a handoff may have changed either value */
    memcpy(&mask_address,scratch+4,4);
    if((mask_address&4095)>4092) return 0;
    const uint8_t *bits=arena+pages[mask_address>>12]+(mask_address&4095);
    memcpy(&mask,bits,4);
    if((mask>>bit)&3u) return 0; /* no useful zero run: avoid remaining admission work */
    /* Scratch writes must not change a subsequently reloaded input, including
     * aliases through different guest pages or the flat image mapping. */
    uintptr_t w=(uintptr_t)scratch;
    const uint8_t *inputs[]={bound,bits,root_slot};
    for(unsigned i=0;i<3;i++) {
        uintptr_t p=(uintptr_t)inputs[i];
        if(w<p+4 && p<w+4) return 0;
    }
    unsigned limit=31-bit;
    if(limit>count-index) limit=count-index;
    if(limit>(unsigned)c->preempt-1) limit=(unsigned)c->preempt-1;
    unsigned n=0;
    while(n<limit && !(mask&(1u<<(bit+n)))) n++;
    if(n<2) return 0;
    uint32_t next=bit+n;
    c->r[0]=root; c->r[3]=1u<<(next-1);
    c->r[1]=next; c->r[2]+=n; c->r[6]+=6*n;
    memcpy(scratch,&next,4);
    c->preempt-=(int32_t)n;
    X_FLAGS(XK_SUB,next,32,(uint16_t)(next-32),16);
    return n;
}
