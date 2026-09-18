/* Stable entry point packaged as eboot.bin. Updates only replace game-a/b.self.
 * A failed candidate remains ATTEMPTED, so the next bubble launch falls back. */
#include "xv_update_halo2.h"
#include "xv_launch_args.h"
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <stdio.h>
unsigned int _newlib_heap_size_user=8*1024*1024;
int main(int argc,char **argv)
{
    scePowerSetArmClockFrequency(444);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    int halo2=xv_launch_has(argc,argv,"--xita-game=halo2");
    int (*boot)(void)=halo2?xv_halo2_update_boot:xv_update_boot;
    const char *paths[2]={halo2?"app0:halo2-a.self":"app0:game-a.self",
                          halo2?"app0:halo2-b.self":"app0:game-b.self"};
    int slot=boot();
    FILE *log=fopen("ux0:data/xita/update/launcher.log","a");
    if(log) {fprintf(log,"launcher selected %s slot %d\n",halo2?"halo2":"haloce",slot);fclose(log);}
    if(slot>=0) {
        char argument[]="--xita-slot=0";argument[12]=(char)('0'+slot);
        char *args[]={argument,xv_launch_has(argc,argv,"--xita-dashboard")?"--xita-dashboard":NULL,NULL};
        int rc=sceAppMgrLoadExec(paths[slot],args,NULL);
        if(rc>=0)for(;;)sceKernelDelayThread(100000);
        log=fopen("ux0:data/xita/update/launcher.log","a");
        if(log) {fprintf(log,"LoadExec slot %d failed %08X\n",slot,rc);fclose(log);}
        /* A synchronous loader failure gets one immediate fallback attempt. */
        int fallback=boot();
        if(fallback>=0&&fallback!=slot) {
            argument[12]=(char)('0'+fallback);
            rc=sceAppMgrLoadExec(paths[fallback],args,NULL);
            if(rc>=0)for(;;)sceKernelDelayThread(100000);
        }
    }
    sceKernelExitProcess(1);return 1;
}
