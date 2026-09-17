/* Pump-only, opt-in publication of ordered query prefixes. Include after the
 * packet/counter definitions. Final retirement and every slot owner are unchanged. */
#pragma once
#if XV_QUERY_PREFIX_PUBLISH
#if !defined(XV_QUERY_BOUNDARY) || !defined(XV_FLARE_QUERY_OVERLAP) || !defined(XV_RUN_RECOMP)
#error XV_QUERY_PREFIX_PUBLISH requires recompiled exact query-boundary/history support
#endif
extern int xv_d3d_visibility_publication_safe(uint32_t,const uint32_t *,unsigned);
static unsigned g_query_prefix_published,g_query_prefix_pending_tail,g_query_prefix_history_blocked;
static void xv_pump_query_prefixes(void)
{
    uint32_t done=__atomic_load_n(&g_frame_completed,__ATOMIC_RELAXED);
    unsigned pending=g_frame_submitted-done, prior_count=0;
    uint32_t prior[XV_FRAME_SLOTS];
    if(!g_gfx.hle_ready || pending>=XV_FRAME_TICKETS || pending>XV_FRAME_SLOTS)return;
    for(unsigned i=1;i<=pending;i++) {
        uint32_t ticket=done+i;
        unsigned q=ticket&(XV_FRAME_TICKETS-1u);
        /* Only fully submitted packets have initialized fences and failure state.
         * Stop on any inconsistent owner; never skip an older unpublished result. */
        if(g_packets[q].fence.value!=ticket || !g_packets[q].fence.address ||
           g_packets[q].failed)return;
        uint32_t mesh=g_packets[q].mesh;
        if(mesh==UINT32_MAX)continue;
        if(!g_packets[q].visibility_completed && xv_d3d_has_visibility(mesh)) {
            /* The existing retirement function still owns oldest publication,
             * fallback and exceptional drains. Only proven younger prefixes enter. */
            if(i==1 || !g_packets[q].query_boundary ||
               !g_packets[q].visibility_fence.address ||
               g_packets[q].visibility_fence.value!=ticket ||
               __atomic_load_n(g_packets[q].visibility_fence.address,__ATOMIC_ACQUIRE)!=ticket)return;
            if(g_packets[q].prefix_history_blocked && g_packets[q].prefix_history_done==done)return;
            if(!xv_d3d_visibility_publication_safe(mesh,prior,prior_count)) {
                if(!g_packets[q].prefix_history_blocked) {
                    g_packets[q].prefix_history_blocked=1;
                    g_query_prefix_history_blocked++;
                }
                g_packets[q].prefix_history_done=done;
                return;
            }
            g_packets[q].visibility_us=sceKernelGetProcessTimeWide();
            if(__atomic_load_n(g_packets[q].fence.address,__ATOMIC_ACQUIRE)!=ticket)
                g_boundary_before_final++;
            unsigned oldest=(done+1u)&(XV_FRAME_TICKETS-1u);
            if(__atomic_load_n(g_packets[oldest].fence.address,__ATOMIC_ACQUIRE)!=done+1u)
                g_query_prefix_pending_tail++;
            xv_d3d_visibility_complete(mesh);
            g_packets[q].visibility_completed=1;
            g_query_prefix_published++;
        }
        prior[prior_count++]=mesh;
    }
}
#endif
