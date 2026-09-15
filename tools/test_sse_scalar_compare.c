/* Synthetic values only; independent native SSE instructions are the oracle. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>
void compare_oracle(uint32_t out[4], const uint32_t left[4], const uint32_t right[4],
                    unsigned predicate, unsigned *csr)
{
    unsigned saved = _mm_getcsr(); _mm_setcsr(*csr);
    __m128 a, b; memcpy(&a, left, 16); memcpy(&b, right, 16);
#define CMP(N) case N: __asm__ volatile("cmpss $" #N ",%1,%0" : "+x"(a) : "x"(b) : "memory"); break
    switch (predicate) { CMP(0); CMP(1); CMP(2); CMP(3); CMP(4); CMP(5); CMP(6); CMP(7); default: assert(0); }
#undef CMP
    *csr = _mm_getcsr(); memcpy(out, &a, 16); _mm_setcsr(saved);
}
uint32_t mask_oracle(const uint32_t source[4], unsigned *csr)
{
    unsigned saved = _mm_getcsr(); _mm_setcsr(*csr);
    __m128 a; uint32_t result; memcpy(&a, source, 16);
    __asm__ volatile("movmskps %1,%0" : "=r"(result) : "x"(a) : "memory");
    *csr = _mm_getcsr(); _mm_setcsr(saved); return result;
}
#ifndef XITA_COMPARE_ORACLE_ONLY
#include "code_000.c"
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
static uint8_t arena[65536]; static uint32_t pages[16], fault_ip;
void x_guest_read_pages(void *dst, uint32_t a, size_t n) {
    assert(n == 4); /* Scalar reads may cross a page, but must never read upper lanes. */
    for (size_t i=0;i<n;++i) ((uint8_t *)dst)[i]=g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)];
}
void x_guest_write_pages(uint32_t a, const void *src, size_t n) {
    for (size_t i=0;i<n;++i) g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)]=((const uint8_t *)src)[i];
}
void xv_unimpl(xctx *c, uint32_t ip, const char *what) {(void)c;(void)what;fault_ip=ip;}
typedef struct { void (*run)(xctx *); unsigned dst,src,predicate; } test_case;
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
static void check_integer_comparison(void)
{
    unsigned count=0;
    for(unsigned n=0;n<101024;++n) for(unsigned predicate=0;predicate<8;++predicate) {
        uint32_t a[4],b[4],want[4],flags=0; unsigned csr=0x1f80;
        for(unsigned lane=0;lane<4;++lane) {
            a[lane]=n<1024?patterns[((n/32)+lane)%32]:random_bits();
            b[lane]=n<1024?patterns[((n%32)+lane)%32]:random_bits();
        }
        uint32_t got=x_cmpss_bits(a[0],b[0],predicate,&flags);
        compare_oracle(want,a,b,predicate,&csr);
        assert(got==want[0]); assert(memcmp(want+1,a+1,12)==0);
        assert(flags==((csr&1)|((csr&2)<<6))); ++count;
    }
    printf("%u integer scalar/native exception comparisons\n",count);
}
static void check_generated(void)
{
    unsigned count=0,saved=_mm_getcsr();
    for(unsigned n=0;n<644;++n) for(unsigned seed=0;seed<32;++seed) for(unsigned control=0;control<16;++control) {
        const test_case *t=&cases[n];xctx c,want;memset(&c,0xa5,sizeof c);
        c.r[4]=0x8000;c.r[0]=(uint32_t[]){0x2011,0x2ffc,0x2ffd}[seed%3];
        uint32_t regs[8][4];
        for(unsigned r=0;r<8;++r) for(unsigned l=0;l<4;++l) regs[r][l]=patterns[(r*5+l+seed)%32];
        if(t->predicate==8) for(unsigned r=0;r<8;++r) for(unsigned l=0;l<4;++l)
            regs[r][l]=(patterns[(r*5+l+seed)%32]&0x7fffffff)|((((seed^r)>>l)&1u)<<31);
        memcpy(c.xmm,regs,sizeof regs);
        uint32_t input[4];memcpy(input,regs[t->src%8],16);
        if(t->src==8)x_guest_write(c.r[0],input,4);
        uint8_t before[sizeof arena];memcpy(before,arena,sizeof arena);
        memcpy(&want,&c,sizeof c);
        unsigned csr=0x1f80|((control&3)<<13)|((control&4)<<4)|((control&8)<<12)|(seed&0x3f);
        unsigned expected_csr=csr;uint32_t expected[4];
        if(t->predicate<8) {
            compare_oracle(expected,regs[t->dst],input,t->predicate,&expected_csr);
            memcpy(want.xmm[t->dst],expected,16);
        } else if(t->predicate==8) want.r[t->dst]=mask_oracle(input,&expected_csr);
        if(t->predicate<=8)want.r[4]+=4;
        _mm_setcsr(csr);fault_ip=0;t->run(&c);unsigned after=_mm_getcsr();
        assert(after==expected_csr);if(memcmp(&c,&want,sizeof c)!=0){fprintf(stderr,"case=%u seed=%u control=%u sp=%x expected_sp=%x fault=%x\n",n,seed,control,c.r[4],want.r[4],fault_ip);abort();}
        assert(fault_ip==(t->predicate>8?0x11000+n*16:0));
        assert(memcmp(arena,before,sizeof arena)==0);++count;
    }
    _mm_setcsr(saved);printf("%u emitted register/alias/m32/reserved/full-context/native-FP comparisons\n",count);
}
int main(void)
{
    g_xram=arena;g_img_base=arena;g_xpt=pages;
    for(unsigned i=0;i<16;++i)pages[i]=(i^1)*4096;
    memset(arena,0xdb,sizeof arena);check_integer_comparison();check_generated();return 0;
}
#endif
