/* Owned BSP query arrays. No guest/runtime hook or implicit synchronization. */
#include "xk_cluster_snapshot.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The Xbox BSP decoder requires a little-endian host"
#endif

struct XvClusterSnapshot {
    XvClusterGeometry geometry;
    unsigned references, published;
    size_t bytes;
    void *arrays;
    /* Metadata follows: clusters, portals, original plane indices. */
};

static int read_span(XvClusterRead read,void *context,uint32_t base,
                     uint32_t offset,void *destination,size_t bytes)
{
    if(base>UINT32_MAX-offset)return 0;
    uint32_t address=base+offset;
    if(bytes&&(bytes-1>UINT32_MAX-address))return 0;
    return !bytes||read(context,address,destination,bytes);
}

/* All arithmetic is bounded by the caller's allocation limit before use. */
static int add_bytes(size_t *total,size_t count,size_t size,size_t limit)
{
    if(*total>limit||count>(limit-*total)/size)return 0;
    *total+=count*size;return 1;
}
static int compare_index(const void *a,const void *b)
{
    uint32_t x,y;memcpy(&x,a,4);memcpy(&y,b,4);
    return (x>y)-(x<y);
}
static unsigned plane_index(const uint32_t *indices,unsigned count,uint32_t id)
{
    unsigned lo=0,hi=count;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(indices[mid]<id)lo=mid+1;else hi=mid;}
    return lo;
}

XvClusterSnapshot *xv_cluster_snapshot_build(XvClusterRead read,void *context,
    const XvClusterSource *source,size_t max_bytes)
{
    if(!read||!source)return NULL;
    uint32_t clusters[2],portals[2],collision[2],distance[2],projection[2],zero;
    int16_t axes[6][2];
    if(!read_span(read,context,source->bsp,0x134,clusters,8)||
       !read_span(read,context,source->bsp,0x154,portals,8)||
       !read_span(read,context,source->bsp,0xb0,collision,8)||
       !collision[0]||!read_span(read,context,collision[1],0xc,distance,8)||
       !read_span(read,context,source->projection,0xc,projection,8)||
       !read_span(read,context,source->axes,0,axes,sizeof axes)||
       !read_span(read,context,source->zero,0,&zero,4)||zero||
       !clusters[0]||clusters[0]>256||portals[0]>32768)return NULL;
    size_t bytes=sizeof(XvClusterSnapshot);
    if(!add_bytes(&bytes,clusters[0],sizeof(XvCluster),max_bytes)||
       !add_bytes(&bytes,portals[0],sizeof(XvPortal)+sizeof(uint32_t),max_bytes))return NULL;
    XvClusterSnapshot *s=calloc(1,bytes);
    if(!s)return NULL;
    s->references=1;s->bytes=bytes;
    XvCluster *c=(XvCluster *)(s+1);
    XvPortal *p=(XvPortal *)(c+clusters[0]);
    uint32_t *indices=(uint32_t *)(p+portals[0]);
    unsigned adjacency_count=0,vertex_count=0;
    for(unsigned i=0;i<clusters[0];i++){
        uint32_t block[2];
        if(!read_span(read,context,clusters[1],i*104+0x5c,block,8)||
           block[0]>32767||!add_bytes(&bytes,block[0],2,max_bytes))goto fail;
        /* Keep the source pointer until the owned arrays have been allocated. */
        c[i]=(XvCluster){block[1],block[0]};adjacency_count+=block[0];
    }
    /* Align the following vertex/plane arrays to four bytes. */
    if(!add_bytes(&bytes,adjacency_count&1u,2,max_bytes))goto fail;
    for(unsigned i=0;i<portals[0];i++){
        unsigned char fields[24];uint32_t block[2];
        if(!read_span(read,context,portals[1],i*64,fields,sizeof fields)||
           !read_span(read,context,portals[1],i*64+0x34,block,8)||block[0]>128||
           !add_bytes(&bytes,block[0],sizeof(XvPoint),max_bytes))goto fail;
        memcpy(p[i].sides,fields,4);memcpy(&p[i].plane,fields+4,4);
        memcpy(p[i].center,fields+8,12);memcpy(&p[i].radius,fields+20,4);
        if(p[i].plane>=distance[0]||p[i].plane>=projection[0])goto fail;
        indices[i]=p[i].plane;p[i].first_vertex=block[1];p[i].vertices=block[0];
        vertex_count+=block[0];
    }
    qsort(indices,portals[0],sizeof *indices,compare_index);
    unsigned plane_count=0;
    for(unsigned i=0;i<portals[0];i++)
        if(!i||indices[i]!=indices[i-1])indices[plane_count++]=indices[i];
    unsigned copies=distance[1]==projection[1]?1:2;
    if(!add_bytes(&bytes,plane_count*copies,sizeof(XvPlane),max_bytes))goto fail;
    /* Even an empty graph supplies non-null plane array pointers. */
    size_t array_bytes=bytes-s->bytes;
    if(!array_bytes&&!add_bytes(&bytes,1,1,max_bytes))goto fail;
    s->arrays=calloc(1,bytes-s->bytes);
    if(!s->arrays)goto fail;
    s->bytes=bytes;
    uint16_t *adjacency=s->arrays;
    XvPoint *vertices=(XvPoint *)((unsigned char *)s->arrays+((adjacency_count+1u)&~1u)*2);
    XvPlane *planes=(XvPlane *)(vertices+vertex_count);
    XvPlane *other=copies==1?planes:planes+plane_count;
    unsigned cursor=0;
    for(unsigned i=0;i<clusters[0];i++){
        if(!read_span(read,context,c[i].first,0,adjacency+cursor,c[i].count*2))goto fail;
        c[i].first=cursor;cursor+=c[i].count;
    }
    cursor=0;
    for(unsigned i=0;i<portals[0];i++){
        if(!read_span(read,context,p[i].first_vertex,0,vertices+cursor,p[i].vertices*12))goto fail;
        p[i].first_vertex=cursor;cursor+=p[i].vertices;
        p[i].plane=plane_index(indices,plane_count,p[i].plane);
    }
    for(unsigned i=0;i<plane_count;i++){
        if(indices[i]>UINT32_MAX/16||
           !read_span(read,context,distance[1],indices[i]*16,&planes[i],16)||
           (copies==2&&!read_span(read,context,projection[1],indices[i]*16,&other[i],16)))goto fail;
    }
    s->geometry=(XvClusterGeometry){.clusters=c,.adjacency=adjacency,.portals=p,.vertices=vertices,
        .distance_planes=planes,.projection_planes=other,.cluster_count=clusters[0],
        .adjacency_count=adjacency_count,.portal_count=portals[0],.vertex_count=vertex_count,
        .distance_plane_count=plane_count,.projection_plane_count=plane_count};
    memcpy(s->geometry.axes,axes,sizeof axes);
    if(!xv_cluster_geometry_valid(&s->geometry))goto fail;
    return s;
fail:
    xv_cluster_snapshot_release(s);return NULL;
}

const XvClusterGeometry *xv_cluster_snapshot_geometry(const XvClusterSnapshot *s)
{return s?&s->geometry:NULL;}
size_t xv_cluster_snapshot_bytes(const XvClusterSnapshot *s)
{return s?s->bytes:0;}
void xv_cluster_snapshot_release(XvClusterSnapshot *s)
{
    if(s&&!--s->references){free(s->arrays);free(s);}
}
int xv_cluster_store_publish(XvClusterStore *store,XvClusterSnapshot *snapshot)
{
    if(!store||!snapshot||snapshot->published||snapshot->references!=1||
       store->generation==UINT64_MAX)return 0;
    XvClusterSnapshot *old=store->current;
    store->current=snapshot;store->generation++;snapshot->published=1;
    xv_cluster_snapshot_release(old);return 1;
}
void xv_cluster_store_retire(XvClusterStore *store)
{
    if(!store)return;
    XvClusterSnapshot *old=store->current;store->current=NULL;
    if(store->generation!=UINT64_MAX)store->generation++;
    xv_cluster_snapshot_release(old);
}
XvClusterLease xv_cluster_store_acquire(XvClusterStore *store)
{
    XvClusterLease result={0};
    if(store&&store->current&&store->current->references<UINT_MAX){
        store->current->references++;
        result=(XvClusterLease){store->current,store->generation};
    }
    return result;
}
int xv_cluster_store_current(const XvClusterStore *store,const XvClusterLease *lease)
{
    return store&&lease&&lease->snapshot&&store->current==lease->snapshot&&
        store->generation==lease->generation;
}
