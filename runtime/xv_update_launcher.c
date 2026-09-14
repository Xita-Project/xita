/* Stable entry point packaged as eboot.bin. Updates only replace game-a/b.self.
 * A failed candidate remains ATTEMPTED, so the next bubble launch falls back. */
#include "xv_update.h"
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <stdio.h>
unsigned int _newlib_heap_size_user=8*1024*1024;
int main(void)
{
    scePowerSetArmClockFrequency(444);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    int slot=xv_update_boot();
    FILE *log=fopen("ux0:data/xita/update/launcher.log","a");
    if(log) {fprintf(log,"launcher selected slot %d\n",slot);fclose(log);}
    if(slot>=0) {
        char argument[]="--xita-slot=0";argument[12]=(char)('0'+slot);
        char *args[]={argument,NULL};
        int rc=sceAppMgrLoadExec(slot?"app0:game-b.self":"app0:game-a.self",args,NULL);
        if(rc>=0)for(;;)sceKernelDelayThread(100000);
        log=fopen("ux0:data/xita/update/launcher.log","a");
        if(log) {fprintf(log,"LoadExec slot %d failed %08X\n",slot,rc);fclose(log);}
        /* A synchronous loader failure gets one immediate fallback attempt. */
        int fallback=xv_update_boot();
        if(fallback>=0&&fallback!=slot) {
            argument[12]=(char)('0'+fallback);
            sceAppMgrLoadExec(fallback?"app0:game-b.self":"app0:game-a.self",args,NULL);
        }
    }
    sceKernelExitProcess(1);return 1;
}
