#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int SceUID;
typedef int SceKernelMemBlockType;
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE 1
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 2
#define SCE_GXM_MEMORY_ATTRIB_READ 4
#define SCE_OK 0
#define UI_LOG(...) ((void)0)
#define ALIGN_UP(n,a) (((n)+(a)-1)&~((a)-1))
static int calls, maps, frees, types[4], fail_alloc, fail_base, fail_map;
static uint32_t sizes[4];
static int sceKernelAllocMemBlock(const char *name, int type, uint32_t size, void *opt) {
    assert(name && !opt && calls<4); types[calls]=type; sizes[calls++]=size;
    return (fail_alloc & (1<<type)) ? -5 : type;
}
static int sceKernelGetMemBlockBase(int uid, void **base) {
    if (fail_base & (1<<uid)) return -6;
    *base=(void *)(uintptr_t)(uid*0x100000); return 0;
}
static int sceGxmMapMemory(void *base, uint32_t size, int attr) {
    assert(size && attr==SCE_GXM_MEMORY_ATTRIB_READ); ++maps;
    return (fail_map & (1<<((uintptr_t)base/0x100000))) ? -7 : 0;
}
static int sceKernelFreeMemBlock(int uid) { assert(uid>0);++frees;return 0; }
#include "allocator.inc"
static void reset(void) { calls=maps=frees=fail_alloc=fail_base=fail_map=0; }
int main(void) {
    SceUID uid;
    unsetenv("XV_TEXTURE_CDRAM"); reset();
    assert(ui_texture_alloc(32768,&uid));assert(types[0]==(XV_TEXTURE_CDRAM_DEFAULT?2:1));
    setenv("XV_TEXTURE_CDRAM","0",1);reset();
    assert(ui_texture_alloc(32768,&uid));assert(calls==1 && types[0]==1);
    setenv("XV_TEXTURE_CDRAM","1",1);reset();
    assert(ui_texture_alloc(32768,&uid));assert(types[0]==2 && sizes[0]==262144 && maps==1);
    reset();assert(ui_gpu_alloc(1,&uid));assert(types[0]==1 && sizes[0]==4096);
    reset();fail_alloc=1<<2;
    assert(ui_texture_alloc(32*1024*1024,&uid));assert(calls==2 && types[1]==1 && frees==0 && uid==1);
    reset();fail_base=1<<2;
    assert(ui_texture_alloc(32768,&uid));assert(calls==2 && maps==1 && frees==1 && uid==1);
    reset();fail_map=1<<2;
    assert(ui_texture_alloc(32768,&uid));assert(calls==2 && maps==2 && frees==1 && uid==1);
    reset();fail_alloc=(1<<1)|(1<<2);
    assert(!ui_texture_alloc(32768,&uid));assert(calls==2 && maps==0 && frees==0 && uid<0);
    reset();fail_map=(1<<1)|(1<<2);
    assert(!ui_texture_alloc(32768,&uid));assert(calls==2 && maps==2 && frees==2 && uid==-1);
    puts("PASS texture pool default/overrides/alignment/allocation/base/map failures");
}
