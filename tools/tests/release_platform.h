#pragma once
/* Deterministic host scheduler/network lifecycle shim; never in a Vita build. */
typedef int SceUID;
typedef struct {void *memory;int size,flags;} SceNetInitParam;
#define SCE_SYSMODULE_NET 1
#define SCE_KERNEL_CPU_MASK_USER_ALL 0
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
int sceSysmoduleLoadModule(int);
int sceSysmoduleUnloadModule(int);
int sceNetInit(SceNetInitParam *);
int sceNetTerm(void);
int sceNetCtlInit(void);
int sceNetCtlTerm(void);
int sceKernelPowerTick(int);
int sceKernelCreateThread(const char *,int (*)(unsigned,void *),int,unsigned,int,int,void *);
int sceKernelStartThread(int,unsigned,void *);
int sceKernelWaitThreadEnd(int,void *,void *);
int sceKernelDeleteThread(int);
