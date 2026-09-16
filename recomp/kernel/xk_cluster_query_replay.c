/* Source-only original-prefix reconstruction. No live guest access or hook. */
#define XV_CLUSTER_REPLAY
#include "xk_cluster_query_impl.h"

int xv_cluster_query_replay(const XvClusterGeometry *g,const XvClusterInput *in,
    const XvClusterReplayLayout *layout,const xctx *entry,XvClusterResult *result,XvClusterReplay *out)
{
    if(!g||!in||!layout||!entry||!result||!out||!layout->adjacency_addresses||
       !layout->original_plane_indices||entry->r[4]<XV_CLUSTER_SCRATCH||
       (entry->r[4]&3)||entry->fsp>7||entry->preempt<0||in->budget!=(unsigned)entry->preempt)return 0;
    out->context=*entry;memset(out->dirty,0,sizeof out->dirty);
    XvReplayWork work;
    work.geometry=g;work.input=in;work.layout=layout;work.entry=entry;work.result=out;
    XvClusterFpu fp={.entry_fsp=entry->fsp,.fsw=entry->fsw};
    for(unsigned i=0;i<8;i++)memcpy(&fp.slots[i],&entry->st[(entry->fsp+i)&7],8);
    if(!replay_query(g,in,result,&fp,&work))return 0;
    xctx *c=&out->context;
    c->r[0]=result->count;c->r[4]=entry->r[4]-136;c->r[5]=layout->head_address;
    c->r[6]=entry->r[6];c->preempt-=result->backedges;
    c->fsw=fp.fsw;for(unsigned i=0;i<8;i++)memcpy(&c->st[(entry->fsp+i)&7],&fp.slots[i],8);
    replay_u32(&work,entry->r[4]-136,entry->r[5]);
    if(result->epoch==in->epoch){
        c->r[1]=(entry->r[1]&0xffff0000u)|(uint16_t)in->start;
        if(in->start==-1){X_FLAGS(XK_SUB,65535,65535,0,16);}
        else{unsigned mask=(fp.fsw>>8)&0x41;X_FLAGS(XK_LOGIC,0,0,mask,8);
            replay_u16(&work,entry->r[4]-128,(uint16_t)in->start);}
    }
    return 1;
}
