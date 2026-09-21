/* xita_clock.skprx - kernel module that holds the ARM clock at a target (500 MHz) while the Xita process
 * is alive. The user-side scePowerSetArmClockFrequency caps at 444; the kernel-side setter accepts 500.
 * module_start reads {mhz, pid} from its argument block, applies the clock and starts a kernel thread
 * that re-applies it every 500 ms (the game and the OS lower it at times) and drops back to 444 when
 * that process is gone. The module stays resident after the app exits, so a later launch re-arms it
 * through the xita_clock_bind syscall. Loaded with taiLoadStartKernelModuleForUser: needs HENkaku
 * "Unsafe Homebrew" and an unsafe SELF. No hooks or patches. */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/processmgr.h>
#include <psp2kern/power.h>

static SceUID target_pid = -1, enforcer = -1;
static volatile int target_mhz = 500, armed;

static int enforce(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        ksceKernelDelayThread(500 * 1000);
        if (!armed) continue;
        int status = 0;
        if (target_pid < 0 || ksceKernelGetProcessStatus(target_pid, &status) < 0) {   /* process gone */
            armed = 0; kscePowerSetArmClockFrequency(444); continue;
        }
        if (kscePowerGetArmClockFrequency() != target_mhz) kscePowerSetArmClockFrequency(target_mhz);
    }
    return 0;
}
/* syscall: bind the calling process and apply the clock now */
int xita_clock_bind(int mhz)
{
    if (mhz < 41 || mhz > 500) return -1;
    target_mhz = mhz; target_pid = ksceKernelGetProcessId(); armed = 1;
    return kscePowerSetArmClockFrequency(mhz);
}
int xita_clock_get_arm(void) { return kscePowerGetArmClockFrequency(); }
void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp)
{
    int a[2] = { 500, -1 };
    if (args >= sizeof a && argp) ksceKernelCopyFromUser(a, argp, sizeof a);
    if (a[0] >= 41 && a[0] <= 500) target_mhz = a[0];
    target_pid = a[1]; armed = target_pid >= 0;
    kscePowerSetArmClockFrequency(target_mhz);
    enforcer = ksceKernelCreateThread("xita_clock", enforce, 0x10000100, 0x1000, 0, 0, NULL);
    if (enforcer >= 0) ksceKernelStartThread(enforcer, 0, NULL);
    return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize args, void *argp) { (void)args; (void)argp; armed = 0; kscePowerSetArmClockFrequency(444); return SCE_KERNEL_STOP_SUCCESS; }
