/* Guard-retained single-query adapter. No pool, allocation, HLE or unlock.
 * This establishes a production C boundary; it is not an unlock/FPS candidate. */
#ifdef XV_WORKER_QUERY
#include "xk.h"
#include "xk_worker_query.h"
#include <fenv.h>
#include <setjmp.h>
#include <limits.h>

enum { Q_STACK=16384+20, Q_VISITED=1024, Q_BYTES=Q_STACK+Q_VISITED+5,
       Q_LINES=512, Q_LINE=64, Q_PAGES=12, Q_OPS=2000000 };
enum { Q_OK, Q_LAYOUT, Q_MEMORY, Q_ALIAS, Q_LIMIT, Q_BUDGET, Q_CHANGED, Q_FP,
       Q_REASONS };
typedef struct { uint32_t address,offset,size; unsigned image; } Region;
typedef struct { uint32_t address,size; unsigned image; uintptr_t pointer; } Mapping;
typedef struct {
    uint32_t address; unsigned image;
    uintptr_t pointer; uint64_t read;
    unsigned char bytes[Q_LINE];
} ReadLine;
typedef struct Query {
    xctx entry,result;
    unsigned char initial[Q_BYTES],bytes[Q_BYTES];
    uint64_t dirty[(Q_BYTES+63)/64];
    Region regions[4]; Mapping mappings[Q_PAGES]; unsigned maps;
    ReadLine lines[Q_LINES]; uint16_t hash[Q_LINES*2]; unsigned used;
    unsigned ops,depth,peak_depth,stack_depth,backedges,reason;
    unsigned arena,image_lo,image_hi;
    unsigned char *ram,*image; uint32_t *pages;
    jmp_buf abort;
    fenv_t environment,result_environment;
    int rounding;
} Query;
#ifndef XV_OBJECT_WORKERS
#define XV_OBJECT_WORKERS 2
#endif
enum { Q_LANES=XV_OBJECT_WORKERS };
static Query queries[Q_LANES] __attribute__((aligned(64)));
static struct { uint64_t attempts,applied,declined[Q_REASONS],us,backedges,reads,dirty;
    unsigned depths[9],max_depth,max_stack; } stats[Q_LANES];

static void __attribute__((noreturn)) q_fail(Query *v,unsigned reason)
{ v->reason=reason; longjmp(v->abort,1); }
static uintptr_t q_pointer(Query *v,uint32_t a,unsigned n,unsigned image)
{
    if(!n||a>UINT32_MAX-(n-1))q_fail(v,Q_MEMORY);
    if(image){
        if(!v->image||a<v->image_lo||a>v->image_hi||n>v->image_hi-a)q_fail(v,Q_MEMORY);
        return (uintptr_t)(v->image+a);
    }
    if(!v->ram||!v->pages||n>4096-(a&4095))q_fail(v,Q_MEMORY);
    uint32_t offset=v->pages[a>>12];
    if((offset&4095)||v->arena<4096||offset>=v->arena-4096||n>v->arena-offset-(a&4095))q_fail(v,Q_MEMORY);
    return (uintptr_t)(v->ram+offset+(a&4095));
}
static unsigned q_offset(Query *v,uint32_t a,unsigned n,unsigned image)
{
    for(unsigned i=0;i<4;i++){
        Region *r=&v->regions[i];
        if(r->image==image&&a>=r->address&&a-r->address<=r->size&&n<=r->size-(a-r->address))return r->offset+a-r->address;
    }
    return UINT_MAX;
}
static void q_no_alias(Query *v,uintptr_t p,unsigned n)
{
    for(unsigned i=0;i<v->maps;i++){
        Mapping *m=&v->mappings[i];
        if(p<m->pointer+m->size&&m->pointer<p+n)q_fail(v,Q_ALIAS);
    }
}
static uint64_t q_read(Query *v,uint32_t a,unsigned n,unsigned image)
{
    if(++v->ops>Q_OPS||!n||n>8)q_fail(v,Q_LIMIT);
    unsigned off=q_offset(v,a,n,image);uint64_t value=0;
    if(off!=UINT_MAX){memcpy(&value,v->bytes+off,n);return value;}
    uintptr_t p=q_pointer(v,a,n,image);q_no_alias(v,p,n);
    /* Cache consumed bytes only. The unused portion of a cache line may share
     * a page with private stack or mutable query globals; it is never replayed. */
    for(unsigned i=0;i<n;){
        uint32_t address=(a+i)&~(Q_LINE-1u);
        unsigned h=((address>>6)^(address>>15)^image)&(Q_LINES*2-1);
        while(v->hash[h]){
            ReadLine *line=&v->lines[v->hash[h]-1];
            if(line->address==address&&line->image==image)break;
            h=(h+1)&(Q_LINES*2-1);
        }
        if(!v->hash[h]){
            if(v->used==Q_LINES)q_fail(v,Q_LIMIT);
            ReadLine *line=&v->lines[v->used];
            line->address=address;line->image=image;line->read=0;
            line->pointer=q_pointer(v,address,Q_LINE,image);
            memcpy(line->bytes,(void*)line->pointer,Q_LINE);
            v->hash[h]=++v->used;
        }
        ReadLine *line=&v->lines[v->hash[h]-1];unsigned k=(a+i)&(Q_LINE-1),take=Q_LINE-k;
        if(take>n-i)take=n-i;
        line->read|=((UINT64_C(1)<<take)-1)<<k;
        memcpy((unsigned char*)&value+i,line->bytes+k,take);i+=take;
    }
    return value;
}
static void q_write(Query *v,uint32_t a,uint64_t value,unsigned n,unsigned image)
{
    if(++v->ops>Q_OPS||!n||n>8)q_fail(v,Q_LIMIT);
    unsigned off=q_offset(v,a,n,image);if(off==UINT_MAX)q_fail(v,Q_MEMORY);
    memcpy(v->bytes+off,&value,n);
    if(off<Q_STACK&&a<v->entry.r[4]&&v->entry.r[4]-a>v->stack_depth)v->stack_depth=v->entry.r[4]-a;
    for(unsigned i=off;i<off+n;i++)v->dirty[i>>6]|=UINT64_C(1)<<(i&63);
}
static double q_float(Query *v,uint32_t a)
{
    uint32_t bits=(uint32_t)q_read(v,a,4,0);
    /* Changed register allocation can change native NaN operand priority. Keep
     * exceptional inputs on the untouched original translation, including sNaN. */
    if((bits&0x7f800000u)==0x7f800000u)q_fail(v,Q_FP);
    float f;memcpy(&f,&bits,4);return (double)f;
}
static void q_store_float(Query *v,uint32_t a,double d)
{
    float f=(float)d;uint32_t bits;memcpy(&bits,&f,4);
    if((bits&0x7f800000u)==0x7f800000u)q_fail(v,Q_FP);
    q_write(v,a,bits,4,0);
}
static uint32_t q_pop(Query *v,xctx *c)
{uint32_t x=(uint32_t)q_read(v,c->r[4],4,0);c->r[4]+=4;return x;}
#undef X_M8
#undef X_M16
#undef X_M32
#undef X_M64
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#undef X_PUSH32
#undef X_POP32
#undef X_PREEMPT
#define X_M8(a) ((uint8_t)q_read(v,(a),1,0))
#define X_M16(a) ((uint16_t)q_read(v,(a),2,0))
#define X_M32(a) ((uint32_t)q_read(v,(a),4,0))
#define X_M64(a) q_read(v,(a),8,0)
#define X_IMG8(a) ((uint8_t)q_read(v,(a),1,1))
#define X_IMG16(a) ((uint16_t)q_read(v,(a),2,1))
#define X_IMG32(a) ((uint32_t)q_read(v,(a),4,1))
#define Q_W8(a,x,i) q_write(v,(a),(uint8_t)(x),1,(i))
#define Q_W16(a,x,i) q_write(v,(a),(uint16_t)(x),2,(i))
#define Q_W32(a,x,i) q_write(v,(a),(uint32_t)(x),4,(i))
#define Q_W64(a,x,i) q_write(v,(a),(uint64_t)(x),8,(i))
#define X_PUSH32(x) do{uint32_t value_=(x);c->r[4]-=4;Q_W32(c->r[4],value_,0);}while(0)
#define X_POP32() q_pop(v,c)
#define X_PREEMPT() do{if(++v->backedges>=1000000u||--c->preempt<=0)q_fail(v,Q_BUDGET);}while(0)
#define x87_load_f32(c,a) q_float(v,(a))
#define x87_store_f32(c,a,x) q_store_float(v,(a),(x))
#include "xk_worker_query_generated.inc"

static void q_capture(Query *v,xctx *c)
{
    v->entry=v->result=*c;v->maps=v->used=v->ops=v->depth=v->peak_depth=v->stack_depth=v->backedges=v->reason=0;
    memset(v->hash,0,sizeof v->hash);memset(v->dirty,0,sizeof v->dirty);
    v->ram=g_xram;v->image=g_img_base;v->pages=g_xpt;
    v->arena=xk_mem_arena_size();v->image_lo=xk_mem_image_lo();v->image_hi=xk_mem_image_hi();
    v->regions[0]=(Region){c->r[4]-16384,0,Q_STACK,0};
    v->regions[1]=(Region){0x2d2fb0,Q_STACK,Q_VISITED,0};
    v->regions[2]=(Region){0x2d2fac,Q_STACK+Q_VISITED,4,1};
    v->regions[3]=(Region){0x2d2fa9,Q_STACK+Q_VISITED+4,1,1};
    for(unsigned i=0;i<4;i++){
        Region *r=&v->regions[i];
        for(unsigned j=0;j<r->size;){
            unsigned n=4096-((r->address+j)&4095);if(n>r->size-j)n=r->size-j;
            uintptr_t pointer=q_pointer(v,r->address+j,n,r->image);q_no_alias(v,pointer,n);
            if(v->maps==Q_PAGES)q_fail(v,Q_LIMIT);
            v->mappings[v->maps++]=(Mapping){r->address+j,n,r->image,pointer};
            memcpy(v->initial+r->offset+j,(void*)pointer,n);j+=n;
        }
    }
    memcpy(v->bytes,v->initial,sizeof v->bytes);
    /* Actual 925AB supplies both inputs above the entry SP. Do not accept
     * scratch/input aliases, even though a more general interpreter might. */
    uint32_t center;memcpy(&center,v->initial+16384+12,4);
    q_no_alias(v,q_pointer(v,c->r[0],6,0),6);
    q_no_alias(v,q_pointer(v,center,12,0),12);
    uint32_t bsp=(uint32_t)q_read(v,0x39be58,4,1);
    unsigned clusters=(unsigned)q_read(v,bsp+0x134,4,0);
    if(!clusters||clusters>256)q_fail(v,Q_LAYOUT);
}
static void q_validate(Query *v,xctx *c,int guard,int lane)
{
    if(g_xram!=v->ram||g_img_base!=v->image||g_xpt!=v->pages||
       memcmp(c,&v->entry,sizeof *c)||fegetround()!=v->rounding||
       xv_object_query_lane(c,guard,v->regions[0].address,Q_STACK)!=lane+1)q_fail(v,Q_CHANGED);
    for(unsigned i=0;i<v->maps;i++){
        Mapping *m=&v->mappings[i];unsigned off=q_offset(v,m->address,m->size,m->image);
        if(q_pointer(v,m->address,m->size,m->image)!=m->pointer||
           memcmp((void*)m->pointer,v->initial+off,m->size))q_fail(v,Q_CHANGED);
    }
    for(unsigned i=0;i<v->used;i++){
        ReadLine *line=&v->lines[i];
        if(q_pointer(v,line->address,Q_LINE,line->image)!=line->pointer)q_fail(v,Q_CHANGED);
        for(unsigned k=0;k<Q_LINE;k++)if((line->read&(UINT64_C(1)<<k))&&
            ((unsigned char*)line->pointer)[k]!=line->bytes[k])q_fail(v,Q_CHANGED);
    }
}
static void q_publish(Query *v,xctx *c)
{
    /* Validation is complete. No fallible operation or callback after this
     * point. Mappings remain protected by the same retained original guard. */
    for(unsigned i=0;i<v->maps;i++){
        Mapping *m=&v->mappings[i];unsigned off=q_offset(v,m->address,m->size,m->image);
        unsigned end=off+m->size;
        for(unsigned word=off>>6;word<=(end-1)>>6;word++){
            uint64_t bits=v->dirty[word];unsigned base=word*64;
            if(base<off)bits&=UINT64_MAX<<(off-base);
            if(end-base<64)bits&=(UINT64_C(1)<<(end-base))-1;
            while(bits){
                unsigned start=__builtin_ctzll(bits),n=64-start;
                uint64_t rest=~(bits>>start);
                if(rest)n=__builtin_ctzll(rest);
                memcpy((unsigned char*)m->pointer+base+start-off,v->bytes+base+start,n);
                bits&=~((n==64?UINT64_MAX:(UINT64_C(1)<<n)-1)<<start);
            }
        }
    }
    *c=v->result;
    /* Preserve the full native FP environment, including ARM FPSCR NZCV and
     * prior sticky exceptions. Re-raising flags alone loses comparison state. */
    fesetenv(&v->result_environment);
}
int xv_worker_query(xctx *c,int guard)
{
    extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));
    if((&xv_watch_n&&xv_watch_n)||(&xv_trace_funcs&&xv_trace_funcs))return 0;
    if(c->r[4]<16384||c->r[4]>UINT32_MAX-20||(c->r[4]&3)||c->fsp>7)return 0;
    int lane=xv_object_query_lane(c,guard,c->r[4]-16384,Q_STACK)-1;
    if(lane<0||lane>=Q_LANES)return 0;
    Query *v=&queries[lane];stats[lane].attempts++;
    uint64_t begin=xk_os_monotonic_us();
    /* The frame and FP environment are owned by this one native lane. Longjmp
     * unwinds only generated/private C frames, never a guard cleanup scope. */
    fegetenv(&v->environment);v->rounding=fegetround();
    if(setjmp(v->abort)){
        fesetenv(&v->environment);stats[lane].declined[v->reason]++;
        stats[lane].us+=xk_os_monotonic_us()-begin;return 0;
    }
    q_capture(v,c);
    q_00056670(&v->result,v);fegetenv(&v->result_environment);
    fesetenv(&v->environment);
#ifdef XV_WORKER_QUERY_TEST
    extern void xv_worker_query_test_ready(xctx *,unsigned);
    xv_worker_query_test_ready(c,(unsigned)lane);
#endif
    q_validate(v,c,guard,lane);q_publish(v,c);
    stats[lane].applied++;stats[lane].backedges+=v->backedges;stats[lane].reads+=v->used;
    unsigned bucket=0;while(bucket<8&&(1u<<bucket)<v->peak_depth)bucket++;
    stats[lane].depths[bucket]++;
    if(v->peak_depth>stats[lane].max_depth)stats[lane].max_depth=v->peak_depth;
    if(v->stack_depth>stats[lane].max_stack)stats[lane].max_stack=v->stack_depth;
    for(unsigned i=0;i<(Q_BYTES+63)/64;i++)stats[lane].dirty+=__builtin_popcountll(v->dirty[i]);
    stats[lane].us+=xk_os_monotonic_us()-begin;return 1;
}
void xv_worker_query_report(void)
{
    /* Caller is the existing drained object report boundary. */
    for(unsigned lane=0;lane<Q_LANES;lane++){
        XK_LOG("[worker-query] lane %u attempts %llu applied %llu adapter-us %llu backedges %llu read-lines %llu dirty-bytes %llu storage %u declines layout/memory/alias/limit/budget/changed/fp %llu/%llu/%llu/%llu/%llu/%llu/%llu\n",
            lane,(unsigned long long)stats[lane].attempts,(unsigned long long)stats[lane].applied,
            (unsigned long long)stats[lane].us,(unsigned long long)stats[lane].backedges,
            (unsigned long long)stats[lane].reads,(unsigned long long)stats[lane].dirty,(unsigned)sizeof(Query),
            (unsigned long long)stats[lane].declined[Q_LAYOUT],(unsigned long long)stats[lane].declined[Q_MEMORY],
            (unsigned long long)stats[lane].declined[Q_ALIAS],(unsigned long long)stats[lane].declined[Q_LIMIT],
            (unsigned long long)stats[lane].declined[Q_BUDGET],(unsigned long long)stats[lane].declined[Q_CHANGED],
            (unsigned long long)stats[lane].declined[Q_FP]);
        XK_LOG("[worker-query] lane %u applied-depth-ceil 1/2/4/8/16/32/64/128/256 counts %u/%u/%u/%u/%u/%u/%u/%u/%u max-depth %u max-stack-bytes %u\n",lane,
            stats[lane].depths[0],stats[lane].depths[1],stats[lane].depths[2],stats[lane].depths[3],
            stats[lane].depths[4],stats[lane].depths[5],stats[lane].depths[6],stats[lane].depths[7],
            stats[lane].depths[8],stats[lane].max_depth,stats[lane].max_stack);
    }
    memset(stats,0,sizeof stats);
}
#endif
