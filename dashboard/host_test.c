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
    xv_dash_config cfg = {{pixels,960,544,976},root,&h,poll_input,present};
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
    return 0;
}
