/* xita_clock.skprx - kernel module: sets the ARM clock through the kernel-side setter, which accepts
 * 500 MHz (the user-side scePowerSetArmClockFrequency caps at 444). module_start applies the clock
 * given in its argument block (default 500); two syscalls re-apply / read it. Loaded by Xita at game
 * start with taiLoadStartKernelModuleForUser when HENkaku "Unsafe Homebrew" is enabled. No hooks. */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/power.h>
int xita_clock_set_arm(int mhz)
{
    if (mhz < 41 || mhz > 500) return -1;
    return kscePowerSetArmClockFrequency(mhz);
}
int xita_clock_get_arm(void) { return kscePowerGetArmClockFrequency(); }
void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp)
{
    int mhz = 500;
    if (args >= sizeof(int) && argp) ksceKernelCopyFromUser(&mhz, argp, sizeof mhz);
    if (mhz >= 41 && mhz <= 500) kscePowerSetArmClockFrequency(mhz);
    return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) { (void)args; (void)argp; return SCE_KERNEL_STOP_SUCCESS; }
