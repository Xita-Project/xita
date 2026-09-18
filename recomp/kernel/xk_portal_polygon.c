/* Owner-only typed portal clipping. No retained guest pointers, job dispatch,
 * scheduler entry, GPU operation, or writes to emulated internal scratch. */
#include "xk.h"
#include "xk_light_census.h"
#include "xk_clip_region.h"
#include "xk_portal_polygon.h"
#include "xk_portal_polygon_math.h"
#include <limits.h>
#include <fenv.h>

#if !defined(XV_EXPERIMENTAL_OBJECT_JOBS) || !defined(XV_LIGHT_QUERY_CENSUS)
#error Typed portal clipping requires the registered owner/worker backend
#endif
extern int xv_watch_n __attribute__((weak)), xv_trace_funcs __attribute__((weak));
enum { MAX_POINTS=256, OLD_SCRATCH=0x3080, MAX_SPANS=16 };
enum { P_CONTEXT,P_DIAGNOSTIC,P_LAYOUT,P_BUDGET,P_MEMORY,P_NUMERIC,P_MODE,P_REASONS };
static unsigned portal_accepted, portal_declines[P_REASONS];
typedef struct { unsigned offset, bytes; } Span;
typedef struct { Span spans[MAX_SPANS]; unsigned count,arena; } Mappings;

/* Validate every fragment before reading, including the old routine's entire
 * internal stack. Pairwise physical disjointness rules out mapped aliases and
 * repeated page backing even when the guest virtual addresses differ. */
static int span(Mappings *m,uint32_t a,unsigned bytes)
{
    if(!bytes || a>UINT32_MAX-(bytes-1) || !g_xram || !g_xpt || m->arena<4096) return 0;
    for(unsigned done=0;done<bytes;) {
        uint32_t v=a+done,offset=g_xpt[v>>12];
        unsigned n=4096-(v&4095); if(n>bytes-done)n=bytes-done;
        if((offset&4095) || offset>=m->arena-4096 || m->count==MAX_SPANS) return 0;
        offset+=v&4095;
        for(unsigned i=0;i<m->count;++i)
            if(offset<m->spans[i].offset+m->spans[i].bytes && m->spans[i].offset<offset+n) return 0;
        m->spans[m->count++]=(Span){offset,n};done+=n;
    }
    return 1;
}

static int bounded(const xp_point *p,unsigned n)
{
    /* Integer checks preserve FPSCR on admission failure, including sNaNs.
     * +/-65536 covers projected portals while bounding intermediate products. */
    for(unsigned i=0;i<n;++i) {
        uint32_t a,b;memcpy(&a,&p[i].x,4);memcpy(&b,&p[i].y,4);
        if((a&0x7fffffff)>0x47800000u || (b&0x7fffffff)>0x47800000u)return 0;
    }
    return 1;
}

int xv_portal_polygon(xctx *c)
{
    /* No guest/global owner state is inspected until native identity passes. */
    if(xv_object_census_boundary(c)!=XV_LC_OK)return 0;
    if(XV_LIGHT_CENSUS_ON() || (&xv_watch_n&&xv_watch_n) || (&xv_trace_funcs&&xv_trace_funcs)) {
        portal_declines[P_DIAGNOSTIC]++;return 0;
    }
    uint32_t sp=c->r[4];
    if(c->df || (sp&3) || sp<OLD_SCRATCH || !xk_cur || sp<xk_cur->stack_limit+OLD_SCRATCH ||
       sp>xk_cur->stack_base || xk_cur->stack_base-sp<0x1050u) {
        portal_declines[P_LAYOUT]++;return 0;
    }
    Mappings m={.arena=xk_mem_arena_size()};
    if(!span(&m,sp-OLD_SCRATCH,OLD_SCRATCH+24)) {portal_declines[P_MEMORY]++;return 0;}
    uint32_t args[6];x_guest_read(args,sp,sizeof(args));
    int n=(int16_t)c->r[1],edges=(int16_t)args[1],capacity=(int16_t)args[3];
    /* Fixed buffer locations are part of the pinned caller's stack layout. */
    if(args[0]!=0x534DA || n<1 || n>MAX_POINTS || edges<1 || edges>MAX_POINTS ||
       capacity!=MAX_POINTS || c->r[2]!=sp+0x4c || args[4]!=sp+0x850 || (args[2]&3) ||
       args[5]!=0x38d1b717u) {portal_declines[P_LAYOUT]++;return 0;}
    if(c->preempt<=edges*257+1) {portal_declines[P_BUDGET]++;return 0;}
    if(!span(&m,c->r[2],n*8)||!span(&m,args[2],edges*8)||!span(&m,args[4],MAX_POINTS*8)||
       !span(&m,0x1f0a68,4)||!span(&m,0x1f0a78,4)||!span(&m,0x1f0af8,8)) {
        portal_declines[P_MEMORY]++;return 0;
    }
    uint32_t zero,one;uint64_t threshold;
    x_guest_read(&zero,0x1f0a68,4);x_guest_read(&one,0x1f0a78,4);x_guest_read(&threshold,0x1f0af8,8);
    if(zero || one!=0x3f800000u || threshold!=UINT64_C(0x3f1a36e2e0000000)) {
        portal_declines[P_NUMERIC]++;return 0;
    }
    xp_point input[MAX_POINTS],boundary[MAX_POINTS],output[MAX_POINTS],scratch[2*MAX_POINTS];
    x_guest_read(input,c->r[2],n*8);x_guest_read(boundary,args[2],edges*8);
    if(!bounded(input,n)||!bounded(boundary,edges)) {portal_declines[P_NUMERIC]++;return 0;}
    if(!xv_clip_region_begin()) {portal_declines[P_MODE]++;return 0;}
#ifdef __arm__
    uint32_t fp;__asm__ volatile("vmrs %0, fpscr":"=r"(fp));
#else
    fenv_t fp;fegetenv(&fp);
#endif
    xp_work work;float tolerance;memcpy(&tolerance,&args[5],4);
    int result=xp_portal_polygon(input,n,boundary,edges,capacity,tolerance,output,scratch,&work);
    if(result < -1) {
#ifdef __arm__
        __asm__ volatile("vmsr fpscr, %0"::"r"(fp):"memory");
#else
        fesetenv(&fp);
#endif
        xv_clip_region_end(NULL);portal_declines[P_NUMERIC]++;return 0;
    }
    /* No yield or concurrent guest owner could change mappings since capture. */
    if(result>0)x_guest_write(args[4],output,result*8);
    c->r[0]=(c->r[0]&0xffff0000u)|(uint16_t)result;
    c->r[4]=sp+24;c->preempt-=(int)work.backedges;
    for(unsigned i=0;i<work.clips;++i)xv_clip_region_account();
    xv_clip_region_work total={.regions=1,.planes=work.planes,.clips=work.clips,
        .input_vertices=work.vertices,.capacity_failures=result==-1};
    xv_clip_region_end(&total);portal_accepted++;return 1;
}

void xv_portal_polygon_report(unsigned frames)
{
    /* Called by the existing drained clip-region report, never a network task. */
    xk_os_log("[portal-native] %u frames: accepted %u declined diag/layout/budget/memory/numeric/mode %u/%u/%u/%u/%u/%u\n",
        frames,portal_accepted,portal_declines[P_DIAGNOSTIC],portal_declines[P_LAYOUT],portal_declines[P_BUDGET],
        portal_declines[P_MEMORY],portal_declines[P_NUMERIC],portal_declines[P_MODE]);
    portal_accepted=0;memset(portal_declines,0,sizeof(portal_declines));
}
