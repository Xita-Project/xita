/* Vita libc forwards these operations to firmware. The instruction runner
 * models their bytes separately; it cannot measure the firmware implementation.
 * Keep this translation unit opaque to all callers. */
#include <stddef.h>
#include <psp2/kernel/threadmgr.h>
void *sceClibMemcpy(void *dest,const void *source,size_t size)
{(void)source;(void)size;return dest;}
void *sceClibMemset(void *dest,int value,size_t size)
{(void)value;(void)size;return dest;}
void *sceClibMemmove(void *dest,const void *source,size_t size)
{(void)source;(void)size;return dest;}
int sceClibPrintf(const char *fmt,...){(void)fmt;__builtin_trap();}
/* Uncontended calls only. Separate compilation keeps ownership/guard checks
 * from folding away because the compiler knows a fixture's return value. */
int sceKernelGetThreadId(void){return 17;}
int sceKernelTryLockLwMutex(SceKernelLwMutexWork *p,int n){(void)p;(void)n;return 0;}
int sceKernelTryLockMutex(SceUID p,int n){(void)p;(void)n;return 0;}
int sceKernelUnlockLwMutex(SceKernelLwMutexWork *p,int n){(void)p;(void)n;return 0;}
int sceKernelUnlockMutex(SceUID p,int n){(void)p;(void)n;return 0;}
int sceKernelLockLwMutex(SceKernelLwMutexWork*p,int n,SceUInt*t){(void)p;(void)n;(void)t;__builtin_trap();}
int sceKernelLockMutex(SceUID p,int n,SceUInt*t){(void)p;(void)n;(void)t;__builtin_trap();}
int sceKernelWaitSema(SceUID p,int n,SceUInt*t){(void)p;(void)n;(void)t;__builtin_trap();}
int sceKernelSignalSema(SceUID p,int n){(void)p;(void)n;__builtin_trap();}
int sceKernelDelayThread(SceUInt t){(void)t;__builtin_trap();}
