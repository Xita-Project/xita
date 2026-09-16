/* Actual Vita pad polling with controlled controller, touch and guest UI state. */
#define __vita__ 1
#include "../kernel/xk_os_vita.c"
#include <assert.h>

uint8_t *g_xram;
uint32_t *g_xpt;
int xk_file_in_ui_map, g_xv_overlay_on;
static SceCtrlData controller;
static unsigned sampled[2], peeked[2], front_contact;
static unsigned settings_buttons;
static int settings_capture;
static uint32_t remote_buttons;
static unsigned controller_peeks, directory_opens;
void xv_remote_pad(uint32_t *buttons,uint8_t *lx,uint8_t *ly,uint8_t *rx,uint8_t *ry)
{(void)lx;(void)ly;(void)rx;(void)ry;if(remote_buttons)*buttons=remote_buttons;}
SceUInt64 sceKernelGetProcessTimeWide(void) {return 1000000;}
int xv_settings_input(uint32_t buttons,uint64_t now)
{assert(now==1000000);settings_buttons=buttons;return settings_capture;}
void xv_logf(const char *fmt, ...) { (void)fmt; }
static int optimization=-1;
void xv_benchmark_optimizations(int enabled) { optimization=enabled; }
uint32_t xv_guest_r16(uint32_t a) { return X_M16(a); }
unsigned xd3d_pad_frame(void) { return 1; }
int sceCtrlSetSamplingMode(SceCtrlPadInputMode m) { return 0; }
int sceCtrlPeekBufferPositive(int port, SceCtrlData *d, int n) { controller_peeks++;*d=controller;return 1; }
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state) { sampled[port]++;return 0; }
int sceTouchPeek(SceUInt32 port, SceTouchData *d, SceUInt32 n) {
    peeked[port]++;memset(d,0,sizeof *d);
    if (port==SCE_TOUCH_PORT_BACK || front_contact) {
        d->reportNum=1;d->report[0].x=port==SCE_TOUCH_PORT_BACK?1500:100;d->report[0].y=1000;
    }
    return 1;
}
SceUID sceIoOpen(const char *p, int flags, SceMode mode) { return -1; }
int sceIoRead(SceUID fd, void *p, SceSize n) { abort(); }
int sceIoWrite(SceUID fd, const void *p, SceSize n) { abort(); }
int sceIoClose(SceUID fd) { abort(); }
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence) { abort(); }
SceUID sceIoDopen(const char *p) { directory_opens++;return -1; }
int sceIoDread(SceUID fd, SceIoDirent *e) { abort(); }
int sceIoDclose(SceUID fd) { abort(); }

int main(int argc, char **argv)
{
    int rear=argc>1&&!strcmp(argv[1],"rear");
    int shot=argc>1&&!strcmp(argv[1],"shot");
    if(shot)setenv("XV_SHOT_DUMP","1",1);else unsetenv("XV_SHOT_DUMP");
    if(rear)setenv("XV_REAR_TOUCH","1",1);else unsetenv("XV_REAR_TOUCH");
    g_xram=calloc(1,4*1024*1024);g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
    X_M32(0x2F8CA0)=0x5000;
    controller.lx=controller.ly=controller.rx=controller.ry=128;
    controller.buttons=SCE_CTRL_DOWN;
    xk_os_pad p;xk_os_pad_poll(&p);
    assert(p.buttons==0x40 && p.analog[5]==(rear?255:0));
    assert(sampled[0]==1 && sampled[1]==(unsigned)rear && peeked[1]==(unsigned)rear);
    /* Multiplayer menu: simulation remains unpaused, but D-pad navigates UI. */
    X_M32(0x2E4000)=0x6000;xk_os_pad_poll(&p);assert(p.buttons==2);
    X_M32(0x2E4000)=0;X_M8(0x5002)=1;xk_os_pad_poll(&p);assert(p.buttons==2);
    X_M8(0x5002)=0;xk_file_in_ui_map=1;xk_os_pad_poll(&p);assert(p.buttons==2);
    xk_file_in_ui_map=0;xk_os_pad_poll(&p);assert(p.buttons==0x40);
    settings_capture=1;controller.buttons=SCE_CTRL_SELECT|SCE_CTRL_CIRCLE;
    xk_os_pad_poll(&p);assert(settings_buttons==XV_SETTINGS_TOGGLE && !p.buttons && p.connected);
    controller.buttons=SCE_CTRL_RIGHT;xk_os_pad_poll(&p);assert(settings_buttons==XV_DASH_RIGHT && !p.buttons);
    settings_capture=0;controller.buttons=SCE_CTRL_DOWN;
    controller.buttons=0;front_contact=1;xk_os_pad_poll(&p);assert(p.buttons==0x40);
    assert(peeked[1]==(rear?8u:0u)); /* rear-disabled never samples or polls panel */
    front_contact=0;X_M8(0x5000)=X_M8(0x5001)=1;
    controller.buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SELECT;
    xk_os_pad_poll(&p);assert(xv_benchmark_active()&&!p.buttons&&p.connected);
    for(unsigned i=0;i<8;i++)assert(!p.analog[i]);
    float view[6]={0,0,0,0,1,0};
    assert(xv_benchmark_step(1,480,1,view)==544);xv_benchmark_applied(1,544);
    controller.buttons=0;controller.lx=255;xk_os_pad_poll(&p);assert(!p.lx&&!p.buttons);
    controller.buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SELECT;
    xk_os_pad_poll(&p);assert(xv_benchmark_step(2,544,1,view)==480);
    xv_benchmark_applied(2,480);assert(!xv_benchmark_active());
    controller.buttons=0;xk_os_pad_poll(&p);
    controller.buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SQUARE;
    xk_os_pad_poll(&p);assert(xv_benchmark_active()&&!p.buttons&&!p.force_start);
    assert(xv_benchmark_step(3,480,1,view)==480&&optimization==0);xv_benchmark_applied(3,480);
    xk_os_pad_poll(&p);assert(!xv_benchmark_step(4,480,1,view)); /* Held chord does not cancel. */
    controller.buttons=0;xk_os_pad_poll(&p);
    controller.buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SQUARE;
    xk_os_pad_poll(&p);assert(xv_benchmark_step(5,480,1,view)==480&&optimization==-1);
    xv_benchmark_applied(5,480);assert(!xv_benchmark_active());
    controller.buttons=0;xk_os_pad_poll(&p);
    controller.buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_TRIANGLE;
    xk_os_pad_poll(&p);assert(p.force_start&&!xv_benchmark_active());
    controller.buttons=0;controller.lx=128;xk_os_pad_poll(&p);
    remote_buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SQUARE;
    xk_os_pad_poll(&p);assert(xv_benchmark_active()&&!p.buttons&&!p.force_start);
    assert(xv_benchmark_step(6,480,1,view)==480);xv_benchmark_applied(6,480);
    remote_buttons=0;xk_os_pad_poll(&p);
    remote_buttons=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER|SCE_CTRL_SQUARE;
    xk_os_pad_poll(&p);assert(xv_benchmark_step(7,480,1,view)==480);
    xv_benchmark_applied(7,480);assert(!xv_benchmark_active()&&optimization==-1);
    if(shot) {
        assert(xv_diag_poll_available(XV_DIAG_SHOT));
        remote_buttons=0;controller.buttons=SCE_CTRL_CROSS;
        unsigned peeks=controller_peeks,dirs=directory_opens;
        xv_diag_poll_override(XV_DIAG_SHOT,1);
        for(unsigned i=0;i<180;i++) {xk_os_pad_poll(&p);assert(p.analog[0]==255&&p.connected);}
        assert(controller_peeks==peeks+180&&directory_opens==dirs);
        xv_diag_poll_override(XV_DIAG_SHOT,-1);
        for(unsigned i=0;i<180;i++) {xk_os_pad_poll(&p);assert(p.analog[0]==255);}
        assert(controller_peeks==peeks+360&&directory_opens==dirs+1);
    }
    free(g_xpt);free(g_xram);
    puts("PASS: actual Vita input, unpaused multiplayer menus, paused campaign, main menu, gameplay shortcuts and independent rear/front touch");
    return 0;
}
