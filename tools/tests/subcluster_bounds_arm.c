/* Data adapter for the private ARM oracle only. This is not production
 * admission: it does not prove owner identity, alias exclusion or lifetimes. */
#include "xv_x86rt.h"
#include "../../recomp/kernel/xk_subcluster_math.h"
void xs_test_bounds(xctx *c)
{
    xs_frustum f;xs_box b;
    x_guest_read(f.plane,c->r[1]+0x78,sizeof(f.plane));
    x_guest_read(&f.enclosing,c->r[1]+0x128,sizeof(f.enclosing));
    x_guest_read(&b,c->r[7],sizeof(b));
    xs_bounds_result r=xs_bounds(&f,&b);
    c->r[0]=(c->r[0]&0xffff0000u)|r.classification;
    c->r[4]+=8;c->preempt-=(int)r.backedges;
}

/* Experimental owner publication in the pinned 52E10 caller only. Inputs and
 * output are disjoint in the fixture; production must establish that fact.
 * Scratch EAX/ECX/EDX/ESI/EBP and flags are overwritten before leaving this
 * enclosing pass. Keep the exact first-unseen-at-cap stop and loop debits. */
int xs_test_surface_union(xctx *c)
{
    uint32_t count;x_guest_read(&count,c->r[7]+0x18,4);
    if(c->r[5] || !count || count>32767 || c->preempt<=(int)count)return 0;
    uint16_t selected;memcpy(&selected,g_img_base+0x38be10,2);
    if(selected>16384)return 0;
    for(uint32_t i=0;i<count;++i) {
        uint32_t face,word;x_guest_read(&face,c->r[6]+i*4,4);
        uint32_t a=0x30be10+(face>>5)*4,mask=1u<<(face&31);
        x_guest_read(&word,a,4);
        if(!(word&mask)) {
            if(selected==16384)break;
            word|=mask;x_guest_write(a,&word,4);++selected;
            memcpy(g_img_base+0x38be10,&selected,2);
        }
        if(i+1<count)--c->preempt;
    }
    return 1;
}
