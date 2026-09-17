/* Include only after the retained command-list and RTT definitions. */
#pragma once
#ifdef XV_SCENE_CENSUS
static void sc_plan_scene(const cmdlist_t *l,unsigned target,unsigned width,unsigned height,
    unsigned first,unsigned command,unsigned first_ui,unsigned ui,unsigned ordinal)
{
    xv_sc_shape s={0};s.target=target;s.width=width;s.height=height;s.command=command;s.ui=ui;s.uis=ui-first_ui;
    for(unsigned i=first;i<command;i++) {
        if(l->cmds[i].kind)s.clears++;
        else {s.draws++;s.indices+=l->cmds[i].index_count;}
    }
    unsigned chosen=UINT32_MAX,wanted=s.draws?(xv_sc_ticket()+ordinal)%s.draws:0;
    for(unsigned i=first;i<command;i++)if(!l->cmds[i].kind && !wanted--) {chosen=i;break;}
    xv_sc_plan(&s,chosen);
}
void xv_d3d_scene_census_plan(uint32_t frame,unsigned width,unsigned height,int scaled)
{
    const cmdlist_t *l=g_lists[frame%XV_NUM_LISTS];
    if(l->ncmds>XV_MAX_CMDS || l->nui>sizeof l->ui/sizeof l->ui[0]) {xv_sc_decline(SC_BOUNDS);return;}
    for(unsigned i=0;i<l->ncmds;i++) {
        if(l->cmds[i].kind>1) {xv_sc_decline(SC_KIND);return;}
        unsigned t=l->cmds[i].pass;
        if(t>XV_RT_SLOTS || (t && !g_rt[t-1].rt)) {xv_sc_decline(SC_TARGET);return;}
    }
    for(unsigned u=0;u<l->nui;u++) {
        if(l->ui[u].before>l->ncmds || (u && l->ui[u].before<l->ui[u-1].before)) {xv_sc_decline(SC_UI);return;}
        unsigned t=l->ui[u].target;
        if(t>XV_RT_SLOTS || (t && !g_rt[t-1].rt)) {xv_sc_decline(SC_TARGET);return;}
    }
    unsigned i=0,u=0,current=UINT32_MAX,first=0,first_ui=0,ordinal=0;
    for(;;) {
        int ui=u<l->nui && l->ui[u].before<=i,done=i==l->ncmds && !ui;
        unsigned target=ui?l->ui[u].target:done?0:l->cmds[i].pass;
        if(target!=current) {
            if(current!=UINT32_MAX)sc_plan_scene(l,current,current?g_rt[current-1].w:width,current?g_rt[current-1].h:height,first,i,first_ui,u,ordinal++);
            current=target;first=i;first_ui=u;
        }
        if(done)break;
        if(ui)u++;else i++;
    }
    sc_plan_scene(l,0,width,height,first,i,first_ui,u,ordinal);
    if(scaled) {xv_sc_shape s={0};s.target=9;s.width=960;s.height=544;s.command=i;s.ui=u;xv_sc_plan(&s,UINT32_MAX);}
    xv_sc_planned();
}
#endif
