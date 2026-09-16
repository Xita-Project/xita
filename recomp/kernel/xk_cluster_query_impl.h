/* Shared typed traversal and x87 reconstruction. The optional replay traces
 * private GP/scratch output as well. Neither mode publishes to live guest state. */
#include "xk_cluster_query.h"
#ifdef XV_CLUSTER_REPLAY
#include "xk_cluster_query_replay_internal.h"
#define TRACE(...) __VA_ARGS__
#define TRACE_ARG , XvReplayWork *trace
#define TRACE_PASS , trace
#define QUERY_NAME replay_query
#define QUERY_STORAGE static
#else
#define TRACE(...) ((void)0)
#define TRACE_ARG
#define TRACE_PASS
#define QUERY_NAME xv_cluster_query_fpu
#define QUERY_STORAGE
#endif
#include <limits.h>
#include <math.h>
#include <string.h>

static int finite32(float value)
{uint32_t bits;memcpy(&bits,&value,4);return (bits&0x7f800000u)!=0x7f800000u;}
static unsigned compare(XvClusterFpu *fp,unsigned relative,double a,double b)
{
    unsigned cc;
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\t"
                     "mov %0, #0\n\tit mi\n\tmovmi %0, #256\n\t"
                     "it eq\n\tmoveq %0, #16384\n\t"
                     "it vs\n\tmovvs %0, #17664"
                     : "=r"(cc) : "w"(a), "w"(b) : "cc");
#else
    cc=(isnan(a)||isnan(b))?0x4500:a<b?0x100:a==b?0x4000:0;
#endif
    /* Match the existing runtime's exact FSW update, including retained TOP
     * bits; reconstructing only the final condition code is insufficient. */
    fp->fsw=(uint16_t)((fp->fsw&~0x4700u)|cc|(((fp->entry_fsp+relative)&7u)<<11));
    return cc;
}
static int edge(XvClusterResult *r,uint32_t budget)
{return ++r->backedges<budget;}

static int portal(const XvClusterGeometry *g,const XvClusterInput *in,
                  const XvPortal *p,XvClusterResult *r,XvClusterFpu *fp TRACE_ARG)
{
    TRACE(replay_portal_begin(trace,p));
    const float *plane=g->distance_planes[p->plane].v,*center=in->center;
    double d=(double)plane[1]*center[1]+(double)plane[2]*center[2];
    d=d+(double)center[0]*plane[0];d=d-plane[3];
    float spilled_d=(float)d;if(!finite32(spilled_d))return -1;
    TRACE(replay_spill(trace,spilled_d));
    fp->slots[7]=fabs(d);fp->slots[6]=(double)in->radius;
    if(compare(fp,6,fp->slots[6],fp->slots[7])&0x4100)return 0;

    double x=(double)p->center[0]-center[0],y=(double)p->center[1]-center[1],z=(double)p->center[2]-center[2];
    double radius=(double)in->radius+p->radius;
    double sphere_distance=(z*z+x*x)+y*y,sphere_radius=radius*radius;
    fp->slots[7]=x;fp->slots[6]=y;fp->slots[5]=z;fp->slots[4]=radius;
    fp->slots[3]=sphere_distance;fp->slots[2]=sphere_radius;
    if(compare(fp,2,sphere_radius,sphere_distance)&0x4100)return 0;

    plane=g->projection_planes[p->plane].v;
    double ax=fabs((double)plane[0]),ay=fabs((double)plane[1]),az=fabs((double)plane[2]);
    fp->slots[7]=ax;fp->slots[6]=ay;fp->slots[5]=az;
    unsigned axis;
    if(!(compare(fp,5,az,ay)&0x100)&&!(compare(fp,5,az,ax)&0x100))axis=2;
    else axis=(compare(fp,6,ay,ax)&0x100)?0:1;
    fp->slots[7]=(double)plane[axis];
    unsigned positive=!(compare(fp,7,fp->slots[7],0.0)&0x4100);
    const int16_t *axes=g->axes[axis*2+positive];
    float projected[3],poly[128][2];
    for(unsigned k=0;k<3;k++){
        projected[k]=(float)(-(double)spilled_d*plane[k]+center[k]);
        if(!finite32(projected[k]))return -1;
    }
    TRACE(replay_projection(trace,projected,axes));
    for(unsigned k=0;k<p->vertices;k++){
        const float *v=g->vertices[p->first_vertex+k].v;
        poly[k][0]=v[axes[0]];volatile double second=(double)v[axes[1]];
        poly[k][1]=(float)second;
        if(k+1<p->vertices&&!edge(r,in->budget))return -1;
    }
    double distance2=(double)spilled_d*spilled_d;
    float circle=(float)sqrt((double)in->radius*in->radius-distance2);
    if(!finite32(circle))return -1;
    double square=(double)circle*circle;float radius2=(float)square;
    if(!finite32(radius2))return -1;
    TRACE(replay_polygon(trace,p,poly,circle,radius2));
    fp->slots[6]=distance2;fp->slots[7]=square;
    for(unsigned k=0;k<p->vertices;k++){
        unsigned n=k+1==p->vertices?0:k+1;
        double dx=(double)poly[n][0]-poly[k][0],dy=(double)poly[n][1]-poly[k][1];
        double px=(double)projected[axes[0]]-poly[k][0],py=(double)projected[axes[1]]-poly[k][1];
        double len=dx*dx+dy*dy;float length2=(float)len;
        if(!finite32(length2))return -1;
        TRACE(replay_length(trace,length2,n));
        fp->slots[7]=dx;fp->slots[6]=dy;fp->slots[5]=px;fp->slots[4]=py;
        fp->slots[3]=len;fp->slots[2]=dy*dy;
        if(!(compare(fp,3,len,0.0)&0x4400)){
            double cross=px*dy-py*dx;
            fp->slots[7]=cross;fp->slots[5]=cross;fp->slots[4]=py*dx;
            if(!(compare(fp,7,cross,0.0)&0x4100)){
                double lhs=(double)length2*radius2,rhs=cross*cross;
                fp->slots[6]=rhs;fp->slots[5]=lhs;
                if(compare(fp,5,lhs,rhs)&0x100)return 0;
            }
        }
        if(k+1<p->vertices&&!edge(r,in->budget))return -1;
    }
    return 1;
}

QUERY_STORAGE int QUERY_NAME(const XvClusterGeometry *g,const XvClusterInput *in,
                         XvClusterResult *r,XvClusterFpu *fp TRACE_ARG)
{
    if(!g||!in||!r||!fp||fp->entry_fsp>7||!in->visited||g->cluster_count>256||!g->cluster_count)return 0;
    memset(r,0,sizeof *r);r->epoch=in->epoch;
    if(in->start==-1)return 1;
    if(in->start<0||(unsigned)in->start>=g->cluster_count||!finite32(in->radius)||in->budget>INT_MAX)return 0;
    for(unsigned k=0;k<3;k++)if(!finite32(in->center[k]))return 0;
    fp->slots[7]=(double)in->radius;
    if(compare(fp,7,fp->slots[7],0.0)&0x4100){r->count=1;r->clusters[0]=(uint16_t)in->start;return 1;}
    r->epoch++;uint32_t seen[8]={0};
    for(unsigned i=0;i<g->cluster_count;i++)if(in->visited[i]==r->epoch)seen[i>>5]|=1u<<(i&31);
    struct {uint16_t cluster,next;} frames[256];unsigned depth=1;
    frames[0]=(typeof(frames[0])){(uint16_t)in->start,0};
    for(;;){
        unsigned id=frames[depth-1].cluster;
        const XvCluster *c=&g->clusters[id];
        if(!frames[depth-1].next){
            TRACE(replay_enter(trace,id,depth));
            if(r->count<64)r->clusters[r->count]=(uint16_t)id;
            r->count++;r->maximum_depth=depth>r->maximum_depth?depth:r->maximum_depth;
            uint32_t bit=1u<<(id&31);
            if(!(seen[id>>5]&bit)){seen[id>>5]|=bit;r->changed[id>>5]|=bit;}
        }
        if(frames[depth-1].next<c->count){
            unsigned next=frames[depth-1].next++;
            if(next&&!edge(r,in->budget))return 0;
            const XvPortal *p=&g->portals[g->adjacency[c->first+next]];
            unsigned other=p->sides[p->sides[0]==(int)id];
            TRACE(replay_edge(trace,id,p));
            if(seen[other>>5]&(1u<<(other&31)))continue;
            r->portal_tests++;int pass=portal(g,in,p,r,fp TRACE_PASS);
            if(pass<0)return 0;
            if(pass){
                if(depth==256)return 0;
                frames[depth++]=(typeof(frames[0])){(uint16_t)other,0};
            }
        }else{
            TRACE(replay_leave(trace,id,depth));
            if(!--depth)return 1;
        }
    }
}

#undef TRACE
#undef TRACE_ARG
#undef TRACE_PASS
#undef QUERY_NAME
#undef QUERY_STORAGE
