#define _POSIX_C_SOURCE 200809L
#include "../runtime/xv_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int drains,applied,benchmark,fail_resize;
static unsigned height;
int xv_benchmark_active(void) {return benchmark;}
void xv_logf(const char *format,...) {(void)format;}
void xv_present_drain(void) {drains++;}
unsigned xv_settings_resolution(unsigned h) {assert(drains);applied++;return height=fail_resize?544:h;}
void xv_settings_frame_cap(unsigned cap) {assert(drains && cap<=30);applied++;}
void xv_settings_pipeline(int on) {assert(drains && (on==0||on==1));applied++;}
void xv_ui_gxm_set_texture_options(int filter,int mip) {assert(drains && filter<=2 && mip<=1);applied++;}
static uint64_t now;
static void press(unsigned buttons)
{now+=100000;assert(xv_settings_input(buttons,now));now+=100000;assert(xv_settings_input(0,now));}
static char *config(void)
{
    static char buffer[4096];FILE *f=fopen("ux0:data/xita/xita.cfg","r");assert(f);
    size_t n=fread(buffer,1,sizeof buffer-1,f);buffer[n]=0;fclose(f);return buffer;
}
int main(void)
{
    char directory[]="/tmp/xita-graphics-XXXXXX";assert(mkdtemp(directory));assert(!chdir(directory));
    assert(!mkdir("ux0:data",0700));assert(!mkdir("ux0:data/xita",0700));
    FILE *f=fopen("ux0:data/xita/xita.cfg","w");assert(f);
    fputs("# keep me\nXV_RENDER_HEIGHT=360\nXV_TEX_MAXDIM=128\nCUSTOM=unchanged\n",f);fclose(f);
    xv_dash_graphics_view first,view;
    assert(!xv_settings_input(0,0));
    benchmark=1;press(XV_SETTINGS_TOGGLE);xv_settings_snapshot(&view);assert(!view.active);
    benchmark=0;press(XV_SETTINGS_TOGGLE);xv_settings_snapshot(&first);
    assert(first.active && !first.live && first.selected==0);
    press(XV_DASH_RIGHT);xv_settings_frame();assert(!drains && !applied);
    assert(strstr(config(),"XV_TEX_MAXDIM=256") && strstr(config(),"CUSTOM=unchanged"));
    press(XV_DASH_DOWN);press(XV_DASH_RIGHT);assert(!applied);
    xv_settings_snapshot(&view);assert(view.live && !strcmp(first.values[0],"LOW"));
    xv_settings_frame();assert(drains==1 && applied==1);
    xv_settings_frame();assert(drains==1); /* unchanged frames never drain */
    press(XV_DASH_DOWN);press(XV_DASH_DOWN);press(XV_DASH_RIGHT);
    xv_settings_frame();assert(height==400 && applied==2 && drains==2);
    fail_resize=1;press(XV_DASH_RIGHT);xv_settings_frame();xv_settings_snapshot(&view);
    assert(height==544 && strstr(view.status,"unavailable"));
    /* Save failure keeps the existing value and queues no runtime edit. */
    assert(!mkdir("ux0:data/xita/xita.cfg.tmp",0700));
    int old_applied=applied;press(XV_DASH_RIGHT);xv_settings_frame();assert(applied==old_applied);
    xv_settings_snapshot(&view);assert(!strcmp(view.values[3],"480P") && strstr(view.status,"Could not save"));
    press(XV_DASH_CIRCLE);xv_settings_snapshot(&view);assert(!view.active);
    assert(!xv_settings_input(0,now+1));
    press(XV_SETTINGS_TOGGLE);xv_settings_snapshot(&view);assert(view.active && view.selected==3);
    press(XV_DASH_CIRCLE);
    assert(!rmdir("ux0:data/xita/xita.cfg.tmp"));
    press(XV_SETTINGS_TOGGLE);
    for (unsigned i=3;i<12;++i) press(XV_DASH_DOWN);
    xv_settings_snapshot(&view);
    assert(view.count==13 && !view.live && !strcmp(view.names[view.selected-view.first],"Model detail"));
    assert(!strcmp(view.values[view.selected-view.first],"ORIGINAL"));
    press(XV_DASH_LEFT);xv_settings_frame();xv_settings_snapshot(&view);
    assert(applied==old_applied && !strcmp(view.values[view.selected-view.first],"BALANCED"));
    assert(strstr(config(),"XV_MODEL_DETAIL=1") && strstr(config(),"CUSTOM=unchanged"));
    press(XV_DASH_LEFT);xv_settings_snapshot(&view);
    assert(!strcmp(view.values[view.selected-view.first],"LOW") && strstr(config(),"XV_MODEL_DETAIL=0"));
    press(XV_DASH_CIRCLE);
    assert(!unlink("ux0:data/xita/xita.cfg"));
    assert(!rmdir("ux0:data/xita"));assert(!rmdir("ux0:data"));assert(!chdir("/"));assert(!rmdir(directory));
    puts("PASS: overlay controls, benchmark exclusion, persistent edits, relaunch labels, immutable views, deferred live changes, resize fallback and save failure");
    return 0;
}
