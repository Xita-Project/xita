/* Private first-scene trial. Same retained target/UI traversal, suspended at
 * one existing EndScene. Only the pump calls these functions. Included from
 * xv_d3d.c; no shared mutable recording counts are read during the prefix. */
static int render_prefix_step(SceGxmContext *ctx, uint32_t frame,
    SceGxmRenderTarget *back, SceGxmSyncObject *sync,
    const SceGxmColorSurface *color, const SceGxmDepthStencilSurface *depth,
    unsigned back_width, unsigned back_height, int depth_tail_readonly,xv_record_prefix_state *p,int early)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    SceGxmDepthStencilSurface bd = p->ended?p->depth:*depth;
    #ifdef XV_DEPTH_STORE
    int ds_enabled=xv_depth_store_enabled()&&xv_depth_store_available();
    int ds_valid=ds_enabled?(early?1:ds_list_valid(l)):0,ds_stored=p->stored,ds_tail=!!depth_tail_readonly;
#else
    (void)depth_tail_readonly;
#endif
    sceGxmDepthStencilSurfaceSetForceStoreMode(&bd, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
    unsigned clear_slot=p->clear,current=p->current;
    int open = 0;
    static int queue_passes = -1;
    if (queue_passes < 0) {
        const char *e = getenv("XV_RT_QUEUE");
        queue_passes = e ? atoi(e) != 0 : 1;
        XV_LOG("RT pass queue: %s (XV_RT_QUEUE=%d)\n",
            queue_passes ? "enabled" : "disabled", queue_passes);
    }
    /* Fragment passes remain ordered on this context across frames. Resource
     * replacement drains owners; ordinary target reuse needs no CPU Finish. */
    unsigned i=p->command,u=p->ui;
    unsigned commands=early?p->packet.command+1:l->ncmds,uis=early?0:l->nui;
    for (;;) {
        int ui = u < uis && l->ui[u].before <= i;
        int done = i >= commands && !ui;
        unsigned target = ui ? l->ui[u].target : done ? 0 : l->cmds[i].pass;
        if (target > XV_RT_SLOTS || (target && !g_rt[target - 1].rt)) {
            XV_LOG("RT invalid target %u at frame %u command %u UI %u\n", target, frame, i, u);
            XV_VP_ERROR(frame,VP_ERR_TARGET);
            if (open) {
                int end_result=XV_RENDER_END(current, sceGxmEndScene(ctx, NULL, NULL));
                XV_VP_END(frame,i,u,end_result);
                (void)end_result;
                xv_render_profile_stage(XV_RENDER_TARGET_FINISH);
                sceGxmFinish(ctx);
                xv_render_profile_stage(XV_RENDER_SUBMIT);
            }
            return -1;
        }
        if (target != current) {
            if (open) {
                const SceGxmNotification *end_fence=XV_QB_NOTIFICATION(frame,i,u);
#ifdef XV_SCENE_CENSUS
                end_fence=xv_sc_end(current,i,u,end_fence,SC_QUERY);
#endif
                int err = XV_RENDER_END(current, sceGxmEndScene(ctx, NULL, end_fence));
#ifdef XV_SCENE_CENSUS
                xv_sc_ended(err);
#endif
                XV_QB_SUBMITTED(frame,i,u,err);
                XV_VP_END(frame,i,u,err);
                /* Successive scenes on this context preserve fragment order:
                 * later passes may sample earlier color/depth stores without
                 * blocking the CPU here. Clear slots, UI batches and geometry
                 * stay owned until this frame's final fragment notification.
                 * Keep a synchronous diagnostic fallback and drain on errors. */
                if (!queue_passes || err < 0) {
                    xv_render_profile_stage(XV_RENDER_TARGET_FINISH);
                    sceGxmFinish(ctx);
                    xv_render_profile_stage(XV_RENDER_SUBMIT);
                }
                if (err < 0) { XV_LOG("RT EndScene failed %08X\n", err); return -1; }
                XV_DS_STORED(current);
                open = 0;
                if(early) {
                    p->command=i;p->ui=u;p->clear=clear_slot;p->current=current;p->depth=bd;
#ifdef XV_DEPTH_STORE
                    p->stored=ds_stored;
#endif
                    p->ended=1;return 1;
                }
            }
            rt_alias_t *r = target ? &g_rt[target - 1] : NULL;
            XV_DS_SURFACE(l,i,u,target,&bd,back_width,back_height);
            int err = XV_RENDER_CALL(XV_RENDER_SCENE_BEGIN, sceGxmBeginScene(ctx, 0, r ? r->rt : back, NULL, NULL,
                r ? NULL : sync, r ? &r->color : color, r ? &r->depth : &bd));
            if (err < 0) {
                xv_render_profile_stage(XV_RENDER_TARGET_FINISH);
                sceGxmFinish(ctx);
                xv_render_profile_stage(XV_RENDER_SUBMIT);
                XV_LOG("RT BeginScene target %u failed %08X\n", target, err);
                XV_VP_ERROR(frame,VP_ERR_BEGIN);
                return -1;
            }
#ifdef XV_SCENE_CENSUS
            const SceGxmDepthStencilSurface *sc_depth=r?&r->depth:&bd;
            xv_sc_open(target,sceGxmDepthStencilSurfaceGetForceLoadMode(sc_depth),sceGxmDepthStencilSurfaceGetForceStoreMode(sc_depth));
#endif
            if (!target) {
                sceGxmDepthStencilSurfaceSetForceLoadMode(&bd, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED);
            }
            unsigned w = r ? r->w : back_width, h = r ? r->h : back_height;
            sceGxmSetViewport(ctx, w * 0.5f, w * 0.5f, h * 0.5f, -(float)h * 0.5f, 0.5f, 0.5f);
            current = target; open = 1;
        }
        if (done) break;
        if (ui) {
            visibility_draw_state(ctx,l,NULL);
            extern void xv_ui_gxm_replay_batch(SceGxmContext *, unsigned, unsigned, const void *);
            xv_ui_gxm_replay_batch(ctx, l->ui[u].frame, l->ui[u].batch, target ? g_rt[target - 1].mem : NULL);
            ++u;
        } else {
            /* Replay consecutive mesh commands together so the local state
             * cache survives adjacent draws. Preserve every UI/target boundary. */
            unsigned end=i+1;
            while (end<commands && l->cmds[end].pass==target &&
                   (u>=uis || end<l->ui[u].before)) ++end;
            render_range(ctx, l, i, end, &clear_slot, frame, XV_CLEAR_SLOTS);
            i=end;
        }
    }
    extern void xv_ui_gxm_replay_overlay(SceGxmContext *, unsigned);
    visibility_draw_state(ctx,l,NULL);
    xv_ui_gxm_replay_overlay(ctx, l->ui_frame);
    XV_VP_REPLAYED(frame,1);
    return 0;
}


int xv_d3d_prefix_begin(SceGxmContext *ctx,const xv_record_prefix *packet,const SceGxmNotification *fence,
    SceGxmRenderTarget *back,SceGxmSyncObject *sync,const SceGxmColorSurface *color,
    const SceGxmDepthStencilSurface *depth,unsigned width,unsigned height)
{
    xv_record_prefix_state *p=&g_record_prefix[packet->frame%XV_NUM_LISTS];
    p->packet=*packet;p->early=1;p->current=0xff;p->physical=1;
    xv_query_boundary_plan *q=&g_query_boundary_plans[packet->frame%XV_NUM_LISTS];
    memset(q,0,sizeof *q);q->frame=packet->frame;q->command=packet->command;
    q->reason=QB_READY;q->fence=*fence;q->armed=1;
    visibility_physical_prepare(ctx,packet->frame,width,height,packet->visibility);
    int result=render_prefix_step(ctx,packet->frame,back,sync,color,depth,width,height,0,p,1);
    p->early=0;return result;
}
int xv_d3d_prefix_query_valid(uint32_t frame)
{
    const xv_record_prefix_state *p=&g_record_prefix[frame%XV_NUM_LISTS];
    const xv_query_boundary_plan *q=&g_query_boundary_plans[frame%XV_NUM_LISTS];
    return p->ended && p->packet.frame==frame && !g_lists[frame%XV_NUM_LISTS]->nui &&
        q->reason==QB_READY && q->command==p->packet.command && !q->ui;
}
int xv_d3d_render_targets(SceGxmContext *ctx,uint32_t frame,SceGxmRenderTarget *back,SceGxmSyncObject *sync,
    const SceGxmColorSurface *color,const SceGxmDepthStencilSurface *depth,unsigned width,unsigned height,int tail)
{
    xv_record_prefix_state *p=&g_record_prefix[frame%XV_NUM_LISTS];
    if(p->physical)return render_prefix_step(ctx,frame,back,sync,color,depth,width,height,tail,p,0);
    return xv_d3d_render_targets_unprefixed(ctx,frame,back,sync,color,depth,width,height,tail);
}
