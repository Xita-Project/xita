#define _POSIX_C_SOURCE 200809L
#include "xv_dash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const uint32_t *script;
    unsigned length, step, frames;
    int previews, fail_present, touch_page, analog;
} harness;
static int poll_input(void *user, xv_dash_input *in)
{
    harness *h = user;
    if (h->step == h->length) return 1;
    in->buttons = h->script[h->step++];
    if (h->touch_page && h->step == 1) {
        in->front_count = 1; in->front[0] = (xv_dash_touch){100,280};
    }
    if (h->analog && h->step == 1) in->ly = 100;
    return 0;
}
static int present(void *user, xv_dash_framebuffer *fb)
{
    harness *h = user;
    h->frames++;
    if (h->fail_present) return -1;
    if (h->previews) {
        char name[64];
        snprintf(name,sizeof(name),"out/frame%u.ppm",h->frames);
        FILE *f = fopen(name,"wb"); assert(f);
        fprintf(f,"P6\n%d %d\n255\n",fb->width,fb->height);
        for (int y = 0; y < fb->height; y++) for (int x = 0; x < fb->width; x++) {
            uint32_t p = fb->pixels[y*fb->pitch+x];
            unsigned char rgb[] = {p & 255,(p >> 8) & 255,(p >> 16) & 255};
            assert(fwrite(rgb,1,3,f) == 3);
        }
        assert(!fclose(f));
    }
    /* Verify rendering respects row padding. */
    for (int y = 0; y < fb->height; y++) for (int x = fb->width; x < fb->pitch; x++)
        assert(fb->pixels[y*fb->pitch+x] == 0x12345678u);
    return 0;
}
static void put(const char *root, const char *leaf, const char *data)
{
    char path[1024]; snprintf(path,sizeof(path),"%s/%s",root,leaf);
    FILE *f = fopen(path,"w"); assert(f); assert(fputs(data,f) >= 0); assert(!fclose(f));
}
static void subdir(const char *root, const char *leaf)
{
    char path[1024]; snprintf(path,sizeof(path),"%s/%s",root,leaf); assert(!mkdir(path,0700));
}
static int run(xv_dash_config *cfg, harness *h, const uint32_t *script, unsigned len, xv_dash_result *result)
{
    h->script = script; h->length = len; h->step = h->frames = 0;
    return xv_dash_run(cfg,result);
}
int main(void)
{
    char root[] = "out/fixture-XXXXXX"; assert(mkdtemp(root));
    subdir(root,"haloce"); subdir(root,"haloce/maps"); subdir(root,"save");
    /* Empty presence markers only: these are not game content. */
    put(root,"halo_image.bin",""); put(root,"haloce/maps/ui.map","");
    put(root,"haloce/maps/a10.map",""); put(root,"haloce/maps/bloodgulch.map","");
    put(root,"save/checkpoint.sav","");
    put(root,"xita.cfg","# keep this comment\nUNKNOWN=hello\n XV_FPS = 0 # overlay\nXV_FPS=0\nXV_VBLANK_HZ=120\nTAIL=keep");
    uint32_t *pixels = malloc(976*544*sizeof(*pixels)); assert(pixels);
    for (int i = 0; i < 976*544; i++) pixels[i] = 0x12345678u;
    harness h = {0};
    xv_dash_config cfg = {{pixels,960,544,976},root,&h,poll_input,present,0};
    xv_dash_result result;
    const uint32_t preview[] = {0,XV_DASH_DOWN,XV_DASH_CROSS};
    h.previews = 1;
    assert(run(&cfg,&h,preview,3,&result) == 1 && h.frames == 3);
    h.previews = 0;
    const uint32_t campaign[] = {XV_DASH_CROSS,0,XV_DASH_CROSS,0,XV_DASH_CROSS,0,XV_DASH_CROSS};
    assert(run(&cfg,&h,campaign,7,&result) == 0);
    assert(!strcmp(result.game_id,"haloce") && result.mode == XV_DASH_CAMPAIGN && !strcmp(result.map,"a10") && !result.is_save);
    const uint32_t multi[] = {XV_DASH_CROSS,0,XV_DASH_CROSS,XV_DASH_DOWN,XV_DASH_CROSS,0,XV_DASH_CROSS};
    assert(run(&cfg,&h,multi,7,&result) == 0 && result.mode == XV_DASH_MULTIPLAYER && !strcmp(result.map,"bloodgulch"));
    const uint32_t save[] = {XV_DASH_CROSS,0,XV_DASH_CROSS,XV_DASH_DOWN,0,XV_DASH_DOWN,XV_DASH_CROSS,0,XV_DASH_CROSS};
    assert(run(&cfg,&h,save,9,&result) == 0 && result.is_save && !strcmp(result.map,"checkpoint.sav"));
    const uint32_t game_settings[] = {XV_DASH_CROSS,0,XV_DASH_CROSS,XV_DASH_UP,XV_DASH_CROSS};
    assert(run(&cfg,&h,game_settings,5,&result) == 0 && result.mode == XV_DASH_SETTINGS && !result.map[0]);
    const uint32_t settings[] = {XV_DASH_DOWN,XV_DASH_CROSS,0,XV_DASH_CROSS};
    assert(run(&cfg,&h,settings,4,&result) == 1);
    char path[1024], data[2048]; snprintf(path,sizeof(path),"%s/xita.cfg",root);
    FILE *f = fopen(path,"r"); assert(f);
    size_t n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"# keep this comment\nUNKNOWN=hello\n XV_FPS = 1 # overlay\nXV_FPS=1\n"));
    assert(strstr(data,"TAIL=keep\n") && strstr(data,"XV_BC_MIPS=0\n") && strstr(data,"XV_PROF=0\n"));
    snprintf(path,sizeof(path),"%s/haloce/maps/ui.map",root); assert(!unlink(path));
    assert(run(&cfg,&h,campaign,7,&result) == 1 && !result.game_id[0]);
    const uint32_t planned[] = {XV_DASH_CROSS,XV_DASH_DOWN,XV_DASH_CROSS};
    assert(run(&cfg,&h,planned,3,&result) == 1 && !result.game_id[0]);
    const uint32_t back[] = {XV_DASH_CROSS,0,XV_DASH_CIRCLE,XV_DASH_DOWN,XV_DASH_CROSS};
    assert(run(&cfg,&h,back,5,&result) == 1);
    h.fail_present = 1;
    assert(run(&cfg,&h,preview,3,&result) == -1 && !result.game_id[0]);
    h.fail_present = 0;
    const uint32_t enter[] = {0,XV_DASH_CROSS,0,XV_DASH_CROSS};
    h.touch_page = 1;
    assert(run(&cfg,&h,enter,4,&result) == 1);
    h.touch_page = 0; h.analog = 1;
    assert(run(&cfg,&h,enter,4,&result) == 1);
    h.analog = 0;
    /* Embedded dashboard: launch is one button, and edits only rewrite the
     * chosen setting. Diagnostic keys, custom values and comments survive. */
    put(root,"haloce/maps/ui.map","");
    put(root,"xita.cfg","# user settings\nXV_THREADS=1\nXV_PROF=1\nXV_VBLANK_HZ=60\n XV_TEX_MAXDIM = 256 # detail\nXV_TEX_MAXDIM=256\nXV_VOLUME=50\nUNKNOWN=keep\n");
    cfg.simple_launcher = 1;
    const uint32_t launch[] = {0,XV_DASH_CROSS};
    assert(run(&cfg,&h,launch,2,&result) == 0 && !strcmp(result.game_id,"haloce") && !result.map[0] && !result.is_save);
    const uint32_t texture[] = {XV_DASH_DOWN,XV_DASH_CROSS,XV_DASH_LEFT,0,XV_DASH_CIRCLE,XV_DASH_UP,XV_DASH_CROSS};
    assert(run(&cfg,&h,texture,7,&result) == 0);
    snprintf(path,sizeof(path),"%s/xita.cfg",root);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data," XV_TEX_MAXDIM = 128 # detail\nXV_TEX_MAXDIM=128\n"));
    assert(strstr(data,"XV_PROF=1\nXV_VBLANK_HZ=60\n") && strstr(data,"XV_THREADS=1\n") && strstr(data,"UNKNOWN=keep\n"));
    assert(!strstr(data,"XV_FPS=") && !strstr(data,"XV_CPU=") && !strstr(data,"XV_BC_MIPS="));
    /* Reopening reads the persisted value before applying the next change. */
    assert(run(&cfg,&h,texture,7,&result) == 0);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_TEX_MAXDIM=64\n"));
    const uint32_t texture_raise[] = {XV_DASH_DOWN,XV_DASH_CROSS,XV_DASH_RIGHT,0,XV_DASH_RIGHT,0,XV_DASH_CIRCLE,XV_DASH_UP,XV_DASH_CROSS};
    assert(run(&cfg,&h,texture_raise,9,&result) == 0);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_TEX_MAXDIM=256\n"));
    const uint32_t filtering[] = {XV_DASH_DOWN,XV_DASH_CROSS,XV_DASH_DOWN,XV_DASH_RIGHT,XV_DASH_DOWN,XV_DASH_RIGHT,XV_DASH_CIRCLE,XV_DASH_UP,XV_DASH_CROSS};
    assert(run(&cfg,&h,filtering,9,&result) == 0);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_TEX_FILTER=1\n") && strstr(data,"XV_MIP_SMOOTH=0\n"));
    assert(strstr(data,"XV_TEX_MAXDIM=256\n") && strstr(data,"XV_THREADS=1\n"));
    const uint32_t resolution[] = {XV_DASH_DOWN,XV_DASH_CROSS,XV_DASH_DOWN,0,XV_DASH_DOWN,0,XV_DASH_DOWN,XV_DASH_RIGHT};
    assert(run(&cfg,&h,resolution,8,&result) == 1);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_RENDER_HEIGHT=360\n") && strstr(data,"XV_TEX_MAXDIM=256\n"));
    assert(run(&cfg,&h,resolution,8,&result) == 1);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_RENDER_HEIGHT=400\n") && strstr(data,"XV_MIP_SMOOTH=0\n"));
    /* Graphics scrolls through every visual control and only saves the edited key. */
    const struct { unsigned page,row; const char *expected; } quality[] = {
        {1,4,"XV_MATERIAL_QUALITY=1\n"}, {1,5,"XV_GLOW_QUALITY=1\n"},
        {1,6,"XV_PARTICLE_QUALITY=1\n"}, {1,7,"XV_DECAL_SECONDS=60\n"},
        {1,8,"XV_DECAL_LIMIT=128\n"}, {1,9,"XV_FRAME_CAP=30\n"}, {5,0,"XV_CPU_MHZ=500\n"},
        {1,10,"XV_EXTENDED_BC=1\n"}, {1,11,"XV_TRIPLE_BUFFER=1\n"}, {1,12,"XV_MODEL_DETAIL=1\n"}
    };
    for (unsigned q=0;q<sizeof quality/sizeof quality[0];++q) {
        uint32_t script[40]; unsigned count=0;
        for (unsigned j=0;j<quality[q].page;++j) { script[count++]=XV_DASH_DOWN; script[count++]=0; }
        script[count++]=XV_DASH_CROSS; script[count++]=0;
        for (unsigned j=0;j<quality[q].row;++j) { script[count++]=XV_DASH_DOWN; script[count++]=0; }
        script[count++]=XV_DASH_LEFT;
        assert(run(&cfg,&h,script,count,&result)==1);
        f=fopen(path,"r"); assert(f); n=fread(data,1,sizeof data-1,f);data[n]=0;fclose(f);
        assert(strstr(data,quality[q].expected));
        assert(strstr(data,"XV_THREADS=1\n") && strstr(data,"UNKNOWN=keep\n"));
    }
    /* Wrap to the last Graphics row, return to the first, then leave and launch. */
    const uint32_t graphics_wrap[] = {XV_DASH_DOWN,XV_DASH_CROSS,XV_DASH_UP,
        XV_DASH_RIGHT,XV_DASH_DOWN,XV_DASH_CIRCLE,XV_DASH_UP,XV_DASH_CROSS};
    assert(run(&cfg,&h,graphics_wrap,8,&result) == 0);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_MODEL_DETAIL=2\n") && strstr(data,"XV_TRIPLE_BUFFER=1\n") && strstr(data,"XV_EXTENDED_BC=1\n") && strstr(data,"XV_TEX_MAXDIM=256\n"));
    /* About/license is readable without game files and never edits settings. */
    char before_license[2048]; strcpy(before_license,data);
    const uint32_t license[] = {XV_DASH_UP,XV_DASH_CROSS,XV_DASH_DOWN,0,
        XV_DASH_RIGHT,0,XV_DASH_LEFT,XV_DASH_UP,XV_DASH_CIRCLE,XV_DASH_DOWN,XV_DASH_CROSS};
    assert(run(&cfg,&h,license,sizeof license/sizeof license[0],&result)==0);
    f=fopen(path,"r");assert(f);n=fread(data,1,sizeof data-1,f);data[n]=0;fclose(f);
    assert(!strcmp(before_license,data));
    const uint32_t volume[] = {XV_DASH_DOWN,0,XV_DASH_DOWN,XV_DASH_CROSS,XV_DASH_LEFT};
    assert(run(&cfg,&h,volume,5,&result) == 1);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_VOLUME=40\n"));
    /* An unwritable staging path must leave the original file intact. */
    subdir(root,"xita.cfg.tmp");
    assert(run(&cfg,&h,texture,7,&result) == 0);
    f = fopen(path,"r"); assert(f); n = fread(data,1,sizeof(data)-1,f); data[n] = 0; fclose(f);
    assert(strstr(data,"XV_TEX_MAXDIM=256\n"));
    snprintf(path,sizeof(path),"%s/xita.cfg.tmp",root); /* remove() may remove an empty directory after the failed open */
    if (access(path,F_OK) == 0) assert(!rmdir(path));
    snprintf(path,sizeof(path),"%s/haloce/maps/ui.map",root); assert(!unlink(path));
    assert(run(&cfg,&h,launch,2,&result) == 1 && !result.game_id[0]);
    assert(run(&cfg,&h,license,sizeof license/sizeof license[0],&result)==1 && !result.game_id[0]);
    assert(xv_dash_run(NULL,&result) == -1);
    free(pixels);
    const char *files[] = {"halo_image.bin","haloce/maps/a10.map","haloce/maps/bloodgulch.map","save/checkpoint.sav","xita.cfg"};
    for (unsigned i = 0; i < sizeof(files)/sizeof(files[0]); i++) {
        snprintf(path,sizeof(path),"%s/%s",root,files[i]); assert(!unlink(path));
    }
    const char *dirs[] = {"haloce/maps","haloce","save"};
    for (unsigned i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        snprintf(path,sizeof(path),"%s/%s",root,dirs[i]); assert(!rmdir(path));
    }
    assert(!rmdir(root));
    puts("PASS: three PPM previews; campaign, multiplayer, saves, game settings, missing files, planned game, config preservation, framebuffer padding");
    puts("PASS: embedded Launch Game, settings persistence, selective edits, failure preservation, and missing-content blocking");
    return 0;
}
