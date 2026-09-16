/* Private pump implementation; include after cmdlist_t and g_rt. Build-gated.
 * The proof scans every recorded potential writer. A fence is attached only to
 * the same existing RTT transition identified by exact exclusive cursors. */
#pragma once
#ifdef XV_QUERY_BOUNDARY
enum { QB_OFF, QB_READY, QB_NO_QUERY, QB_NO_WRITER, QB_NO_BOUNDARY,
       QB_BOUNDS, QB_OPEN, QB_SLOT, QB_KIND, QB_TARGET, QB_UI, QB_REASONS };
typedef struct {
    uint32_t frame, command, ui, reason, armed, used;
    SceGxmNotification fence;
} xv_query_boundary_plan;
static xv_query_boundary_plan g_query_boundary_plans[XV_NUM_LISTS];
static uint32_t g_query_boundary_reasons[QB_REASONS],g_query_boundary_attached,g_query_boundary_failed;
static unsigned qb_plan(const cmdlist_t *l,xv_query_boundary_plan *p)
{
    if(l->ncmds>XV_MAX_CMDS || l->nui>sizeof l->ui/sizeof l->ui[0] ||
       l->nvisibility>XV_VISIBILITY_PER_FRAME)return QB_BOUNDS;
    if(l->active_visibility)return QB_OPEN;
    if(!l->nvisibility)return QB_NO_QUERY;
    for(unsigned i=0;i<l->nvisibility;i++)
        if(l->visibility[i].serial && l->visibility[i].result_slot>=XV_VISIBILITY_IDS)return QB_SLOT;
    unsigned last=UINT32_MAX;
    for(unsigned i=0;i<l->ncmds;i++) {
        const cmd_t *c=&l->cmds[i];
        if(c->kind>1)return QB_KIND;
        if(c->pass>XV_RT_SLOTS || (c->pass && !g_rt[c->pass-1].rt))return QB_TARGET;
        /* Clear commands disable visibility in visibility_draw_state. All
         * draws remain potential writers even when empty or later skipped. */
        if(c->kind || !c->visibility)continue;
        unsigned slot=c->visibility-1u;
        if(slot>=l->nvisibility || !l->visibility[slot].serial)return QB_SLOT;
        last=i;
    }
    for(unsigned u=0;u<l->nui;u++) {
        if(l->ui[u].before>l->ncmds || (u && l->ui[u].before<l->ui[u-1].before))return QB_UI;
        unsigned t=l->ui[u].target;
        if(t>XV_RT_SLOTS || (t && !g_rt[t-1].rt))return QB_TARGET;
    }
    if(last==UINT32_MAX)return QB_NO_WRITER;
    /* Match the existing replay's UI-before-command and final backbuffer
     * transition. Validate the entire list above before accepting any prefix. */
    unsigned i=0,u=0,current=UINT32_MAX;
    for(;;) {
        int ui=u<l->nui && l->ui[u].before<=i;
        int done=i==l->ncmds && !ui;
        unsigned target=ui?l->ui[u].target:done?0:l->cmds[i].pass;
        if(target!=current) {
            if(current!=UINT32_MAX && i>last) {p->command=i;p->ui=u;return QB_READY;}
            current=target;
        }
        if(done)break;
        if(ui)u++;else i++;
    }
    return QB_NO_BOUNDARY;
}
/* Called for every real mesh packet before submission. Even disabled packets
 * clear the old plan so frame-number wrap cannot resurrect a stale fence. */
int xv_d3d_query_boundary_prepare(uint32_t frame,int enabled)
{
    xv_query_boundary_plan *p=&g_query_boundary_plans[frame%XV_NUM_LISTS];
    memset(p,0,sizeof *p);p->frame=frame;
    p->reason=enabled?qb_plan(g_lists[frame%XV_NUM_LISTS],p):QB_OFF;
    g_query_boundary_reasons[p->reason]++;
    return p->reason==QB_READY;
}
void xv_d3d_query_boundary_arm(uint32_t frame,const SceGxmNotification *fence)
{
    xv_query_boundary_plan *p=&g_query_boundary_plans[frame%XV_NUM_LISTS];
    if(p->frame==frame && p->reason==QB_READY && !p->armed && fence && fence->address) {
        p->fence=*fence;p->armed=1;
    }
}
static const SceGxmNotification *qb_notification(uint32_t frame,unsigned command,unsigned ui)
{
    xv_query_boundary_plan *p=&g_query_boundary_plans[frame%XV_NUM_LISTS];
    if(p->frame!=frame || !p->armed || p->used || p->command!=command || p->ui!=ui)return NULL;
    p->used=1;return &p->fence;
}
static void qb_submitted(uint32_t frame,unsigned command,unsigned ui,int result)
{
    xv_query_boundary_plan *p=&g_query_boundary_plans[frame%XV_NUM_LISTS];
    if(p->frame==frame && p->armed && p->used && p->command==command && p->ui==ui) {
        if(result<0)g_query_boundary_failed++;else g_query_boundary_attached++;
    }
}
void xv_d3d_query_boundary_report(void)
{
    XV_LOG("[query-boundary] packets off/ready/no-query/no-writer/no-boundary %u/%u/%u/%u/%u; declines bounds/open/slot/kind/target/ui %u/%u/%u/%u/%u/%u; existing-EndScene attached/failed %u/%u; original final ownership retained\n",
        g_query_boundary_reasons[QB_OFF],g_query_boundary_reasons[QB_READY],g_query_boundary_reasons[QB_NO_QUERY],g_query_boundary_reasons[QB_NO_WRITER],g_query_boundary_reasons[QB_NO_BOUNDARY],g_query_boundary_reasons[QB_BOUNDS],g_query_boundary_reasons[QB_OPEN],g_query_boundary_reasons[QB_SLOT],g_query_boundary_reasons[QB_KIND],g_query_boundary_reasons[QB_TARGET],g_query_boundary_reasons[QB_UI],g_query_boundary_attached,g_query_boundary_failed);
    memset(g_query_boundary_reasons,0,sizeof g_query_boundary_reasons);
    g_query_boundary_attached=g_query_boundary_failed=0;
}
#define XV_QB_NOTIFICATION(f,c,u) qb_notification(f,c,u)
#define XV_QB_SUBMITTED(f,c,u,r) qb_submitted(f,c,u,r)
#else
#define XV_QB_NOTIFICATION(f,c,u) NULL
#define XV_QB_SUBMITTED(f,c,u,r) ((void)0)
#endif
