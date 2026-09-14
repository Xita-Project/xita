#define XV_GUEST_AFFINITY 1
#include "runtime/xv_cpu.c"
#include <assert.h>
static SceUID current=7;
static int masks[10],get_error,set_error,ignore_set,sets;
SceUID sceKernelGetThreadId(void) { return current; }
int sceKernelGetThreadCpuAffinityMask(SceUID id) { assert(id>0&&id<10);return get_error?-1:masks[id]; }
int sceKernelChangeThreadCpuAffinityMask(SceUID id,int mask)
{ assert(id>0&&id<10);sets++;if(set_error)return -2;if(!ignore_set)masks[id]=mask;return 0; }
void xv_logf(const char *fmt,...) { (void)fmt; }
int main(void)
{
    const int original[]={0,SCE_KERNEL_CPU_MASK_USER_0,SCE_KERNEL_CPU_MASK_USER_1,SCE_KERNEL_CPU_MASK_USER_2,SCE_KERNEL_CPU_MASK_USER_ALL};
    for(unsigned i=0;i<sizeof original/sizeof original[0];i++) {
        masks[7]=original[i];masks[8]=SCE_KERNEL_CPU_MASK_USER_1;
        xv_guest_affinity_override(0);assert(xv_guest_affinity_valid()&&masks[7]==original[i]);
        xv_guest_affinity_override(1);assert(xv_guest_affinity_valid()&&masks[7]==SCE_KERNEL_CPU_MASK_USER_2);
        xv_guest_affinity_override(0);assert(xv_guest_affinity_valid()&&masks[7]==original[i]);
        xv_guest_affinity_override(1);xv_guest_affinity_override(-1);
        assert(masks[7]==original[i]&&affinity_thread<0&&masks[8]==SCE_KERNEL_CPU_MASK_USER_1);
    }
    masks[7]=0;xv_guest_affinity_override(0);xv_guest_affinity_override(1);
    current=8;xv_guest_affinity_override(0);assert(!xv_guest_affinity_valid());
    xv_guest_affinity_override(-1);assert(masks[7]==0&&affinity_thread<0);current=7;
    get_error=1;int before=sets;xv_guest_affinity_override(0);assert(!xv_guest_affinity_valid()&&sets==before&&affinity_thread<0);get_error=0;
    xv_guest_affinity_override(0);set_error=1;xv_guest_affinity_override(1);assert(!xv_guest_affinity_valid());
    xv_guest_affinity_override(-1);assert(affinity_thread==7);set_error=0;
    xv_guest_affinity_override(-1);assert(masks[7]==0&&affinity_thread<0);
    xv_guest_affinity_override(0);ignore_set=1;xv_guest_affinity_override(1);assert(!xv_guest_affinity_valid());ignore_set=0;xv_guest_affinity_override(-1);
    xv_guest_affinity_override(0);xv_guest_affinity_override(1);ignore_set=1;xv_guest_affinity_override(-1);assert(!xv_guest_affinity_valid()&&affinity_thread==7);ignore_set=0;xv_guest_affinity_override(-1);assert(masks[7]==0&&affinity_thread<0);
    before=sets;xv_guest_affinity_override(1);assert(!xv_guest_affinity_valid()&&sets==before&&affinity_thread<0);
    puts("PASS: affinity exact masks/restoration, presenting thread ownership, untouched workers, read/set failures, ignored setter, cancellation and restoration retry");
}
