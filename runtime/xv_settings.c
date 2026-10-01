#include "xv_settings.h"
#include "xv_benchmark.h"
#include <stdio.h>
#include <string.h>

static xv_dash_graphics *panel;
static uint32_t previous;
static uint64_t repeat_at;
static int active, releasing;
static const char *const live_keys[]={"XV_RENDER_HEIGHT","XV_TEX_FILTER","XV_MIP_SMOOTH","XV_FRAME_CAP","XV_TRIPLE_BUFFER"};
static int values[5];
static unsigned pending;

int xv_settings_input(uint32_t buttons, uint64_t now)
{
    uint32_t edge=buttons&~previous;
    if(buttons!=previous)repeat_at=now+350000;
    else if(now>=repeat_at) {
        edge|=buttons&(XV_DASH_UP|XV_DASH_DOWN);repeat_at=now+100000;
    }
    previous=buttons;
    if(releasing) { if(!buttons)releasing=0;return 1; }
    if(edge&XV_SETTINGS_TOGGLE) {
        if(!xv_benchmark_active()) {
            if(!panel)panel=xv_dash_graphics_create(NULL);
            if(panel)active=!active;
        }
        releasing=1;return 1;
    }
    if(!active)return 0;
    if(edge&XV_DASH_CIRCLE) { active=0;releasing=1;return 1; }
    int value;
    const char *key=xv_dash_graphics_input(panel,edge,&value);
    if(key) for(unsigned i=0;i<5;i++) if(!strcmp(key,live_keys[i])) {
        values[i]=value;pending|=1u<<i;
    }
    return 1;
}
void xv_settings_snapshot(xv_dash_graphics_view *view)
{
    if(active)xv_dash_graphics_snapshot(panel,view);
    else memset(view,0,sizeof(*view));
}
void xv_settings_frame(void)
{
    if(!pending)return;
    extern void xv_present_drain(void);
    extern unsigned xv_settings_resolution(unsigned height);
    extern void xv_settings_frame_cap(unsigned cap);
    extern void xv_settings_pipeline(int enabled);
    extern void xv_ui_gxm_set_texture_options(int filter,int mip);
    extern void xv_logf(const char *,...);
    /* One handoff for user edits only. No new waits on unchanged frames. */
    xv_present_drain();
    int failed=0;
    for(unsigned i=0;i<5;i++)if(pending&(1u<<i)) {
        if(i==0) failed=xv_settings_resolution((unsigned)values[i])!=(unsigned)values[i];
        else if(i==1)xv_ui_gxm_set_texture_options(values[i],-1);
        else if(i==2)xv_ui_gxm_set_texture_options(-1,values[i]);
        else if(i==3)xv_settings_frame_cap((unsigned)values[i]);
        else xv_settings_pipeline(values[i]);
        xv_logf("[settings] %s=%d applied%s\n",live_keys[i],values[i],failed?" with resolution fallback":"");
    }
    xv_dash_graphics_status(panel,failed?"Saved. Resolution unavailable; using native until relaunch.":"Saved and applied. Close with CIRCLE to return.");
    pending=0;
}
