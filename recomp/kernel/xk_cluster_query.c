/* Direct typed BSP traversal prototype. No guest-memory interception, copying,
 * callbacks, allocation, runtime registration, or live shared writes. */
#include "xk_cluster_query.h"
#include <math.h>
#include <string.h>
#include <limits.h>

static int finite32(float f)
{uint32_t bits;memcpy(&bits,&f,4);return (bits&0x7f800000u)!=0x7f800000u;}
int xv_cluster_geometry_valid(const XvClusterGeometry *g)
{
    if(!g||!g->clusters||!g->cluster_count||g->cluster_count>256||
       (g->adjacency_count&&!g->adjacency)||g->portal_count>32768||
       (g->portal_count&&!g->portals)||
       (g->vertex_count&&!g->vertices)||!g->distance_planes||!g->projection_planes)return 0;
    for(unsigned i=0;i<6;i++)for(unsigned k=0;k<2;k++)if(g->axes[i][k]<0||g->axes[i][k]>2)return 0;
    for(unsigned i=0;i<g->cluster_count;i++){
        const XvCluster *c=&g->clusters[i];
        if(c->count>32767||c->first>g->adjacency_count||c->count>g->adjacency_count-c->first)return 0;
        for(unsigned k=0;k<c->count;k++){
            unsigned p=g->adjacency[c->first+k];
            if(p>=g->portal_count||(g->portals[p].sides[0]!=(int)i&&g->portals[p].sides[1]!=(int)i))return 0;
        }
    }
    for(unsigned i=0;i<g->portal_count;i++){
        const XvPortal *p=&g->portals[i];
        if(p->sides[0]<0||p->sides[1]<0||(unsigned)p->sides[0]>=g->cluster_count||
           (unsigned)p->sides[1]>=g->cluster_count||p->plane>=g->distance_plane_count||
           p->plane>=g->projection_plane_count||p->vertices>128||
           p->first_vertex>g->vertex_count||p->vertices>g->vertex_count-p->first_vertex||
           !finite32(p->radius))return 0;
        for(unsigned k=0;k<3;k++)if(!finite32(p->center[k]))return 0;
    }
    for(unsigned i=0;i<g->vertex_count;i++)for(unsigned k=0;k<3;k++)if(!finite32(g->vertices[i].v[k]))return 0;
    for(unsigned j=0;j<2;j++){
        const XvPlane *p=j?g->projection_planes:g->distance_planes;
        unsigned n=j?g->projection_plane_count:g->distance_plane_count;
        for(unsigned i=0;i<n;i++)for(unsigned k=0;k<4;k++)if(!finite32(p[i].v[k]))return 0;
    }
    return 1;
}
static int edge(XvClusterResult *r,uint32_t budget)
{return ++r->backedges<budget;}

/* Return -1 for conservative exceptional/budget fallback, 0 reject, 1 pass.
 * Preserve ordered double arithmetic and original f32 spill boundaries. */
static int portal(const XvClusterGeometry *g,const XvClusterInput *in,
                  const XvPortal *p,XvClusterResult *r)
{
    const float *plane=g->distance_planes[p->plane].v,*center=in->center;
    double d=((double)plane[1]*center[1]+(double)plane[2]*center[2]);
    d=d+(double)center[0]*plane[0];d=d-plane[3];
    float spilled_d=(float)d;if(!finite32(spilled_d))return -1;
    if(!((double)in->radius>fabs(d)))return 0;
    double x=(double)p->center[0]-center[0],y=(double)p->center[1]-center[1],z=(double)p->center[2]-center[2];
    double radius=(double)in->radius+p->radius;
    if(!(radius*radius>(z*z+x*x)+y*y))return 0;
    plane=g->projection_planes[p->plane].v;
    double ax=fabs((double)plane[0]),ay=fabs((double)plane[1]),az=fabs((double)plane[2]);
    unsigned axis=az>=ay&&az>=ax?2:ay>=ax?1:0;
    const int16_t *axes=g->axes[axis*2+(plane[axis]>0.0f)];
    float projected[3];
    for(unsigned k=0;k<3;k++){
        projected[k]=(float)(-(double)spilled_d*plane[k]+center[k]);
        if(!finite32(projected[k]))return -1;
    }
    float poly[128][2];
    for(unsigned k=0;k<p->vertices;k++){
        const float *v=g->vertices[p->first_vertex+k].v;
        /* Original first coordinate is a raw f32 copy; second passes through
         * x87 load/store (observable when native FZ treats subnormals as zero). */
        poly[k][0]=v[axes[0]];
        volatile double second=(double)v[axes[1]];
        poly[k][1]=(float)second;
        if(k+1<p->vertices&&!edge(r,in->budget))return -1;
    }
    double radicand=(double)in->radius*in->radius-(double)spilled_d*spilled_d;
    float circle=(float)sqrt(radicand);
    if(!finite32(circle))return -1;
    float radius2=(float)((double)circle*circle);
    if(!finite32(radius2))return -1;
    for(unsigned k=0;k<p->vertices;k++){
        unsigned n=k+1==p->vertices?0:k+1;
        double dx=(double)poly[n][0]-poly[k][0],dy=(double)poly[n][1]-poly[k][1];
        double px=(double)projected[axes[0]]-poly[k][0],py=(double)projected[axes[1]]-poly[k][1];
        double len=dx*dx+dy*dy;float length2=(float)len;
        if(!finite32(length2))return -1;
        if(len>0.0){
            double cross=px*dy-py*dx;
            if(cross>0.0&&!((double)length2*radius2>=cross*cross))return 0;
        }
        if(k+1<p->vertices&&!edge(r,in->budget))return -1;
    }
    return 1;
}
int xv_cluster_query_direct(const XvClusterGeometry *g,const XvClusterInput *in,XvClusterResult *r)
{
    /* Geometry validation is amortized at immutable-snapshot construction. */
    if(!g||!in||!r||!in->visited||g->cluster_count>256||!g->cluster_count)return 0;
    memset(r,0,sizeof *r);r->epoch=in->epoch;
    if(in->start==-1)return 1;
    if(in->start<0||(unsigned)in->start>=g->cluster_count||!finite32(in->radius)||
       in->budget>INT_MAX)return 0;
    for(unsigned k=0;k<3;k++)if(!finite32(in->center[k]))return 0;
    if(!(in->radius>0.0f)){r->count=1;r->clusters[0]=(uint16_t)in->start;return 1;}
    r->epoch++;uint32_t seen[8]={0};
    for(unsigned i=0;i<g->cluster_count;i++)if(in->visited[i]==r->epoch)seen[i>>5]|=1u<<(i&31);
    struct {uint16_t cluster,next;} frames[256];unsigned depth=1;
    frames[0]=(typeof(frames[0])){(uint16_t)in->start,0};
    for(;;){
        unsigned id=frames[depth-1].cluster;
        const XvCluster *c=&g->clusters[id];
        if(!frames[depth-1].next){
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
            if(seen[other>>5]&(1u<<(other&31)))continue;
            r->portal_tests++;int pass=portal(g,in,p,r);
            if(pass<0)return 0;
            if(pass){
                if(depth==256)return 0;
                frames[depth++]=(typeof(frames[0])){(uint16_t)other,0};
            }
        }else if(!--depth)return 1;
    }
}
