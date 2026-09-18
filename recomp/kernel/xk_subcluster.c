/* Owner-only native subcluster operations; no borrowed pointers survive return.
 * Ordered visibility publication is deliberately separate from pure math. */
#include "xk.h"
#include "xk_light_census.h"
#include "xk_subcluster.h"
#include "xk_subcluster_math.h"
#include <limits.h>
#if !defined(XV_EXPERIMENTAL_OBJECT_JOBS) || !defined(XV_LIGHT_QUERY_CENSUS)
#error Native subclusters require the registered owner/worker backend
#endif
extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));
extern int xv_benchmark_compare_native_bounds(void) __attribute__((weak));
enum { MAX_SPANS=96, MAX_FACES=4096, COUNT_ADDRESS=0x38be10, BITS_ADDRESS=0x30be10 };
enum { DIAGNOSTIC,LAYOUT,BUDGET,MEMORY,NUMERIC,REASONS };
static unsigned subcluster_accepted[2],subcluster_declined[REASONS],subcluster_faces;
typedef struct { uint32_t offset,bytes; } Span;
typedef struct { Span part[MAX_SPANS]; unsigned n,arena; } Mappings;

static int physical(Mappings *m,uint32_t off,unsigned bytes)
{
    if(!bytes || m->arena<4096 || off>=m->arena-4096 || bytes>m->arena-4096-off || m->n==MAX_SPANS)return 0;
    for(unsigned i=0;i<m->n;++i)
        if(off<m->part[i].offset+m->part[i].bytes && m->part[i].offset<off+bytes)return 0;
    m->part[m->n++]=(Span){off,bytes};return 1;
}
static int mapped(Mappings *m,uint32_t a,unsigned bytes)
{
    if(!g_xram || !g_xpt || m->arena<4096 || !bytes || a>UINT32_MAX-(bytes-1))return 0;
    for(unsigned done=0;done<bytes;) {
        uint32_t v=a+done,p=g_xpt[v>>12];unsigned n=4096-(v&4095);
        if(n>bytes-done)n=bytes-done;
        if((p&4095) || p>=m->arena-4096 || !physical(m,p+(v&4095),n))return 0;
        done+=n;
    }
    return 1;
}
static int image(Mappings *m,uint32_t a,unsigned bytes)
{
    uintptr_t p=(uintptr_t)g_img_base,base=(uintptr_t)g_xram;
    if(!g_img_base || !g_xram || p<base || p-base>m->arena || a>m->arena-(p-base))return 0;
    return physical(m,(uint32_t)(p-base)+a,bytes);
}
static int decline(unsigned reason) { ++subcluster_declined[reason];return 0; }
static int owner(xctx *c)
{
    if(xv_object_census_boundary(c)!=XV_LC_OK)return 0;
    if(XV_LIGHT_CENSUS_ON() || (&xv_watch_n&&xv_watch_n) || (&xv_trace_funcs&&xv_trace_funcs) ||
       (xv_benchmark_compare_native_bounds&&xv_benchmark_compare_native_bounds()))return decline(DIAGNOSTIC);
    return 1;
}
static int stack(uint32_t sp,unsigned below,unsigned above)
{
    return xk_cur && !(sp&3) && sp>=below && sp>=xk_cur->stack_limit &&
        sp-xk_cur->stack_limit>=below && sp<=xk_cur->stack_base && xk_cur->stack_base-sp>=above;
}
static uint32_t word(uint32_t a) { uint32_t v;x_guest_read(&v,a,4);return v; }
static int finite_words(const void *data,unsigned n)
{
    const unsigned char *p=data;
    for(unsigned i=0;i<n;++i) {
        uint32_t v;memcpy(&v,p+i*4,4);if((v&0x7fffffffu)>=0x7f800000u)return 0;
    }
    return 1;
}
static uint32_t ordered_key(uint32_t v)
{
    if(!(v&0x7fffffffu))v=0; /* Equal signed zeros. No FP status changes. */
    return (v&0x80000000u) ? ~v : v^0x80000000u;
}
static int ordered(const xs_box *b)
{
    for(unsigned i=0;i<3;++i) {
        uint32_t lo,hi;memcpy(&lo,&b->axis[i][0],4);memcpy(&hi,&b->axis[i][1],4);
        if(ordered_key(lo)>ordered_key(hi))return 0;
    }
    return 1;
}

int xv_subcluster_bounds(xctx *c)
{
    if(!owner(c))return 0;
    uint32_t sp=c->r[4],f=c->r[1],b=c->r[7];
    if(c->df || !stack(sp,0x70,8) || (f&3) || (b&3) || f>UINT32_MAX-0x13fu)return decline(LAYOUT);
    if(c->preempt<=7)return decline(BUDGET);
    Mappings m={.arena=xk_mem_arena_size()};
    if(!mapped(&m,sp-0x70,0x78) || !mapped(&m,f+0x78,64) || !mapped(&m,f+0x128,24) ||
       !mapped(&m,b,24) || !mapped(&m,0x1f0a68,4))return decline(MEMORY);
    if(word(sp)!=0x52ec6 || word(sp+4))return decline(LAYOUT);
    if(word(0x1f0a68))return decline(NUMERIC);
    xs_frustum frustum;xs_box box;
    x_guest_read(frustum.plane,f+0x78,64);x_guest_read(&frustum.enclosing,f+0x128,24);x_guest_read(&box,b,24);
    if(!finite_words(&frustum,sizeof(frustum)/4) || !finite_words(&box,6) || !ordered(&frustum.enclosing) || !ordered(&box))return decline(NUMERIC);
    xs_bounds_result result=xs_bounds(&frustum,&box);
    c->r[0]=(c->r[0]&0xffff0000u)|result.classification;c->r[4]=sp+8;c->preempt-=(int)result.backedges;
    ++subcluster_accepted[0];return 1;
}

int xv_subcluster_publish(xctx *c)
{
    if(!owner(c))return 0;
    uint32_t sp=c->r[4],sub=c->r[7],list=c->r[6];
    if(c->df || c->r[5] || !stack(sp,0,36) || (sub&3) || (list&3) || sub>UINT32_MAX-0x1fu)return decline(LAYOUT);
    Mappings m={.arena=xk_mem_arena_size()};
    if(!mapped(&m,sp,36) || !mapped(&m,sub+0x18,8) || !image(&m,COUNT_ADDRESS,2))return decline(MEMORY);
    if(word(sp+28)!=0x53af7)return decline(LAYOUT);
    uint32_t root=word(sp+32);
    if((root&3) || root>UINT32_MAX-0xfbu)return decline(LAYOUT);
    if(!mapped(&m,root+0xf8,4))return decline(MEMORY);
    uint32_t count=word(sub+0x18),pointer=word(sub+0x1c),surfaces=word(root+0xf8);
    if(!count || count>MAX_FACES || pointer!=list || !surfaces || surfaces>0x400000u)return decline(LAYOUT);
    if(c->preempt<=(int)count)return decline(BUDGET);
    uint16_t selected;memcpy(&selected,g_img_base+COUNT_ADDRESS,2);
    if(selected>16384)return decline(LAYOUT);
    /* Validate the complete output and list before the first write. Distinct
     * virtual pages may not alias one backing page or the count/stack/header. */
    if(!mapped(&m,list,count*4) || !mapped(&m,BITS_ADDRESS,((surfaces+31)/32)*4))return decline(MEMORY);
    uint32_t indices[MAX_FACES];x_guest_read(indices,list,count*4);
    for(unsigned i=0;i<count;++i)if(indices[i]>=surfaces)return decline(LAYOUT);
    for(unsigned i=0;i<count;++i) {
        uint32_t a=BITS_ADDRESS+(indices[i]>>5)*4,mask=1u<<(indices[i]&31),bits=word(a);
        if(!(bits&mask)) {
            if(selected==16384)break;
            bits|=mask;x_guest_write(a,&bits,4);++selected;
            memcpy(g_img_base+COUNT_ADDRESS,&selected,2);++subcluster_faces;
        }
        if(i+1<count)--c->preempt;
    }
    ++subcluster_accepted[1];return 1;
}

void xv_subcluster_report(unsigned frames)
{
    xk_os_log("[subcluster-native] %u frames: bounds %u unions %u faces %u declines diag/layout/budget/memory/numeric %u/%u/%u/%u/%u\n",
        frames,subcluster_accepted[0],subcluster_accepted[1],subcluster_faces,subcluster_declined[0],subcluster_declined[1],
        subcluster_declined[2],subcluster_declined[3],subcluster_declined[4]);
    memset(subcluster_accepted,0,sizeof(subcluster_accepted));memset(subcluster_declined,0,sizeof(subcluster_declined));subcluster_faces=0;
}
