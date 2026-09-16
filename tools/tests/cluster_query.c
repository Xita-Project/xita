/* Independent original-prefix oracle: traversal order, capacity, visited epoch
 * and exact backedge count. This is NOT full guest-state publication validation. */
#include "xv_x86rt.h"
#include "kernel/xk_cluster_query.h"
#include "cluster_axes.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

enum { RAM=4<<20, ARENA=8<<20, BSP=0x10000, COLL=0x11000,
       PROJECTION=0x11100, PLANES=0x12000, PLANES2=0x13000,
       CLUSTERS=0x20000, ADJ=0x40000, PORTALS=0x50000, VERTS=0x90000,
       SP=0x2b0000, MAX_PORTALS=512, MAX_VERTICES=8192 };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static XvCluster clusters[256];
static uint16_t adjacency[MAX_PORTALS*2];
static XvPortal portals[MAX_PORTALS];
static XvPoint vertices[MAX_VERTICES];
static XvPlane distance_planes[8], projection_planes[8];
static uint32_t visited[256], seed, current_case;
static XvClusterGeometry geometry;
static unsigned yields, accepted, declined, over_capacity, seen_budget;
void f_00056670(xctx *);
void __wrap_xv_preempt(xctx *c) { yields++; c->preempt=1000000; }
static uint32_t random32(void)
{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static float random_float(float scale)
{return (float)((int)(random32()%2001)-1000)*scale;}
static void put(uint32_t a,const void *p,unsigned n) {x_guest_write(a,p,n);}
static void w32(uint32_t a,uint32_t v) {put(a,&v,4);}
static void w16(uint32_t a,uint16_t v) {put(a,&v,2);}
static void fp32(uint32_t a,float v) {put(a,&v,4);}
static void require(int value,const char *what)
#ifdef CLUSTER_ARM
{(void)what;if(!value)__builtin_trap();}
#else
{if(!value){fprintf(stderr,"case %u: %s\n",current_case,what);abort();}}
#endif

static void graph(unsigned n,unsigned style)
{
    memset(&geometry,0,sizeof geometry);
    memset(clusters,0,sizeof clusters);memset(portals,0,sizeof portals);
    geometry=(XvClusterGeometry){.clusters=clusters,.adjacency=adjacency,
        .portals=portals,.vertices=vertices,.distance_planes=distance_planes,
        .projection_planes=projection_planes,.cluster_count=n,
        .distance_plane_count=8,.projection_plane_count=8};
    memcpy(geometry.axes,axes,sizeof axes);
    for(unsigned k=0;k<8;k++){
        unsigned axis=k%3;
        distance_planes[k]=(XvPlane){{0,0,0,0}};
        distance_planes[k].v[axis]=k&1?-1:1;
        if(style==3)distance_planes[k].v[(axis+1)%3]=distance_planes[k].v[axis];
        if(style==4)distance_planes[k].v[3]=random_float(.025f);
        projection_planes[k]=distance_planes[k];
        if(style==5)projection_planes[k].v[(axis+1)%3]=.25f;
    }
    unsigned extra=n>1?(style?n/2:0):0;
    geometry.portal_count=n-1+extra;
    for(unsigned i=0;i<geometry.portal_count;i++){
        XvPortal *p=&portals[i];
        p->sides[0]=i<n-1?(int)i:(int)(random32()%n);
        p->sides[1]=i<n-1?(int)i+1:(int)(random32()%n);
        p->plane=style?i%8:0;p->radius=100;
        unsigned axis=p->plane%3;
        p->vertices=4;
        if(style==2)p->vertices=i%6;
        if(style==6&&i==0)p->vertices=128;
        p->first_vertex=geometry.vertex_count;
        for(unsigned j=0;j<p->vertices;j++){
            XvPoint *v=&vertices[geometry.vertex_count++];
            *v=(XvPoint){{0,0,0}};
            static const float quad[4][2]={{-10,-10},{10,-10},{10,10},{-10,10}};
            v->v[(axis+1)%3]=quad[j%4][0];v->v[(axis+2)%3]=quad[j%4][1];
            if(style==7)v->v[(axis+1)%3]+=random_float(.01f);
        }
        assert(geometry.vertex_count<=MAX_VERTICES);
    }
    for(unsigned k=0;k<n;k++){
        clusters[k].first=geometry.adjacency_count;
        for(unsigned i=0;i<geometry.portal_count;i++)
            if(portals[i].sides[0]==(int)k||portals[i].sides[1]==(int)k)
                adjacency[geometry.adjacency_count++]=(uint16_t)i;
        clusters[k].count=geometry.adjacency_count-clusters[k].first;
        if(style&1)for(unsigned j=clusters[k].count;j>1;j--){
            unsigned at=clusters[k].first,other=random32()%j;
            uint16_t v=adjacency[at+j-1];adjacency[at+j-1]=adjacency[at+other];adjacency[at+other]=v;
        }
    }
    assert(geometry.adjacency_count<=MAX_PORTALS*2);
    require(xv_cluster_geometry_valid(&geometry),"fixture geometry");
    X_IMG32(0x39be58)=BSP;X_IMG32(0x39be50)=PROJECTION;
    w32(BSP+0xb4,COLL);w32(COLL+0x10,PLANES);w32(PROJECTION+0x10,PLANES2);
    w32(BSP+0x134,n);w32(BSP+0x138,CLUSTERS);
    w32(BSP+0x154,geometry.portal_count);w32(BSP+0x158,PORTALS);
    put(PLANES,distance_planes,sizeof distance_planes);put(PLANES2,projection_planes,sizeof projection_planes);
    put(0x1eaf30,axes,24);fp32(0x1f0a68,0);
    put(ADJ,adjacency,geometry.adjacency_count*2);
    for(unsigned k=0;k<n;k++){
        w32(CLUSTERS+k*104+0x5c,clusters[k].count);
        w32(CLUSTERS+k*104+0x60,ADJ+clusters[k].first*2);
    }
    for(unsigned k=0;k<geometry.portal_count;k++){
        const XvPortal *p=&portals[k];unsigned a=PORTALS+k*64;
        w16(a,p->sides[0]);w16(a+2,p->sides[1]);w32(a+4,p->plane);
        put(a+8,p->center,12);fp32(a+20,p->radius);
        w32(a+0x34,p->vertices);w32(a+0x38,VERTS+p->first_vertex*12);
    }
    put(VERTS,vertices,geometry.vertex_count*sizeof *vertices);
}

static void prepare(unsigned k,unsigned n,unsigned style,XvClusterInput *input,xctx *context)
{
    current_case=k;seed=k+1;graph(n,style);
    XvClusterInput in={.radius=100,.start=0,.epoch=100,.budget=1000000,.visited=visited};
    if(k%11==0)in.epoch=UINT32_MAX;
    for(unsigned i=0;i<256;i++)visited[i]=(k%5==0&&i%7==0)?in.epoch+1:0xabcdef00+i;
    if(k%17==0)in.start=-1;
    else if(k%13==0)in.start=(int16_t)(n-1);
    if(k%9==0)in.radius=0;
    else if(k%9==1)in.radius=-1;
    else if(style)in.radius=(k%9==2)?1:10;
    if(style>=3)for(unsigned j=0;j<3;j++)in.center[j]=random_float(.0125f);
    /* Exact and adjacent plane/radius boundaries, small and large finite inputs. */
    if(style==1){in.center[0]=k&1?nextafterf(in.radius,0):in.radius;}
    xctx c={0};for(unsigned i=0;i<8;i++){c.r[i]=0x13570000+k*71+i*13;c.st[i]=i+.25;}
    c.r[0]=SP+64;c.r[4]=SP;c.fsp=k&7;c.fcw=0x37f;c.fsw=(k*0x9123)&65535;c.preempt=in.budget;
    w16(SP+68,(uint16_t)in.start);w32(SP+12,SP+80);put(SP+80,in.center,12);fp32(SP+16,in.radius);
    w32(SP+8,0);X_IMG32(0x2d2fac)=in.epoch;X_IMG8(0x2d2fa9)=0;
    put(0x2d2fb0,visited,sizeof visited);yields=0;
    *input=in;*context=c;
}

static void check(unsigned k,unsigned n,unsigned style)
{
    XvClusterInput in;xctx c;
    prepare(k,n,style,&in,&c);
    f_00056670(&c);require(!yields,"unexpected reference budget exhaustion");
    XvClusterResult out;
    if(!xv_cluster_query_direct(&geometry,&in,&out)){declined++;return;}
    accepted++;require(out.count==(uint16_t)c.r[0],"result count");
    require(out.epoch==X_IMG32(0x2d2fac),"epoch");
    require(out.backedges==in.budget-(unsigned)c.preempt,"backedges");
    for(unsigned i=0;i<out.count&&i<64;i++)require(out.clusters[i]==X_M16(SP-128+2*i),"ordered cluster output");
    if(out.count>64)over_capacity++;
    for(unsigned i=0;i<256;i++){
        uint32_t expected=(out.changed[i>>5]&(1u<<(i&31)))?out.epoch:visited[i];
        require(expected==X_M32(0x2d2fb0+4*i),"visited changes");
    }
    if(out.backedges){
        in.budget=out.backedges;
        require(!xv_cluster_query_direct(&geometry,&in,&out),"budget decline");seen_budget++;
    }
}

#ifndef CLUSTER_ARM
static void invalid_inputs(void)
{
    XvClusterInput in;xctx c;XvClusterResult out;
    prepare(7,7,0,&in,&c);
    XvClusterGeometry bad=geometry;
    bad.cluster_count=0;require(!xv_cluster_geometry_valid(&bad),"empty geometry");
    bad=geometry;bad.cluster_count=257;require(!xv_cluster_geometry_valid(&bad),"visited capacity");
    bad=geometry;bad.portal_count=32769;require(!xv_cluster_geometry_valid(&bad),"signed portal index");
    bad=geometry;bad.adjacency=NULL;require(!xv_cluster_geometry_valid(&bad),"missing adjacency");
    XvCluster saved=clusters[0];clusters[0].first=UINT32_MAX;
    require(!xv_cluster_geometry_valid(&geometry),"adjacency bounds");clusters[0]=saved;
    uint16_t old=adjacency[0];adjacency[0]=32768;
    require(!xv_cluster_geometry_valid(&geometry),"portal reference bounds");adjacency[0]=old;
    XvPortal portal=portals[0];portals[0].vertices=129;
    require(!xv_cluster_geometry_valid(&geometry),"projection scratch capacity");portals[0]=portal;
    portals[0].first_vertex=UINT32_MAX;
    require(!xv_cluster_geometry_valid(&geometry),"vertex offset wrap");portals[0]=portal;
    portals[0].sides[1]=-1;
    require(!xv_cluster_geometry_valid(&geometry),"invalid adjacent cluster");portals[0]=portal;
    portals[0].plane=geometry.projection_plane_count;
    require(!xv_cluster_geometry_valid(&geometry),"projection plane bounds");portals[0]=portal;
    float old_plane=distance_planes[0].v[0];distance_planes[0].v[0]=NAN;
    require(!xv_cluster_geometry_valid(&geometry),"exceptional geometry");distance_planes[0].v[0]=old_plane;
    require(xv_cluster_geometry_valid(&geometry),"restored geometry");
    in.radius=NAN;require(!xv_cluster_query_direct(&geometry,&in,&out),"NaN input fallback");
    in.radius=INFINITY;require(!xv_cluster_query_direct(&geometry,&in,&out),"infinite input fallback");
    in.radius=100;in.center[0]=INFINITY;
    require(!xv_cluster_query_direct(&geometry,&in,&out),"exceptional center fallback");
    in.center[0]=0;in.budget=UINT32_MAX;
    require(!xv_cluster_query_direct(&geometry,&in,&out),"negative signed budget fallback");
}
int main(void)
{
    g_xram=calloc(1,ARENA);g_img_base=g_xram+RAM;g_xpt=calloc(1<<20,4);
    assert(g_xram&&g_xpt);
    for(unsigned i=0;i<RAM/4096;i++)g_xpt[i]=(i^1u)*4096;
    unsigned sizes[]={1,2,7,31,65,128,256};
    int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned round=0;round<4;round++){
        fesetround(rounds[round]);
        for(unsigned k=0;k<CASES;k++)check(k,sizes[k%7],(k/7)%8);
    }
    invalid_inputs();
    require(accepted>CASES*3,"admission coverage");require(over_capacity&&seen_budget,"large graph/budget coverage");
    printf("PASS %u numerical comparisons, %u declines, %u over-capacity traversals, %u budget fallbacks; guest-state publication unproved\n",accepted,declined,over_capacity,seen_budget);
    free(g_xram);free(g_xpt);return 0;
}
#else
/* Instruction harness only: typed input/snapshot preparation is outside both
 * measured functions. These wrappers do not model live publication or locks. */
#include <stddef.h>
static XvClusterInput arm_input;
xctx arm_context;
XvClusterResult arm_result;
int arm_admitted;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
const unsigned result_layout[]={sizeof(XvClusterResult),offsetof(XvClusterResult,count),offsetof(XvClusterResult,epoch),offsetof(XvClusterResult,changed),offsetof(XvClusterResult,backedges)};
void arm_prepare(unsigned k,unsigned n,unsigned style)
{prepare(k,n,style,&arm_input,&arm_context);}
void arm_tweak(unsigned kind)
{
    static const uint32_t radii[]={0x42c80000,0,0xbf800000,0x7f800000,
        0x7fc12345,0x7f801234,1,0x80000001,0x7f7fffff};
    if(kind<sizeof radii/sizeof *radii){
        memcpy(&arm_input.radius,&radii[kind],4);w32(SP+16,radii[kind]);
    }else if(kind==9){arm_input.start=-1;w16(SP+68,65535);}
    else if(kind==10){arm_input.budget=1;arm_context.preempt=1;}
    else if(kind==11){
        arm_input.epoch=UINT32_MAX;X_IMG32(0x2d2fac)=UINT32_MAX;
        visited[2]=0;w32(0x2d2fb0+8,0);
    }
}
void arm_original(void) {f_00056670(&arm_context);}
#ifdef CLUSTER_FPU
XvClusterFpu arm_fpu;
const unsigned fpu_layout[]={sizeof(XvClusterFpu),offsetof(XvClusterFpu,slots),offsetof(XvClusterFpu,entry_fsp),offsetof(XvClusterFpu,fsw)};
void arm_candidate(void)
{
    arm_fpu.entry_fsp=arm_context.fsp;arm_fpu.fsw=arm_context.fsw;
    for(unsigned i=0;i<8;i++)
        memcpy(&arm_fpu.slots[i],&arm_context.st[(arm_context.fsp+i)&7],8);
    arm_admitted=xv_cluster_query_fpu(&geometry,&arm_input,&arm_result,&arm_fpu);
}
#else
void arm_candidate(void)
{arm_admitted=xv_cluster_query_direct(&geometry,&arm_input,&arm_result);}
#endif
void test_boot(void) {}
void xk_os_log(const char *fmt,...) {(void)fmt;}
void __assert_func(const char *file,int line,const char *function,const char *text)
{(void)file;(void)line;(void)function;(void)text;__builtin_trap();}
int *__errno(void){static int e;return &e;}
#endif
