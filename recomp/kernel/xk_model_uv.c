/* Qualified exact-key replay of 56F20; cold guest arithmetic stays in its caller.
 * No allocation, worker execution, per-call clocks, or GPU lifetime changes. */
#if defined(XV_MODEL_UV) && XV_MODEL_UV
#include "xk.h"
#include "xk_owner_phase.h"
#include "xk_render_cache_context.h"
#include "xk_model_uv.h"
extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));
static const uint32_t canonical[14]={0,0x3f800000,0,0x3f800000,0,0x3f800000,0,0x3f800000,0,0x3f800000,0,0x43b40000,0,0};
typedef struct {uint32_t args[6],top,fsw,fcw,fpscr;} UVKey;
static struct {
    xctx *owner;
    const uint8_t *ram,*image;const uint32_t *pt;
    uint32_t arena,lo,hi,packet,generation,serial,depth,blocked;
} scope;
typedef struct {
    UVKey key;
    uint32_t scratch[8],time,rows[8],fpscr;
    double st[4];uint16_t fsw;unsigned valid;
} UVValue;
static UVValue values[2];
static UVValue *last=&values[0], *previous=&values[1];
static unsigned victim_hits;
static struct {UVKey key;uint32_t sp,u,v;uintptr_t stack,row0,row1;unsigned token;} pending;
static unsigned serial,exhausted;
#if XV_MODEL_UV_CROSS_MODEL
static unsigned last_serial, previous_serial;
static struct { unsigned retained_exits,cross_hits,boundary_resets; } cross_counts;
#endif
static struct {
    unsigned scopes,calls[2],hits[2],cold,arguments,fp,both;
    unsigned top,fsw,fcw,fpscr,scope_declines,diagnostics,roots,spans,program,numeric;
    unsigned nesting,invalidations,completed;
} counts;
static uint32_t fp_get(void){
#if defined(__arm__)
    uint32_t v;__asm__ volatile("vmrs %0,fpscr":"=r"(v)::"memory");return v;
#else
    return ~0u;
#endif
}
static void fp_set(uint32_t v){
#if defined(__arm__)
    __asm__ volatile("vmsr fpscr,%0"::"r"(v):"memory");
#else
    (void)v;
#endif
}
static void invalidate(void){if(last->valid || previous->valid || pending.token)counts.invalidations++;last->valid=previous->valid=0;pending.token=0;}
static int diagnostic(void){return (&xv_watch_n && xv_watch_n) || (&xv_trace_funcs && xv_trace_funcs)
#ifdef XV_CHECK_GUEST_ADDRESS
    || xv_watch_len
#endif
    ;}
/* Reject workers/native foreign callers before any mutable cache/counter access. */
static int owner(xctx*c,uint32_t *generation){return xv_render_cache_admit(c,generation);}
static uint32_t *span(uint32_t a,unsigned n){
    if((a&3) || n>4096-(a&4095))return NULL;
    uint32_t p=scope.pt[a>>12];
    if((p&4095) || p>=scope.arena-4096)return NULL;
    return (uint32_t *)(scope.ram+p+(a&4095));
}
static int overlap(const void*a,unsigned n,const void*b,unsigned m){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+m && y<x+n;}
static int memory_context(const uint8_t *ram,const uint32_t *pt,const uint8_t *image){
    if(!ram || !pt || !image || ram!=g_xram || pt!=xv_render_cache_pt() || image!=xv_render_cache_image() ||
       ram!=scope.ram || pt!=scope.pt || image!=scope.image ||
       xk_mem_arena_size()!=scope.arena || xk_mem_image_lo()!=scope.lo || xk_mem_image_hi()!=scope.hi)return 0;
    return (xv_render_cache_helper_enabled() ? X_IMG32(0x2e3520u) :
            *(const xu32_u *)(image+0x2e3520))==scope.packet;
}
unsigned xk_model_uv_scope_begin(xctx*c,const uint8_t *ram,const uint32_t *pt,const uint8_t *image){
    uint32_t generation=0;int active=owner(c,&generation);
    if(active<0)return 0;
    if(active!=1){counts.scope_declines++;invalidate();scope.blocked=1;return 0;}
    if(scope.generation!=generation || scope.owner!=c){invalidate();memset(&scope,0,sizeof scope);}
    if(scope.depth){invalidate();scope.blocked=1;counts.nesting++;if(scope.depth==UINT32_MAX)return 0;scope.depth++;return scope.serial;}
    if(diagnostic()){invalidate();counts.diagnostics++;return 0;}
    uint32_t arena=xk_mem_arena_size(),lo=xk_mem_image_lo(),hi=xk_mem_image_hi();
    uintptr_t base=(uintptr_t)ram,at=(uintptr_t)image+0x2e3520u;
    if(ram!=g_xram || pt!=xv_render_cache_pt() || image!=xv_render_cache_image() || !ram || !pt || !image || arena<8192 ||
       lo>0x2e3520 || hi<0x2e3524 || at<base || at-base>arena-4100u){counts.roots++;invalidate();return 0;}
    if(exhausted || serial==UINT32_MAX){exhausted=1;counts.scope_declines++;return 0;}
#if XV_MODEL_UV_CROSS_MODEL
    if(pending.token || scope.ram!=ram || scope.pt!=pt || scope.image!=image ||
       scope.arena!=arena || scope.lo!=lo || scope.hi!=hi)invalidate();
#else
    invalidate();
#endif
    scope.owner=c;scope.ram=ram;scope.pt=pt;scope.image=image;scope.arena=arena;scope.lo=lo;scope.hi=hi;
    scope.generation=generation;scope.packet=xv_render_cache_helper_enabled() ? X_IMG32(0x2e3520u) : *(const xu32_u *)at;
    if(!scope.packet || !span(scope.packet,4)){counts.roots++;
#if XV_MODEL_UV_CROSS_MODEL
        invalidate();
#endif
        return 0;}
    scope.serial=++serial;scope.depth=1;scope.blocked=0;counts.scopes++;return scope.serial;
}
void xk_model_uv_scope_end(xctx*c,unsigned token){
    if(!token)return;uint32_t generation=0;
#if XV_MODEL_UV_CROSS_MODEL
    int active=owner(c,&generation);if(active<0)return;
#else
    if(owner(c,&generation)<0)return;
#endif
    if(c!=scope.owner || generation!=scope.generation || token!=scope.serial || !scope.depth)return;
#if XV_MODEL_UV_CROSS_MODEL
    if(active!=1 || scope.depth!=1 || scope.blocked || pending.token || diagnostic() ||
       !memory_context(scope.ram,scope.pt,scope.image))invalidate();
    else if(last->valid)cross_counts.retained_exits++;
    pending.token=0;
#else
    invalidate();
#endif
    if(!--scope.depth){scope.blocked=0;counts.completed++;}
}
#if XV_MODEL_UV_CROSS_MODEL
/* Scene/Present boundary: admitted owner only, including outside the scene.
 * Pending guest pointers never cross this boundary. An open model stays blocked. */
void xk_model_uv_owner_boundary(void *context){
    uint32_t generation=0;if(owner(context,&generation)<0)return;
    invalidate();if(scope.depth)scope.blocked=1;cross_counts.boundary_resets++;
}
#endif
int xk_model_uv_begin(xctx*c,const uint8_t *ram,const uint32_t *pt,const uint8_t *image,unsigned site,unsigned *token){
    uint32_t fp=fp_get(),generation=0;*token=0;int active=owner(c,&generation);
    if(active<0)return 0;
    if(!active || !scope.depth || scope.blocked || scope.owner!=c || scope.generation!=generation || site>1){counts.scope_declines++;invalidate();return 0;}
    counts.calls[site]++;
    if(diagnostic()){counts.diagnostics++;invalidate();return 0;}
    if(!memory_context(ram,pt,image)){counts.roots++;scope.blocked=1;invalidate();return 0;}
    if(c->fsp>7 || (c->fcw!=0x023f && c->fcw!=0x027f) || (fp&~0xf3c0009fu)){counts.numeric++;invalidate();return 0;}
    uint32_t e=c->r[4];
    if(e<32 || e>UINT32_MAX-28u){counts.spans++;invalidate();return 0;}
    uint32_t *s=span(e-32,60),*d=span(c->r[6],56),*u=span(c->r[3],16),*v=span(c->r[7],16);
    uint32_t *z=span(0x1f0a68,4),*o=span(0x1f0a78,4),*r=span(0x1f0b40,4),*p=NULL;
    if(c->r[1])p=span(c->r[1]+4,4);
    if(!s||!d||!u||!v||!z||!o||!r||(c->r[1]&&!p))goto bad_span;
    if(overlap(u,16,v,16)||overlap(u,16,s,60)||overlap(v,16,s,60)||overlap(s,60,d,56)||overlap(u,16,d,56)||overlap(v,16,d,56))goto bad_span;
    const void *inputs[4]={z,o,r,p};
    for(unsigned i=0;i<4;i++)if(inputs[i] && (overlap(s,60,inputs[i],4)||overlap(u,16,inputs[i],4)||overlap(v,16,inputs[i],4)))goto bad_span;
    for(unsigned i=0;i<14;i++)if(d[i]!=canonical[i])goto bad_program;
    if(*z!=0 || *o!=0x3f800000 || *r!=0x3c8efa35)goto bad_program;
    UVKey key;
    for(unsigned i=0;i<6;i++){key.args[i]=s[9+i];uint32_t a=key.args[i]&0x7fffffff;if(a && (a<0x00800000 || a>0x4b800000)){counts.numeric++;invalidate();return 0;}}
    /* Original56F26 overwrites these condition bits before their first read.
     * Sticky status, all other FSW bits, controls and TOP remain exact. */
    key.top=c->fsp;key.fsw=c->fsw&~0x4700u;key.fcw=c->fcw;key.fpscr=fp&~0xf0000000u;
    /* Keep the previous exact value as a victim entry: alternating material
     * parameters need not rerun canonical UV arithmetic. All admission and
     * memory checks above apply equally to both entries. No guest pointer is
     * retained here, and invalidate() retires both at the existing boundaries. */
    if(previous->valid && (!last->valid || memcmp(&key,&last->key,sizeof key)) &&
       !memcmp(&key,&previous->key,sizeof key)) {
        UVValue *swap=last;last=previous;previous=swap;victim_hits++;
#if XV_MODEL_UV_CROSS_MODEL
        unsigned serial_swap=last_serial;last_serial=previous_serial;previous_serial=serial_swap;
#endif
    }
    if(last->valid){
        unsigned args=memcmp(key.args,last->key.args,sizeof key.args)!=0;
        unsigned top=key.top!=last->key.top,fsw=key.fsw!=last->key.fsw,fcw=key.fcw!=last->key.fcw,fpscr=key.fpscr!=last->key.fpscr;
        unsigned state=top|fsw|fcw|fpscr;
        if(!args && !state){
            for(unsigned i=0;i<8;i++)s[i]=last->scratch[i];s[14]=last->time;
            for(unsigned i=0;i<4;i++){u[i]=last->rows[i];v[i]=last->rows[i+4];c->st[(c->fsp-1-i)&7]=last->st[i];}
            c->r[0]=last->fsw;c->r[1]=c->r[2]=0;c->r[4]=e+28;c->fsw=last->fsw;
            X_FLAGS(XK_LOGIC,0,0,(last->fsw>>8)&0x44,8);counts.hits[site]++;
#if XV_MODEL_UV_CROSS_MODEL
            /* Count only the first reuse in this model: subsequent same-model
             * hits were already possible with the original scope lifetime. */
            if(last_serial!=scope.serial){cross_counts.cross_hits++;last_serial=scope.serial;}
#endif
            fp_set(last->fpscr);return 1;
        }
        counts.arguments+=args&&!state;counts.fp+=state&&!args;counts.both+=args&&state;
        counts.top+=top;counts.fsw+=fsw;counts.fcw+=fcw;counts.fpscr+=fpscr;
    }
    if(last->valid) {
        UVValue *swap=last;last=previous;previous=swap;
#if XV_MODEL_UV_CROSS_MODEL
        previous_serial=last_serial;
#endif
    }
    last->valid=0;counts.cold++;pending.key=key;pending.sp=e;pending.u=c->r[3];pending.v=c->r[7];
    pending.stack=(uintptr_t)s;pending.row0=(uintptr_t)u;pending.row1=(uintptr_t)v;pending.token=scope.serial;*token=scope.serial;return 0;
 bad_span:counts.spans++;invalidate();return 0;
 bad_program:counts.program++;invalidate();return 0;
}
void xk_model_uv_end(xctx*c,unsigned token){
    if(!token)return;uint32_t fp=fp_get(),generation=0;
    int active=owner(c,&generation);if(active<0)return;
    if(active!=1){invalidate();scope.blocked=1;return;}
    if(scope.owner!=c || scope.generation!=generation || scope.serial!=token || pending.token!=token)return;
    if(scope.blocked || diagnostic() || !memory_context(scope.ram,scope.pt,scope.image) ||
       c->r[4]!=pending.sp+28 || c->fsp!=pending.key.top || c->fcw!=pending.key.fcw ||
       (uintptr_t)span(pending.sp-32,60)!=pending.stack || (uintptr_t)span(pending.u,16)!=pending.row0 || (uintptr_t)span(pending.v,16)!=pending.row1){invalidate();scope.blocked=1;return;}
    uint32_t *s=(uint32_t *)pending.stack,*u=(uint32_t *)pending.row0,*v=(uint32_t *)pending.row1;
    last->key=pending.key;last->fpscr=fp;last->fsw=c->fsw;
    for(unsigned i=0;i<8;i++)last->scratch[i]=s[i];last->time=s[14];
    for(unsigned i=0;i<4;i++){last->rows[i]=u[i];last->rows[i+4]=v[i];last->st[i]=c->st[(c->fsp-1-i)&7];}
    pending.token=0;last->valid=1;
#if XV_MODEL_UV_CROSS_MODEL
    last_serial=scope.serial;
#endif
}
void xk_model_uv_report(unsigned frames){
    /* Joined presenting-owner reporter only; never a per-material timer. */
    XK_LOG("[model-uv] %u frames scopes %u completed %u calls %u/%u hits %u/%u cold %u miss-args/fp/both %u/%u/%u fp-top/swx/cwx/fpscr %u/%u/%u/%u decline-scope/diag/root/span/program/numeric %u/%u/%u/%u/%u/%u nested %u invalidations %u; condition-bits-normalized\n",frames,counts.scopes,counts.completed,counts.calls[0],counts.calls[1],counts.hits[0],counts.hits[1],counts.cold,counts.arguments,counts.fp,counts.both,counts.top,counts.fsw,counts.fcw,counts.fpscr,counts.scope_declines,counts.diagnostics,counts.roots,counts.spans,counts.program,counts.numeric,counts.nesting,counts.invalidations);
#if XV_MODEL_UV_CROSS_MODEL
    XK_LOG("[model-uv-cross] %u frames retained-exits %u cross-scope-hits %u boundaries %u; scene/Present bounded\n",frames,cross_counts.retained_exits,cross_counts.cross_hits,cross_counts.boundary_resets);
    memset(&cross_counts,0,sizeof cross_counts);
#endif
    XK_LOG("[model-uv-victim] %u frames hits %u; second exact value, same scene/Present lifetime\n",frames,victim_hits);
    victim_hits=0;
    memset(&counts,0,sizeof counts);
}
#endif
