/* Synthetic values only; native SSE is the independent result/status oracle. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <xmmintrin.h>
void minmax_oracle(uint32_t out[4], const uint32_t left[4], const uint32_t right[4],
                   unsigned maximum, unsigned *csr)
{
    unsigned saved = _mm_getcsr(); _mm_setcsr(*csr);
    __m128 a, b; memcpy(&a, left, 16); memcpy(&b, right, 16);
    if (maximum) __asm__ volatile("maxps %1,%0" : "+x"(a) : "x"(b) : "memory");
    else __asm__ volatile("minps %1,%0" : "+x"(a) : "x"(b) : "memory");
    *csr = _mm_getcsr(); memcpy(out, &a, 16); _mm_setcsr(saved);
}
#ifndef XITA_MINMAX_ORACLE_ONLY
#include "code_000.c"
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
static uint8_t arena[65536]; static uint32_t pages[16];
void x_guest_read_pages(void *dst, uint32_t a, size_t n) {
    for (size_t i=0;i<n;++i) ((uint8_t *)dst)[i]=g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)];
}
void x_guest_write_pages(uint32_t a, const void *src, size_t n) {
    for (size_t i=0;i<n;++i) g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)]=((const uint8_t *)src)[i];
}
void xv_unimpl(xctx *c, uint32_t ip, const char *what) {(void)c;(void)ip;(void)what;assert(0);}
typedef struct { void (*run)(xctx *); unsigned dst,src,maximum; } test_case;
static const test_case cases[] = {
#include "cases.h"
};
static const uint32_t patterns[] = {
    0,0x80000000,0x7f800000,0xff800000,0x7f800001,0xff800001,0x7fc12345,0xffc54321,
    1,0x007fffff,0x80000001,0x807fffff,0x00800000,0x7f7fffff,0x3f800000,0xbf800000,
    0x40800000,0xc0800000,0x01000001,0x81000001,0x7fffffff,0xffffffff,0x3f800001,0xbf800001,
    0x00800001,0x80800001,0x7fc00000,0xffc00000,0x00400000,0x80400000,0x7f800002,0xff800002
};
static unsigned random_state=0x71ac582d;
static unsigned random_bits(void) {random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void check_integer_selection(void)
{
    unsigned count=0;
    for(unsigned n=0;n<101024;++n) for(unsigned maximum=0;maximum<2;++maximum) {
        uint32_t a[4],b[4],got[4],want[4],flags=0; unsigned csr=0x1f80;
        for(unsigned lane=0;lane<4;++lane) {
            a[lane]=n<1024?patterns[((n/32)+lane)%32]:random_bits();
            b[lane]=n<1024?patterns[((n%32)+lane)%32]:random_bits();
            got[lane]=x_minmax_bits(a[lane],b[lane],maximum,&flags);
        }
        minmax_oracle(want,a,b,maximum,&csr);
        assert(memcmp(got,want,16)==0);
        assert(flags==((csr&1)|((csr&2)<<6))); ++count;
    }
    printf("%u integer lane-selection/native exception comparisons\n",count);
}
static void check_generated(void)
{
    unsigned count=0,saved=_mm_getcsr();
    for(unsigned n=0;n<144;++n) for(unsigned seed=0;seed<32;++seed) for(unsigned control=0;control<16;++control) {
        const test_case *t=&cases[n];xctx c,want;memset(&c,0xa5,sizeof c);
        c.r[4]=0x8000;c.r[0]=seed&1?0x2ffc:0x2011;
        uint32_t regs[8][4];
        for(unsigned r=0;r<8;++r) for(unsigned l=0;l<4;++l) regs[r][l]=patterns[(r*5+l+seed)%32];
        memcpy(c.xmm,regs,sizeof regs);
        uint32_t input[4];memcpy(input,regs[t->src%8],16);
        if(t->src==8)x_guest_write(c.r[0],input,16);
        uint8_t before[sizeof arena];memcpy(before,arena,sizeof arena);
        memcpy(&want,&c,sizeof c);want.r[4]+=4;
        unsigned csr=0x1f80|((control&3)<<13)|((control&4)<<4)|((control&8)<<12)|(seed&0x3f);
        unsigned expected_csr=csr;uint32_t expected[4];
        minmax_oracle(expected,regs[t->dst],input,t->maximum,&expected_csr);
        memcpy(want.xmm[t->dst],expected,16);
        _mm_setcsr(csr);t->run(&c);unsigned after=_mm_getcsr();
        assert(after==expected_csr);assert(memcmp(&c,&want,sizeof c)==0);
        assert(memcmp(arena,before,sizeof arena)==0);++count;
    }
    _mm_setcsr(saved);printf("%u emitted register/alias/memory/full-context/native-FP comparisons\n",count);
}
int main(void)
{
    g_xram=arena;g_img_base=arena;g_xpt=pages;
    for(unsigned i=0;i<16;++i)pages[i]=(i^1)*4096;
    memset(arena,0xdb,sizeof arena);check_integer_selection();check_generated();return 0;
}
#endif
