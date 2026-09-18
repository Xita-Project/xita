#include "fp_environment.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static uint32_t native_state, writes;
static xctx cpu;
static jmp_buf fault;
uint32_t xk_mem_arena_size(void) { return 0x20000; }
uint32_t h2_platform_fpscr_read(void) { return native_state; }
void h2_platform_fpscr_write(uint32_t value) { native_state=value; ++writes; }
void xv_logf(const char *format, ...) { (void)format; native_state=0xDEADBEEF; }
void h2_fp_environment_fault(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{ (void)c; (void)ip; (void)address; (void)value; longjmp(fault,1); }
static void reject(void (*operation)(xctx *,uint32_t,uint32_t),uint32_t address)
{
    xctx before=cpu; uint32_t fp=native_state, n=writes;
    uint8_t *memory=malloc(0x20000); assert(memory); memcpy(memory,g_xram,0x20000);
    if (!setjmp(fault)) { operation(&cpu,0x4000,address); abort(); }
    assert(native_state==fp && writes==n && !memcmp(&before,&cpu,sizeof cpu));
    assert(!memcmp(memory,g_xram,0x20000)); free(memory);
}
int main(void)
{
    g_xram=malloc(0x20000); g_img_base=g_xram; g_xpt=malloc((1u<<20)*4); assert(g_xram&&g_xpt);
    memset(g_xram,0xA5,0x20000); memset(&cpu,0xA6,sizeof cpu); xctx before=cpu;
    for(unsigned i=0;i<1u<<20;++i) g_xpt[i]=0x1F000;
    g_xpt[1]=0x5000; g_xpt[2]=0x8000; /* deliberately nonadjacent physical pages */
    static const unsigned bit[]={0,7,1,2,3,4};
    for(unsigned status=0;status<64;++status) {
        uint32_t expected=0xFC000000u;
        for(unsigned i=0;i<6;++i) if(status&(1u<<i)) expected|=1u<<bit[i];
        for(unsigned offset=0;offset<8;++offset) {
            uint32_t address=0x1FFCu+offset, value;
            native_state=expected; h2_stmxcsr(&cpu,0x4000,address);
            x_guest_read(&value,address,4); assert(value==(0x1F80u|status));
            assert(native_state==expected && !memcmp(&before,&cpu,sizeof cpu));
            value=0x1F80u|(status^63); x_guest_write(address,&value,4);
            h2_ldmxcsr(&cpu,0x4004,address);
            assert(native_state==(expected^0x9Fu) && !memcmp(&before,&cpu,sizeof cpu));
            x_guest_read(&expected,address,4); assert(expected==value); /* input unchanged */
            expected=native_state^0x9Fu;
        }
    }
    native_state=0x60000011; h2_stmxcsr(&cpu,0x4000,0x1000);
    assert(X_M32(0x1000)==0x1FA1 && native_state==0x60000011);
    X_M32(0x1000)=0x1F80; h2_ldmxcsr(&cpu,0x4004,0x1000); assert(native_state==0x60000000);
    for(unsigned i=0;i<32;++i) {
        if(i<6) continue;
        X_M32(0x1000)=0x1F80u^(1u<<i); reject(h2_ldmxcsr,0x1000);
    }
    for(unsigned i=0;i<32;++i) if(0x03F79F00u&(1u<<i)) {
        native_state=1u<<i; X_M32(0x1000)=0x1F80;
        reject(h2_stmxcsr,0x1000); reject(h2_ldmxcsr,0x1000);
    }
    native_state=0;
    const uint32_t invalid[]={0,0x2FFF,0xD0000000,0xFFFFFFFE};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        reject(h2_stmxcsr,invalid[i]); reject(h2_ldmxcsr,invalid[i]);
    }
    free(g_xpt); free(g_xram);
    puts("H2 native FP environment: status mapping, mask/control rejection, cross-page spans, diagnostic isolation and CPU preservation passed.");
    return 0;
}
