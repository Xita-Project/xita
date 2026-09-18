/* Bounded capture of the pinned 53AF2 -> 52E10 visibility pass. Workers see
 * native copies only; the owner publishes each first-unseen surface in order. */
#include "xk.h"
#include "xk_light_census.h"
#include "xk_visibility_jobs.h"
#include "../xv_phase.h"
#include <limits.h>
#if !XV_NATIVE_VISIBILITY_JOBS
#error Visibility capture requires the copied-data worker interface
#endif
extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));
extern int xv_benchmark_compare_native_bounds(void) __attribute__((weak));
enum { VISIBLE=128,REFERENCES=32768,SURFACES=131072,WRITES=16,
       BITS=0x30be10,COUNT=0x38be10 };
enum { LAYOUT,MEMORY,BUDGET,CAPACITY,WORKER,REASONS };
static unsigned visibility_pass_accepted,visibility_pass_declined[REASONS];
static unsigned visibility_pass_classified,visibility_pass_published;
static struct {
    xv_visibility_input input[XV_VISIBILITY_JOB_CAPACITY];
    xs_bounds_result result[XV_VISIBILITY_JOB_CAPACITY];
    unsigned first[XV_VISIBILITY_JOB_CAPACITY],length[XV_VISIBILITY_JOB_CAPACITY];
    unsigned group[VISIBLE+1],faces[REFERENCES],bits[SURFACES/32];
} capture __attribute__((aligned(64)));
typedef struct { unsigned offset,n; } Span;
typedef struct { Span write[WRITES];unsigned count,arena; } Mapping;
static int physical(Mapping *m,unsigned offset,unsigned n,int writing)
{
    if(!n||m->arena<4096||offset>=m->arena-4096||n>m->arena-4096-offset)return 0;
    for(unsigned i=0;i<m->count;++i)
        if(offset<m->write[i].offset+m->write[i].n && m->write[i].offset<offset+n)return 0;
    if(writing) {
        if(m->count==WRITES)return 0;
        m->write[m->count++]=(Span){offset,n};
    }
    return 1;
}
static int span(Mapping *m,uint32_t a,unsigned n,int writing)
{
    if(!g_xram||!g_xpt||!n||a>UINT32_MAX-(n-1))return 0;
    for(unsigned done=0;done<n;) {
        uint32_t v=a+done,p=g_xpt[v>>12];unsigned take=4096-(v&4095);
        if(take>n-done)take=n-done;
        if((p&4095)||p>UINT32_MAX-(v&4095)||!physical(m,p+(v&4095),take,writing))return 0;
        done+=take;
    }
    return 1;
}
static int image(Mapping *m,uint32_t a,unsigned n,int writing)
{
    uintptr_t base=(uintptr_t)g_xram,p=(uintptr_t)g_img_base;
    if(!g_img_base||!g_xram||p<base||p-base>m->arena||a>m->arena-(p-base))return 0;
    return physical(m,(unsigned)(p-base)+a,n,writing);
}
static unsigned word(uint32_t a) { unsigned v;x_guest_read(&v,a,4);return v; }
static unsigned image_word(uint32_t a) { unsigned v;memcpy(&v,g_img_base+a,4);return v; }
static unsigned image_short(uint32_t a) { uint16_t v;memcpy(&v,g_img_base+a,2);return v; }
static int decline(unsigned reason) { ++visibility_pass_declined[reason];return 0; }
int xv_visibility_pass(xctx *c)
{
    if(xv_object_census_boundary(c)!=XV_LC_OK||XV_LIGHT_CENSUS_ON()||xv_phase_enabled||
       (&xv_watch_n&&xv_watch_n)||(&xv_trace_funcs&&xv_trace_funcs)||
       (xv_benchmark_compare_native_bounds&&xv_benchmark_compare_native_bounds()))return 0;
    uint32_t sp=c->r[4];
    if(c->df||(sp&3)||!xk_cur||sp<256||sp<xk_cur->stack_limit||
       sp-xk_cur->stack_limit<256||sp>xk_cur->stack_base||xk_cur->stack_base-sp<8)return decline(LAYOUT);
    Mapping m={.arena=xk_mem_arena_size()};
    /* Model original scratch writes too: they must not alter captured inputs. */
    if(!span(&m,sp-256,264,1)||!image(&m,COUNT,2,1)||
       !image(&m,0x30be0c,2,0))return decline(MEMORY);
    if(word(sp)!=0x53af7)return decline(LAYOUT);
    unsigned selected=image_short(COUNT),views=image_short(0x30be0c);
    if(!views||views>VISIBLE||selected>=16384)return decline(CAPACITY);
    uint32_t root=word(sp+4);
    if((root&3)||root>UINT32_MAX-0x13bu)return decline(LAYOUT);
    if(!span(&m,root+0xf8,4,0)||!span(&m,root+0x134,8,0))return decline(MEMORY);
    unsigned surfaces=word(root+0xf8),clusters=word(root+0x134);uint32_t table=word(root+0x138);
    if(!surfaces||surfaces>SURFACES||!clusters||clusters>512||(table&3))return decline(CAPACITY);
    unsigned words=(surfaces+31)/32;
    if(!span(&m,BITS,words*4,1))return decline(MEMORY);
    /* Recheck metadata against the now-complete output set. Read/read aliases
     * are allowed, including repeated clusters and the shared global frustum. */
    if(!span(&m,root+0xf8,4,0)||!span(&m,root+0x134,8,0)||
       !image(&m,0x30be0c,2,0)||!image(&m,0x39cc15,1,0)||
       !image(&m,0x2fedc4,4,0)||!image(&m,0x1f0a68,4,0))return decline(MEMORY);
    if(image_word(0x1f0a68))return decline(LAYOUT);
    unsigned global=g_img_base[0x39cc15]||image_word(0x2fedc4)==UINT32_MAX;
    unsigned n=0,refs=0;capture.group[0]=0;
    for(unsigned v=0;v<views;++v) {
        uint32_t record=0x2fee0c+v*0x1a0;int16_t id;
        if(!span(&m,record,2,0))return decline(MEMORY);
        x_guest_read(&id,record,2);
        if(id<0||(unsigned)id>=clusters||table>UINT32_MAX-(unsigned)id*0x68-0x3bu)return decline(LAYOUT);
        uint32_t cluster=table+(unsigned)id*0x68;
        if(!span(&m,cluster+0x34,8,0))return decline(MEMORY);
        unsigned count=word(cluster+0x34);uint32_t list=word(cluster+0x38);
        if(count>XV_VISIBILITY_JOB_CAPACITY-n)return decline(CAPACITY);
        if(count) {
            uint32_t f=global?0x2febe4:record+20;xs_frustum frustum;
            if((list&3)||!span(&m,list,count*36,0)||
               !span(&m,f+0x78,64,0)||!span(&m,f+0x128,24,0))return decline(MEMORY);
            x_guest_read(frustum.plane,f+0x78,64);x_guest_read(&frustum.enclosing,f+0x128,24);
            for(unsigned b=0;b<count;++b,++n) {
                uint32_t sub=list+b*36;unsigned len=word(sub+24);uint32_t indices=word(sub+28);
                if(len>4096||len>REFERENCES-refs)return decline(CAPACITY);
                capture.input[n].frustum=frustum;x_guest_read(&capture.input[n].box,sub,24);
                capture.first[n]=refs;capture.length[n]=len;
                if(len) {
                    if((indices&3)||!span(&m,indices,len*4,0))return decline(MEMORY);
                    x_guest_read(capture.faces+refs,indices,len*4);
                    for(unsigned j=0;j<len;++j)if(capture.faces[refs+j]>=surfaces)return decline(LAYOUT);
                }
                refs+=len;
            }
        }
        capture.group[v+1]=n;
        if(c->preempt<=(int)(8*n+refs+views))return decline(BUDGET);
    }
    if(!n)return decline(CAPACITY);
    x_guest_read(capture.bits,BITS,words*4);
    if(!xv_visibility_classify_jobs(c,capture.input,n,capture.result))return decline(WORKER);
    /* All possible declines precede publication. The conservative budget keeps
     * original callbacks outside this interval; debit only executed backedges. */
    unsigned debit=0,added=0;
    for(unsigned v=0;v<views && selected<16384;++v) {
        for(unsigned i=capture.group[v];i<capture.group[v+1] && selected<16384;++i) {
            debit+=capture.result[i].backedges;
            if(capture.result[i].classification) {
                unsigned len=capture.length[i],first=capture.first[i];
                for(unsigned j=0;j<len;++j) {
                    unsigned face=capture.faces[first+j],word_index=face>>5,mask=1u<<(face&31);
                    if(!(capture.bits[word_index]&mask)) {
                        if(selected==16384)break;
                        capture.bits[word_index]|=mask;
                        x_guest_write(BITS+word_index*4,capture.bits+word_index,4);
                        uint16_t value=(uint16_t)++selected;memcpy(g_img_base+COUNT,&value,2);++added;
                    }
                    if(j+1<len)++debit;
                }
            }
            if(i+1<capture.group[v+1])++debit;
        }
        if(v+1<views)++debit;
    }
    c->r[4]=sp+8;c->preempt-=(int)debit;
    ++visibility_pass_accepted;visibility_pass_classified+=n;visibility_pass_published+=added;return 1;
}
void xv_visibility_pass_report(unsigned frames)
{
    xk_os_log("[visibility-pass] %u frames accepted %u boxes %u surfaces %u declines layout/memory/budget/capacity/worker %u/%u/%u/%u/%u\n",
        frames,visibility_pass_accepted,visibility_pass_classified,visibility_pass_published,
        visibility_pass_declined[0],visibility_pass_declined[1],visibility_pass_declined[2],visibility_pass_declined[3],visibility_pass_declined[4]);
    visibility_pass_accepted=visibility_pass_classified=visibility_pass_published=0;
    memset(visibility_pass_declined,0,sizeof(visibility_pass_declined));
}
