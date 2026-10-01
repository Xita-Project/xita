/* Native collision feature construction components. Not a guest entry hook.
 * Callers must validate memory ownership, non-aliasing, guest FP mode and
 * context effects before using these in the runtime. No runtime caller yet. */
#ifndef XK_FEATURE_BUILD_H
#define XK_FEATURE_BUILD_H
#include <stdint.h>
#include <math.h>
#include <string.h>

typedef struct {
    uint32_t object, surface;
    uint8_t flags_a, flags_b;
    uint16_t material;
} xk_feature_metadata;

static inline uint16_t xk_fb_count(const uint8_t *p) {
    uint16_t n; memcpy(&n,p,2); return n;
}
static inline void xk_fb_count_store(uint8_t *p,uint16_t n) { memcpy(p,&n,2); }
static inline void xk_fb_metadata(uint8_t *p,const xk_feature_metadata *m) {
    memcpy(p,&m->object,4); memcpy(p+4,&m->surface,4);
    p[8]=m->flags_a; p[9]=m->flags_b; memcpy(p+10,&m->material,2);
}

/* Host views over validated guest geometry. The caller must establish that
 * each byte span covers count*stride bytes and remains immutable for the call.
 * Guest page translation/physical alias checks are deliberately not hidden here. */
typedef struct {
    const uint8_t *vertices,*edges,*surfaces;
    uint32_t vertex_count,edge_count,surface_count;
    const uint8_t *planes;
    uint32_t plane_count;
} xk_feature_geometry;
static inline uint32_t xk_fb_u32(const uint8_t *p){
    uint32_t n;memcpy(&n,p,4);return n;
}
/* The eight-vertex result is published only after a complete closed walk.
 * Broken references, unrelated edges and oversized/non-closing loops decline. */
static inline int xk_feature_surface_points(const xk_feature_geometry *g,
        uint32_t surface,float points[8][3],unsigned *count){
    if(surface>=g->surface_count)return 0;
    uint32_t first=xk_fb_u32(g->surfaces+(size_t)surface*12+4),edge=first;
    float scratch[8][3];unsigned n=0;
    do {
        if(edge>=g->edge_count || n==8)return 0;
        const uint8_t *e=g->edges+(size_t)edge*24;
        unsigned side=xk_fb_u32(e+20)==surface;
        if(!side && xk_fb_u32(e+16)!=surface)return 0;
        uint32_t vertex=xk_fb_u32(e+4*side);
        if(vertex>=g->vertex_count)return 0;
        memcpy(scratch[n++],g->vertices+(size_t)vertex*16,12);
        edge=xk_fb_u32(e+8+4*side);
    }while(edge!=first);
    memcpy(points,scratch,n*12);*count=n;return 1;
}

/* Scale + column basis, then optional translation, with guest operation order.
 * Inputs finite, default FP mode, -ffp-contract=off; matrix must not alias out.
 * A point may alias out because the three coordinates are captured first. */
static inline void xk_feature_transform(float out[3],const float point[3],
        const float matrix[13],int translate){
    double x=point[0],y=point[1],z=point[2];
    if(matrix[0]!=1.0f){x*=matrix[0];y*=matrix[0];z*=matrix[0];}
    for(unsigned i=0;i<3;i++){
        double value=(z*matrix[7+i]+y*matrix[4+i])+x*matrix[1+i];
        if(translate)value+=matrix[10+i];
        out[i]=(float)value;
    }
}

/* Pure output component for 855F0. The output has at least 0x4408 bytes.
 * Counts must be in [0,256]; malformed signed guest counts require fallback.
 * Inputs and metadata must not alias output. Preserve unused record bytes.
 * Finite inputs, round-to-nearest FP environment, no fast-math required.
 * Context/x87 exception and stack effects belong to the future guest wrapper. */
static inline int xk_feature_build_vertex(uint8_t *out,
        const xk_feature_metadata *m,const float point[3],float height,float radius) {
    uint16_t spheres=xk_fb_count(out),capsules=xk_fb_count(out+2);
    if(spheres>256 || capsules>256) return 0;
    if(spheres<256) {
        uint8_t *p=out+8+28*spheres++;
        xk_fb_metadata(p,m); memcpy(p+12,point,12); memcpy(p+24,&radius,4);
    }
    if(height>0) {
        float bottom[3]={point[0],point[1],(float)((double)point[2]-(double)height)};
        if(spheres<256) {
            uint8_t *p=out+8+28*spheres++;
            xk_fb_metadata(p,m); memcpy(p+12,bottom,12); memcpy(p+24,&radius,4);
        }
        if(capsules<256) {
            uint8_t *p=out+0x1c08+40*capsules++;
            xk_fb_metadata(p,m); memcpy(p+12,bottom,12);
            memset(p+24,0,8); memcpy(p+32,&height,4); memcpy(p+36,&radius,4);
        }
    }
    xk_fb_count_store(out,spheres); xk_fb_count_store(out+2,capsules);
    return 1;
}
/* Geometry-to-feature component for 86440, with validated host views.
 * Every index and output count is checked before the writer mutates output. */
static inline int xk_feature_vertex_from_geometry(uint8_t *out,
        const xk_feature_geometry *g,uint32_t index,uint32_t object,
        const float *matrix,float height,float radius){
    if(index>=g->vertex_count)return 0;
    const uint8_t *v=g->vertices+(size_t)index*16;
    uint32_t edge=xk_fb_u32(v+12);
    if(edge>=g->edge_count)return 0;
    uint32_t surface=xk_fb_u32(g->edges+(size_t)edge*24+16);
    if(surface>=g->surface_count)return 0;
    const uint8_t *s=g->surfaces+(size_t)surface*12;
    xk_feature_metadata meta={object,object==UINT32_MAX?surface:UINT32_MAX,s[8],s[9],0};
    memcpy(&meta.material,s+10,2);
    float point[3];memcpy(point,v,12);
    if(matrix)xk_feature_transform(point,point,matrix,1);
    return xk_feature_build_vertex(out,&meta,point,height,radius);
}

/* Output component for 85020. Finite inputs and disjoint validated spans,
 * as above; output length >= 0xac08. At most eight polygon vertices fit.
 * Point order and unused padding are preserved. */
static inline int xk_feature_build_surface(uint8_t *out,
        const xk_feature_metadata *m,const float (*points)[3],unsigned count,
        const float plane[4],float height,float radius) {
    uint16_t n=xk_fb_count(out+4);
    if(n>256 || count>8) return 0;
    if(n==256) return 1;
    float ax=fabsf(plane[0]),ay=fabsf(plane[1]),az=fabsf(plane[2]);
    uint16_t axis=(az>=ay && az>=ax)?2:(ay>=ax?1:0);
    uint8_t positive=plane[axis]>0;
    unsigned u=(axis+1)%3,v=(axis+2)%3;
    if(!positive){unsigned t=u;u=v;v=t;}
    uint8_t *p=out+0x4408+104*n;
    xk_fb_metadata(p,m); memcpy(p+12,plane,16); memcpy(p+28,&radius,4);
    memcpy(p+32,&axis,2);p[34]=positive;
    uint32_t vertices=count;memcpy(p+36,&vertices,4);
    for(unsigned i=0;i<count;i++){
        memcpy(p+40+8*i,&points[i][u],4);
        memcpy(p+44+8*i,&points[i][v],4);
    }
    if(height>0 && plane[2]<0){
        float distance=(float)((double)plane[3]-(double)height*(double)plane[2]);
        memcpy(p+24,&distance,4);
        if(axis!=2){
            unsigned component=v==2?1:0;
            for(unsigned i=0;i<count;i++){
                float z=(float)((double)points[i][2]-(double)height);
                memcpy(p+40+8*i+4*component,&z,4);
            }
        }
    }
    xk_fb_count_store(out+4,n+1);
    return 1;
}
/* Complete geometric output path for 86170; guest execution state is separate.
 * Signed plane references negate both normal and distance. Matrix inputs and
 * all host spans must meet the same validated ownership/FP contract. */
static inline int xk_feature_surface_from_geometry(uint8_t *out,
        const xk_feature_geometry *g,uint32_t index,uint32_t object,
        const float *matrix,float height,float radius){
    if(index>=g->surface_count)return 0;
    const uint8_t *s=g->surfaces+(size_t)index*12;
    uint32_t reference=xk_fb_u32(s),plane_index=reference&0x7fffffffu;
    if(plane_index>=g->plane_count)return 0;
    float points[8][3],plane[4];unsigned count=0;
    if(!xk_feature_surface_points(g,index,points,&count))return 0;
    memcpy(plane,g->planes+(size_t)plane_index*16,16);
    if(reference>>31)for(unsigned i=0;i<4;i++)plane[i]=-plane[i];
    if(matrix){
        for(unsigned i=0;i<count;i++)xk_feature_transform(points[i],points[i],matrix,1);
        double x=plane[0],y=plane[1],z=plane[2],distance=plane[3];
        plane[0]=(float)((x*matrix[1]+z*matrix[7])+y*matrix[4]);
        plane[1]=(float)((x*matrix[2]+z*matrix[8])+y*matrix[5]);
        double nz=(y*matrix[6]+x*matrix[3])+z*matrix[9];
        plane[2]=(float)nz;
        /* Original retains unrounded Z for this dot product, but reloads X/Y. */
        plane[3]=(float)(((nz*matrix[12]+(double)plane[1]*matrix[11])+
                           distance*matrix[0])+(double)plane[0]*matrix[10]);
    }
    xk_feature_metadata meta={object,object==UINT32_MAX?index:UINT32_MAX,s[8],s[9],0};
    memcpy(&meta.material,s+10,2);
    return xk_feature_build_surface(out,&meta,points,count,plane,height,radius);
}

/* Output component for 851E0. Same validated finite/disjoint input contract.
 * epsilon is the owned executable's double-precision edge-length threshold. */
static inline int xk_feature_build_edge(uint8_t *out,
        const xk_feature_metadata *m,const float point[3],const float direction[3],
        float height,float radius,double epsilon) {
    uint16_t capsules=xk_fb_count(out+2),prisms=xk_fb_count(out+4);
    if(capsules>256 || prisms>256) return 0;
    unsigned copies=height>0?2:1;
    for(unsigned i=0;i<copies && capsules<256;i++){
        uint8_t *p=out+0x1c08+40*capsules++;
        xk_fb_metadata(p,m);memcpy(p+12,point,12);memcpy(p+24,direction,12);
        if(i){float z=(float)((double)point[2]-(double)height);memcpy(p+20,&z,4);}
        memcpy(p+36,&radius,4);
    }
    xk_fb_count_store(out+2,capsules);
    if(!(height>0))return 1;
    float nx=-direction[1],ny=direction[0];
    double length=sqrt((double)nx*nx+(double)ny*ny);
    if(length<epsilon || length==0)return 1;
    double inverse=1.0/length;
    nx=(float)((double)nx*inverse);ny=(float)((double)ny*inverse);
    float plane[4]={nx,ny,0,(float)((double)ny*point[1]+(double)nx*point[0])};
    float polygon[4][3];
    memcpy(polygon[0],point,12);
    for(unsigned j=0;j<3;j++)polygon[1][j]=(float)((double)point[j]+direction[j]);
    memcpy(polygon[2],polygon[1],12);memcpy(polygon[3],point,12);
    polygon[2][2]=(float)((double)polygon[2][2]-height);
    polygon[3][2]=(float)((double)polygon[3][2]-height);
    xk_feature_build_surface(out,m,polygon,4,plane,0,radius);
    float temp[3];memcpy(temp,polygon[1],12);memcpy(polygon[1],polygon[3],12);memcpy(polygon[3],temp,12);
    plane[0]=-plane[0];plane[1]=-plane[1];plane[3]=-plane[3];
    xk_feature_build_surface(out,m,polygon,4,plane,0,radius);
    return 1;
}
/* Geometric edge selection and output for 862A0. Returns 1 for a valid
 * omitted edge as well as an emitted edge; 0 requests fallback before writes.
 * Thresholds are supplied from the owned image, not guessed constants. */
static inline int xk_feature_edge_from_geometry(uint8_t *out,
        const xk_feature_geometry *g,uint32_t index,uint32_t object,
        const float *matrix,float height,float radius,
        float negative_threshold,float positive_threshold,double length_threshold){
    if(index>=g->edge_count)return 0;
    const uint8_t *e=g->edges+(size_t)index*24;
    uint32_t left=xk_fb_u32(e+16),right=xk_fb_u32(e+20);
    if(left>=g->surface_count || right>=g->surface_count)return 0;
    const uint8_t *s=g->surfaces+(size_t)left*12;
    uint32_t a=xk_fb_u32(s),b=xk_fb_u32(g->surfaces+(size_t)right*12);
    if(a==b)return 1;
    uint32_t v0=xk_fb_u32(e),v1=xk_fb_u32(e+4);
    if(v0>=g->vertex_count || v1>=g->vertex_count)return 0;
    float point[3],end[3],direction[3];
    memcpy(point,g->vertices+(size_t)v0*16,12);memcpy(end,g->vertices+(size_t)v1*16,12);
    for(unsigned i=0;i<3;i++)direction[i]=(float)((double)end[i]-point[i]);
    uint32_t pa=a&0x7fffffff,pb=b&0x7fffffff;
    if(pa>=g->plane_count || pb>=g->plane_count)return 0;
    if(pa!=pb){
        float p[3],q[3];memcpy(p,g->planes+(size_t)pa*16,12);memcpy(q,g->planes+(size_t)pb*16,12);
        double cx=(double)p[1]*q[2]-(double)p[2]*q[1];
        double cy=(double)p[2]*q[0]-(double)p[0]*q[2];
        double cz=(double)p[0]*q[1]-(double)p[1]*q[0];
        if((a>>31)==(b>>31)){
            double dot=(cz*direction[2]+cx*direction[0])+cy*direction[1];
            if(!(dot>negative_threshold))return 1;
        }else{
            double dot=(cz*direction[2]+cy*direction[1])+cx*direction[0];
            if(!(dot<positive_threshold))return 1;
        }
    }
    if(matrix){xk_feature_transform(direction,direction,matrix,0);xk_feature_transform(point,point,matrix,1);}
    xk_feature_metadata meta={object,object==UINT32_MAX?left:UINT32_MAX,s[8],s[9],0};
    memcpy(&meta.material,s+10,2);
    return xk_feature_build_edge(out,&meta,point,direction,height,radius,length_threshold);
}
typedef struct {
    const uint32_t *vertices,*edges,*surfaces;
    uint32_t vertex_count,edge_count,surface_count;
} xk_feature_query;
/* Full geometric output sequence for 868F0. Work must be private staging
 * storage initialized from the current output. On failure it may be partially
 * changed: discard it, never publish it. Guest memory/context is not touched.
 * All query and geometry spans must be immutable and disjoint from work. */
static inline int xk_feature_build_query(uint8_t *work,
        const xk_feature_geometry *g,const xk_feature_query *query,
        uint32_t object,const float *matrix,float height,float radius,
        float negative_threshold,float positive_threshold,double length_threshold){
    if(query->vertex_count>256 || query->edge_count>256 || query->surface_count>256)return 0;
    if(xk_fb_count(work)>256 || xk_fb_count(work+2)>256 || xk_fb_count(work+4)>256)return 0;
    for(uint32_t i=0;i<query->vertex_count;i++)
        if(!xk_feature_vertex_from_geometry(work,g,query->vertices[i],object,matrix,height,radius))return 0;
    for(uint32_t i=0;i<query->edge_count;i++)
        if(!xk_feature_edge_from_geometry(work,g,query->edges[i],object,matrix,height,radius,
                                        negative_threshold,positive_threshold,length_threshold))return 0;
    for(uint32_t i=0;i<query->surface_count;i++)
        if(!xk_feature_surface_from_geometry(work,g,query->surfaces[i],object,matrix,height,radius))return 0;
    return 1;
}
#endif
