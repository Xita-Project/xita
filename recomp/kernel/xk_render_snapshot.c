#include "xk_render_snapshot.h"
#include <string.h>

int xv_render_snapshots_init(xv_render_snapshots *q,xv_render_state *a,xv_render_state *b)
{
    if(!q)return 0;
    memset(q,0,sizeof *q);
    return xv_frame_snapshot_init(&q->exchange,a,b,sizeof *a);
}
int xv_render_snapshots_begin(xv_render_snapshots *q,uint64_t world,uint64_t tick)
{
    if(q->building)return 0;
    xv_render_state *s=xv_frame_snapshot_begin(&q->exchange);
    if(!s)return 0;
    s->world_generation=world;s->tick=tick;s->count=0;
    q->building=s;q->failed=0;return 1;
}
int xv_render_snapshots_add(xv_render_snapshots *q,uint32_t datum,uint32_t model,
                           unsigned nodes,const float *matrices)
{
    xv_render_state *s=q->building;
    if(!s||q->failed)return 0;
    if(datum==UINT32_MAX||model==UINT32_MAX||!nodes||nodes>XV_RENDER_SNAPSHOT_NODES||
       !matrices||s->count==XV_RENDER_SNAPSHOT_OBJECTS)goto failed;
    /* An object must occur exactly once, including objects whose model changed.
     * Separate view/material passes reference this pose rather than appending it. */
    for(unsigned i=0;i<s->count;i++)if(s->poses[i].datum==datum)goto failed;
    xv_render_pose *p=&s->poses[s->count++];
    p->datum=datum;p->model=model;p->nodes=nodes;
    memcpy(p->matrices,matrices,nodes*sizeof p->matrices[0]);
    return 1;
failed:
    q->failed=1;return 0;
}
void xv_render_snapshots_cancel(xv_render_snapshots *q)
{
    xv_frame_snapshot_cancel(&q->exchange);q->building=NULL;q->failed=0;
}
int xv_render_snapshots_publish(xv_render_snapshots *q)
{
    if(!q->building||q->failed) {xv_render_snapshots_cancel(q);return 0;}
    int ok=xv_frame_snapshot_publish(&q->exchange,sizeof(xv_render_state));
    if(!ok)xv_frame_snapshot_cancel(&q->exchange);
    q->building=NULL;q->failed=0;return ok;
}
const xv_render_state *xv_render_snapshots_acquire(xv_render_snapshots *q,uint64_t world)
{
    xv_snapshot_view view;
    if(!xv_frame_snapshot_acquire(&q->exchange,&view))return NULL;
    const xv_render_state *s=view.data;
    if(view.size!=sizeof *s||s->world_generation!=world) {
        xv_frame_snapshot_release(&q->exchange);return NULL;
    }
    return s;
}
void xv_render_snapshots_release(xv_render_snapshots *q)
{ xv_frame_snapshot_release(&q->exchange); }
const xv_render_pose *xv_render_snapshot_find(const xv_render_state *s,uint32_t datum,
                                            uint32_t model)
{
    if(!s)return NULL;
    for(unsigned i=0;i<s->count;i++)
        if(s->poses[i].datum==datum&&s->poses[i].model==model)return &s->poses[i];
    return NULL;
}
