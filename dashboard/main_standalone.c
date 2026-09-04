#include "xv_dash.h"
#include <stdio.h>
#include <string.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

typedef struct {
    SceUID blocks[2];
    uint32_t *pixels[2];
    int drawing;
    SceTouchPanelInfo panels[2];
} platform;

static int present(void *userdata, xv_dash_framebuffer *fb)
{
    platform *p = userdata;
    SceDisplayFrameBuf frame = {0};
    frame.size = sizeof(frame);
    frame.base = fb->pixels;
    frame.pitch = fb->pitch;
    frame.width = fb->width;
    frame.height = fb->height;
    frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    int rc = sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (rc < 0) return rc;
    rc = sceDisplayWaitVblankStart();
    if (rc < 0) return rc;
    p->drawing ^= 1;
    fb->pixels = p->pixels[p->drawing];
    return 0;
}
static int poll_input(void *userdata, xv_dash_input *in)
{
    platform *p = userdata;
    SceCtrlData pad = {0};
    int rc = sceCtrlPeekBufferPositive(0,&pad,1);
    if (rc < 0) return rc;
    if (!rc) return 0;
    const unsigned vita[] = {SCE_CTRL_UP,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_CROSS,SCE_CTRL_CIRCLE};
    for (unsigned i = 0; i < sizeof(vita)/sizeof(vita[0]); i++)
        if (pad.buttons & vita[i]) in->buttons |= 1u << i;
    in->lx = (int)pad.lx-128; in->ly = (int)pad.ly-128;
    in->rx = (int)pad.rx-128; in->ry = (int)pad.ry-128;
    for (int port = 0; port < 2; port++) {
        SceTouchData touch = {0};
        rc = sceTouchPeek(port,&touch,1);
        if (rc < 0) return rc;
        unsigned count = touch.reportNum > 8 ? 8 : touch.reportNum;
        if (port) in->rear_count = count; else in->front_count = count;
        xv_dash_touch *points = port ? in->rear : in->front;
        const SceTouchPanelInfo *panel = &p->panels[port];
        for (unsigned i = 0; i < count; i++) {
            points[i].x = ((int)touch.report[i].x-panel->minAaX)*959/(panel->maxAaX-panel->minAaX);
            points[i].y = ((int)touch.report[i].y-panel->minAaY)*543/(panel->maxAaY-panel->minAaY);
        }
    }
    return 0;
}
static int handoff(const xv_dash_result *result)
{
    static const char *const modes[] = {"campaign","multiplayer","settings"};
    const char *tmp = "ux0:data/xita/launch.cfg.tmp";
    FILE *f = fopen(tmp,"wb");
    if (!f) return -1;
    /* IS_SAVE extends the three required keys to disambiguate save filenames. */
    int rc = fprintf(f,"GAME=%s\nMODE=%s\nMAP=%s\nIS_SAVE=%d\n",
                     result->game_id,modes[result->mode],result->map,result->is_save) < 0 ? -1 : 0;
    if (fclose(f)) rc = -1;
    if (!rc && rename(tmp,"ux0:data/xita/launch.cfg")) rc = -1;
    if (rc) { remove(tmp); return rc; }
    return sceAppMgrLaunchAppByUri(0x20000,"psgm:play?titleid=XITA00001");
}
int main(void)
{
    platform p = {.blocks = {-1,-1}};
    int rc = -1;
    /* CDRAM allocation bases and lengths are aligned to 256 KiB. */
    const unsigned bytes = (960u*544u*4u + 0x3ffffu) & ~0x3ffffu;
    for (int i = 0; i < 2; i++) {
        p.blocks[i] = sceKernelAllocMemBlock("xita_dash_fb",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,bytes,NULL);
        if (p.blocks[i] < 0) goto done;
        if (sceKernelGetMemBlockBase(p.blocks[i],(void **)&p.pixels[i]) < 0) goto done;
    }
    if (sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE) < 0) goto done;
    for (int port = 0; port < 2; port++) {
        if (sceTouchGetPanelInfo(port,&p.panels[port]) < 0 ||
            p.panels[port].maxAaX <= p.panels[port].minAaX ||
            p.panels[port].maxAaY <= p.panels[port].minAaY ||
            sceTouchSetSamplingState(port,SCE_TOUCH_SAMPLING_STATE_START) < 0) goto done;
    }
    sceIoMkdir("ux0:data",0777);
    sceIoMkdir("ux0:data/xita",0777);
    xv_dash_config cfg = {{p.pixels[0],960,544,960},NULL,&p,poll_input,present};
    xv_dash_result result;
    rc = xv_dash_run(&cfg,&result);
    if (!rc) rc = handoff(&result);
    if (rc < 0) {
        sceClibPrintf("Xita Dashboard failed: 0x%08x\n",(unsigned)rc);
        /* Visible failure indication; details persist for diagnosis. */
        FILE *f = fopen("ux0:data/xita/dashboard.log","w");
        if (f) { fprintf(f,"Dashboard / hand-off failed: 0x%08x\n",(unsigned)rc); fclose(f); }
        for (int i = 0; i < 960*544; i++) p.pixels[p.drawing][i] = 0xff102080u;
        cfg.framebuffer.pixels = p.pixels[p.drawing];
        present(&p,&cfg.framebuffer);
        sceKernelDelayThread(3000000);
    }
done:
    /* Stop scanout before returning any displayed allocation to CDRAM. */
    sceDisplaySetFrameBuf(NULL,SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    for (int port = 0; port < 2; port++) sceTouchSetSamplingState(port,SCE_TOUCH_SAMPLING_STATE_STOP);
    for (int i = 0; i < 2; i++) if (p.blocks[i] >= 0) sceKernelFreeMemBlock(p.blocks[i]);
    sceKernelExitProcess(rc < 0 ? 1 : 0);
    return 0;
}
