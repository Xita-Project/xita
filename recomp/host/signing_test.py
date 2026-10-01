#!/usr/bin/env python3
"""Test Halo 3925's own recompiled XAPI signing functions against Python HMAC.
Requires local recomp/code_*.c from the user's own XBE, generated with --lift
XCalculateSignatureBegin XCalculateSignatureUpdate XCalculateSignatureEnd.
Generated test code and
binaries stay in the ignored host/build directory. Kernel SHA is real; allocation,
free and error reporting are controlled to check both success and failure paths.
"""
from pathlib import Path
import re,hashlib,hmac,subprocess
root=Path(__file__).resolve().parents[2]
names=['00015A96','00015AF8','00015B6A','00015BA5','00015BE3','00015BFD','0001911C','00019122','00019128']
sources = sorted((root/'recomp').glob('code_*.c'))
if not sources:
 raise SystemExit('Run tools/recomp.sh with your own Halo 3925 XBE first.')
source=''.join(p.read_text() for p in sources)
functions=[]
for n in names:
 m=re.search(r'^void f_'+n+r'\(xctx \*restrict c\)\n\{.*?^\}',source,re.M|re.S)
 if not m:
  raise SystemExit('Missing Halo 3925 signing function '+n+'; regenerate using --lift XCalculateSignatureBegin XCalculateSignatureUpdate XCalculateSignatureEnd (experimental signing).')
 functions.append(m.group())
key=hmac.new(bytes.fromhex('5c0733ae0401f7e8ba7993fdcd2f1fe0'),bytes(range(16)),hashlib.sha1).digest()[:16]
message=bytes((i*17+i//256)%256 for i in range(10000))
digest=hmac.new(key,message,hashlib.sha1).digest()
expected=[digest,hmac.new(bytes(16),digest,hashlib.sha1).digest()]
head=r'''
#define xv_call test_call
#include "xv_recomp_protos.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
static unsigned frees, fail_alloc, error;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
uint32_t xk_kalloc(uint32_t n) { static uint32_t a=0x7000; uint32_t p=a; a+=n; return p; }
extern void xk_crypto_init(uint32_t,uint32_t);
extern uint32_t xk_crypto_export(unsigned);
extern void xk_XcSHAInit(xctx*),xk_XcSHAUpdate(xctx*),xk_XcSHAFinal(xctx*);
void test_call(xctx *c, uint32_t a) {
 switch(a) { case 0xfe00014f: xk_XcSHAInit(c); break; case 0xfe000150:xk_XcSHAUpdate(c); break; case 0xfe000151:xk_XcSHAFinal(c); break; default:assert(0); }
}
void f_00013585(xctx *c) { assert(X_ARG(0)==0 && X_ARG(1)==124); c->r[0]=fail_alloc?0:0x8ff0; X_RET(2); }
void f_00013567(xctx *c) { assert(X_ARG(0)==0x8ff0); frees++; c->r[0]=0; X_RET(1); }
void f_00013677(xctx *c) { error=X_ARG(0); X_RET(1); }
void f_0001364F(xctx *c) { c->r[0]=error; X_RET(0); }
static uint32_t invoke(void (*fn)(xctx*), unsigned nargs, uint32_t a,uint32_t b,uint32_t d) {
 xctx c={0};c.preempt=1000000;c.r[4]=0x6000;
 c.r[3]=333;c.r[5]=555;c.r[6]=666;c.r[7]=777;
 X_M32(0x6004)=a;X_M32(0x6008)=b;X_M32(0x600c)=d;
 fn(&c);
 assert(c.r[4]==0x6004+nargs*4 && c.r[3]==333 && c.r[5]==555 && c.r[6]==666 && c.r[7]==777);
 return c.r[0];
}
'''
expected_c=','.join('{'+','.join(map(str,x))+'}' for x in expected)
main=r'''
int main(void) {
 g_xram=calloc(1,4*1024*1024);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=4096*i;
 /* Split the signature allocation after 16 bytes. */
 g_xpt[9]=0x300000;
 X_M32(0x10118)=0x10200;for(unsigned i=0;i<16;i++)X_M8(0x102c0+i)=i;
 xk_crypto_init(0x10000,0x300000);
 X_M32(0x1d6740)=0xfe000150;X_M32(0x1d6744)=0xfe00014f;X_M32(0x1d6748)=0xfe000151;
 X_M32(0x1d674c)=xk_crypto_export(323);X_M32(0x1d6750)=xk_crypto_export(325);
 for(unsigned i=0;i<10000;i++)X_M8(0x20000+i)=(uint8_t)(i*17+i/256);
 const uint8_t expected[2][20]={EXPECTED};
 for(unsigned flags=0;flags<2;flags++) {
   unsigned h=invoke(f_00015BA5,1,flags,0,0);assert(h==0x8ff0);
   assert(!invoke(f_00015BE3,3,h,0x20000,123));
   assert(!invoke(f_00015BE3,3,h,0x20000+123,9877));
   assert(!invoke(f_00015BFD,2,h,0x40000,0));
   for(unsigned i=0;i<20;i++)assert(X_M8(0x40000+i)==expected[flags][i]);
 }
 assert(frees==2);fail_alloc=1;
 assert(invoke(f_00015BA5,1,0,0,0)==0xffffffff && error==8);
 free(g_xram);free(g_xpt);
 puts("PASS: original XAPI Begin/Update/End, roaming/non-roaming signatures, fragmented context, allocation/free/error and register/stack ABI");
}
'''.replace('EXPECTED',expected_c)
build=root/'recomp/host/build'; build.mkdir(exist_ok=True)
p=build/'original_signing.c';p.write_text(head+'\n'.join(functions)+main)
binary=str(build/'original_signing')
subprocess.run(['cc','-O1','-g','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-std=gnu11','-I'+str(root/'recomp'),str(p),str(root/'recomp/kernel/xk_crypto.c'),str(root/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',binary],check=True)
subprocess.run([binary],check=True)
