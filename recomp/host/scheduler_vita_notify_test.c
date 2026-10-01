#define __vita__ 1
#include "../kernel/xk_os_vita.c"
#include <assert.h>
static unsigned bits,last_delay,waits;
static int fail_create,fail_wait,expired;
void xv_logf(const char *fmt,...) { (void)fmt; }
SceUID sceKernelCreateEventFlag(const char *name,int attr,int initial,SceKernelEventFlagOptParam *opt)
{ assert(!attr&&!initial&&!opt);return fail_create?-1:7; }
int sceKernelSetEventFlag(SceUID id,unsigned value) { assert(id==7);bits|=value;return 0; }
int sceKernelWaitEventFlag(SceUID id,unsigned wanted,unsigned mode,unsigned *out,SceUInt *timeout)
{
    assert(id==7&&wanted==1&&mode==(SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT));waits++;
    if(expired){*timeout=0;return -1;}if(fail_wait)return -1;
    assert(bits);*out=bits;bits&=~wanted;return 0;
}
int sceKernelDelayThread(SceUInt us) { last_delay=us;return 0; }
int main(void)
{
    xk_os_scheduler_notify();assert(!bits);xk_os_scheduler_wait(99);assert(last_delay==99);
    fail_create=1;assert(!xk_os_scheduler_prepare());xk_os_scheduler_wait(100);assert(last_delay==100);
    g_scheduler_event=-2;fail_create=0;assert(xk_os_scheduler_prepare());
    xk_os_scheduler_notify();xk_os_scheduler_wait(100000);assert(!bits&&waits==1);
    fail_wait=1;xk_os_scheduler_wait(100000);assert(last_delay==1000);
    last_delay=0;expired=1;xk_os_scheduler_wait(100000);assert(!last_delay);
    puts("PASS: Vita scheduler sticky event, creation/API failure and bounded timeout fallback");
}
