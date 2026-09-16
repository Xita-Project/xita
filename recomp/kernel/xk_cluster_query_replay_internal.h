/* Shared-kernel trace helpers; only included by the replay translation unit. */
#include "xk_cluster_query_replay.h"
#include <string.h>
typedef struct {uint32_t ebx,capacity,output,count;} XvReplayFrame;
typedef struct {
    const XvClusterGeometry *geometry;
    const XvClusterInput *input;
    const XvClusterReplayLayout *layout;
    const xctx *entry;
    XvClusterReplay *result;
    XvReplayFrame frames[256];
    unsigned depth;
    uint32_t frame,portal_frame;
} XvReplayWork;

static inline __attribute__((always_inline)) void replay_u32(XvReplayWork *t,uint32_t address,uint32_t value)
{
    /* Proven offsets: aligned 2/4-byte stores, at most 256*44+1084+144
     * below the aligned entry SP. No operation reads unwritten scratch. */
    unsigned at=address-(t->entry->r[4]-XV_CLUSTER_SCRATCH);
    memcpy(t->result->scratch+at,&value,4);
    /* All word stores are four-byte aligned, so the two dirty bits always
     * fit in one mask word, including an offset of 60 within a 64-byte line. */
    t->result->dirty[at/64]|=3u<<((at/2)%32);
}
static inline __attribute__((always_inline)) void replay_u16(XvReplayWork *t,uint32_t address,uint16_t value)
{
    unsigned at=address-(t->entry->r[4]-XV_CLUSTER_SCRATCH);
    memcpy(t->result->scratch+at,&value,2);t->result->dirty[at/64]|=1u<<((at/2)%32);
}
static inline __attribute__((always_inline)) void replay_float(XvReplayWork *t,uint32_t at,float value)
{uint32_t bits;memcpy(&bits,&value,4);replay_u32(t,at,bits);}
static void replay_enter(XvReplayWork *t,unsigned id,unsigned depth)
{
    const XvClusterReplayLayout *l=t->layout;
    const xctx *e=t->entry;
    uint32_t f=e->r[4]-180-(depth-1)*44;
    XvReplayFrame *v=&t->frames[depth-1];
    uint32_t ebx,ebp,esi,edi,capacity,output;
    if(depth==1){ebx=e->r[3];ebp=l->head_address;esi=e->r[6];edi=e->r[7];
        capacity=64;output=e->r[4]-128;}
    else{
        XvReplayFrame *parent=v-1;
        ebx=parent->ebx;ebp=parent->capacity;edi=parent->output;
        /* The working ESI is the parent's current cluster-record address. */
        esi=t->result->context.r[6];capacity=ebp;output=edi;
    }
    replay_u32(t,f,edi);replay_u32(t,f+4,esi);replay_u32(t,f+8,ebp);replay_u32(t,f+12,ebx);
    replay_u16(t,f+16,(uint16_t)id);replay_u32(t,f+20,l->bsp);replay_u32(t,f+24,l->center_address);
    replay_u32(t,f+28,depth==1?0x566cb:0x52320);replay_float(t,f+32,t->input->radius);
    replay_u32(t,f+36,0);replay_u32(t,f+40,1);
    if((int16_t)capacity>0)replay_u16(t,output,(uint16_t)id);
    *v=(XvReplayFrame){(ebx&0xffff0000u)|id,capacity-1,
        output+((int16_t)capacity>0?2:0),1};
    t->depth=depth;t->frame=f;
    t->result->context.r[6]=l->clusters+id*104;
    t->result->context.r[1]=l->bsp;t->result->context.r[2]=0;
    t->result->context.f_cf=t->result->context.f_of=0;
}
static void replay_edge(XvReplayWork *t,unsigned id,const XvPortal *p)
{
    XvReplayFrame *v=&t->frames[t->depth-1];
    unsigned pid=(unsigned)(p-t->geometry->portals);
    v->ebx=p->sides[0]==(int)id?(v->ebx&0xffff0000u)|(uint16_t)p->sides[1]:
        (t->layout->bsp&0xffff0000u)|(uint16_t)p->sides[0];
    t->result->context.r[2]=(t->layout->adjacency_addresses[id]&0xffff0000u)|pid;
    t->result->context.f_cf=t->result->context.f_of=0;
}
static void replay_portal_begin(XvReplayWork *t,const XvPortal *p)
{
    XvReplayFrame *v=&t->frames[t->depth-1];
    uint32_t f=t->frame,a=f-1068;
    replay_float(t,f-4,t->input->radius);replay_u32(t,f-8,0x5230a);
    replay_u32(t,a,v->output);replay_u32(t,a+4,v->capacity);replay_u32(t,a+8,v->ebx);
    t->portal_frame=a-4;
    uint32_t plane=t->layout->original_plane_indices[p-t->geometry->portals];
    t->result->context.f_of=((plane<<4)>>31)^((plane>>28)&1);
}
static void replay_spill(XvReplayWork *t,float distance)
{replay_float(t,t->portal_frame+16,distance);}
static void replay_projection(XvReplayWork *t,const float projected[3],const int16_t axes[2])
{
    uint32_t b=t->portal_frame;
    replay_u32(t,b,t->result->context.r[6]);replay_u32(t,b-4,0x51f42);
    for(unsigned i=0;i<3;i++)replay_float(t,b+20+4*i,projected[i]);
    /* Original UV projection round-trips through f64 even for empty polygons. */
    volatile double u=(double)projected[axes[0]],v=(double)projected[axes[1]];
    replay_float(t,b+32,(float)u);replay_float(t,b+36,(float)v);
    t->result->context.f_of=0;
}
static void replay_polygon(XvReplayWork *t,const XvPortal *p,const float poly[128][2],float circle,float radius2)
{
    unsigned n=p->vertices,pid=(unsigned)(p-t->geometry->portals);
    uint32_t b=t->portal_frame,esi,ebp;
    for(unsigned i=0;i<n;i++){
        replay_float(t,b+40+8*i,poly[i][0]);replay_float(t,b+44+8*i,poly[i][1]);
    }
    if(n){esi=b+40+(n-1)*8;memcpy(&ebp,&poly[n-1][0],4);}
    else{esi=t->layout->projection_planes+16*t->layout->original_plane_indices[pid];ebp=t->layout->portals;}
    replay_u32(t,b-32,n);replay_u32(t,b-28,esi);replay_u32(t,b-24,ebp);
    replay_u32(t,b-20,t->layout->portals+64*pid);replay_u32(t,b-16,0x52014);replay_u32(t,b-12,n);
    replay_float(t,b-8,radius2);replay_float(t,b-4,circle);
    t->result->context.r[2]=n;
}
static void replay_length(XvReplayWork *t,float length,unsigned next)
{replay_float(t,t->portal_frame-4,length);t->result->context.r[2]=next;}
static void replay_leave(XvReplayWork *t,unsigned id,unsigned depth)
{
    xctx *c=&t->result->context;
    XvReplayFrame *v=&t->frames[depth-1];unsigned n=t->geometry->clusters[id].count;
    uint32_t f=t->entry->r[4]-180-(depth-1)*44;
    replay_u32(t,f+36,n);replay_u32(t,f+40,v->count);
    if(n){c->r[1]=n;X_FLAGS(XK_SUB,n,n,0,32);}
    else{c->r[1]=t->layout->bsp;X_FLAGS(XK_LOGIC,0,0,0,32);}
    if(depth>1){
        XvReplayFrame *parent=v-1;
        c->f_cf=parent->capacity<v->count;
        parent->capacity-=v->count;parent->output+=2*v->count;parent->count+=v->count;
        c->r[2]=v->count;t->depth=depth-1;t->frame=f+44;
        /* Recover the caller's ESI from the saved-register value, not from a
         * live guest read. It was saved before entering this node. */
        memcpy(&c->r[6],t->result->scratch+(f+4-(t->entry->r[4]-XV_CLUSTER_SCRATCH)),4);
    }
}
