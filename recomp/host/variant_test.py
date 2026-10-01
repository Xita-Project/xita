#!/usr/bin/env python3
"""Exercise Halo 3925's original variant loader with synthetic saved records.

Uses locally generated game code; none of that code or user saves are committed.
The fake disk isolates signature rejection from menus, networking and rendering.
"""
from pathlib import Path
import hashlib
import hmac
import os
import re
import shlex
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
functions = {}
for p in sorted((root / 'recomp').glob('code_*.c')):
    for name in ('0002F800', '001015A0'):
        m = re.search(r'^void f_' + name + r'\(.*?^\}', p.read_text(), re.M | re.S)
        if m:
            functions[name] = m[0]
if len(functions) != 2:
    raise SystemExit('Generate Halo 3925 with tools/recomp.sh first.')
record = bytearray(512)
record[:14] = 'TestAll'.encode('utf-16-le')
for offset, value in ((24, 2), (32, 3), (52, 300), (60, 0x3f800000), (64, 15)):
    struct.pack_into('<I', record, offset, value)
key = hmac.new(bytes.fromhex('5c0733ae0401f7e8ba7993fdcd2f1fe0'), bytes(range(16)), hashlib.sha1).digest()[:16]
expected = hmac.new(key, record[:104], hashlib.sha1).digest()
head = r'''
#define xv_call test_call
#include "xv_recomp_protos.h"
#include "kernel/xk.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#undef XV_FN
#undef XV_FN_BACK
#define XV_FN(a) ((void)0)
#define XV_FN_BACK(a) ((void)0)
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
static unsigned repair, old_sign, fallback, xapi_mode, xapi_calls;
static unsigned char disk[512] = {RECORD};
void xk_os_log(const char *fmt, ...) { (void)fmt; }
uint32_t xk_kalloc(uint32_t n) { static uint32_t a=0x7000; uint32_t p=a; a+=n; return p; }
/* Compile the actual signer against this controlled XAPI fallback. */
#include "kernel/xk_crypto.c"
void test_call(xctx *c, uint32_t addr) {
    if (addr == 0x15BA5) { assert(X_ARG(0)==0); xapi_calls++; c->r[0]=xapi_mode?0x700:0xffffffff; X_RET(1); }
    if (addr == 0x15BE3) { assert(xapi_mode && X_ARG(0)==0x700 && X_ARG(2)==48); xapi_calls++; c->r[0]=0; X_RET(3); }
    if (addr == 0x15BFD) { assert(xapi_mode && X_ARG(0)==0x700); xapi_calls++; memset(X_G(X_ARG(1)),0x42,20); c->r[0]=0; X_RET(2); }
    fprintf(stderr,"unexpected dispatch %X\n",addr); abort();
}
/* On current generated sources the signer is a direct HLE call. Also accept
 * pre-regeneration sources so the regression can be run before a full build. */
void test_sign(xctx *c) {
    if (old_sign) { c->r[0]=0xffffffff; X_RET(3); }
    xv_hle_HaloSignSavedRecord(c);
}
#define f_0002D120 test_sign
#define xv_hle_HaloSignSavedRecord test_sign
void f_00012AB6(xctx *c) { abort(); }
void f_00012C44(xctx *c) { abort(); }
void f_00012E8F(xctx *c) { c->r[0]=0; X_RET(2); }
void f_00012D59(xctx *c) { c->r[0]=0; X_RET(1); }
void f_0002EE30(xctx *c) { c->r[0]=1; X_RET(1); }
void f_0002EDF0(xctx *c) { X_RET(1); }
void f_00050930(xctx *c) {
    assert(c->r[6]==512); x_guest_write(c->r[1],disk,512);
    if (repair) xk_variant_recover_unsigned(c->r[1]);
    c->r[0]=1; X_RET(0);
}
void f_0002EED0(xctx *c) { fallback++; c->r[0]=0x8000; X_RET(0); }
void f_0001B712(xctx *c) {
    uint32_t dst=X_ARG(0),src=X_ARG(1),n=X_ARG(2);
    for(unsigned i=0;i<n;i++) { uint16_t v=X_M16(src+2*i); X_M16(dst+2*i)=v; if(!v)break; }
    X_RET(0); /* cdecl */
}
'''.replace('RECORD', ','.join(map(str, record)))
main = r'''
#undef xv_hle_HaloSignSavedRecord
static void load_variant(unsigned want_vehicle) {
    xctx c={0}; c.preempt=1000000; c.r[4]=0x6000;
    c.r[3]=333;c.r[5]=555;c.r[6]=666;c.r[7]=777;
    memset(X_G(0x5000),0xa5,4096); /* reject dependence on clean stack memory */
    X_M32(0x6004)=0x80000001;X_M32(0x6008)=0x9000;
    f_0002F800(&c);
    assert((c.r[0]&255)==1 && c.r[4]==0x600c);
    assert(c.r[3]==333 && c.r[5]==555 && c.r[6]==666 && c.r[7]==777);
    assert(X_M32(0x9048)==want_vehicle);
    assert(!memcmp(X_G(0x9000),disk,14)); /* custom name survives even fallback */
}
int main(void) {
    g_xram=calloc(1,4*1024*1024);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=4096*i;
    X_M32(0x10118)=0x10200;for(unsigned i=0;i<16;i++)X_M8(0x102c0+i)=i;
    xk_crypto_init(0x10000,0x300000);X_M32(0x2E335C)=0x3000;
    memcpy(X_G(0x8000),disk,24);
    old_sign=1;load_variant(2);assert(fallback==1); /* old behavior: renamed Slayer */
    old_sign=0;repair=1;load_variant(0);assert(fallback==1);
    assert(!memcmp(X_G(0x9000),disk,104)); /* every custom option survives */
    for(unsigned vehicle=0;vehicle<=4;vehicle++) {
        disk[72]=vehicle;load_variant(vehicle);assert(fallback==1);
        assert(!memcmp(X_G(0x9000),disk,104));
    }
    disk[72]=0;
    assert(!memcmp(disk+104,(uint8_t[20]){0},20)); /* migration never writes disk */
    const uint8_t expected[20]={EXPECTED};
    x_guest_write(0x9000,disk,512);
    assert(xk_variant_recover_unsigned(0x9000));
    assert(!memcmp(X_G(0x9068),expected,20)); /* independent Python HMAC */
    assert(!xk_variant_recover_unsigned(0x9000));
    memcpy(disk+104,expected,20);load_variant(0);assert(fallback==1);
    disk[104]^=0x80;load_variant(2);assert(fallback==2); /* corrupt signed record rejected */
    memset(disk+104,0,20);memcpy(disk+108,"u:\\122A17771B9E\\",16);
    load_variant(0);assert(fallback==2); /* observed generated-playlist stack residue */
    memset(disk+104,0,20);disk[72]=5;load_variant(2);assert(fallback==3);disk[72]=0;
    /* Fragment both the variant and its digest; neither can assume contiguous pages. */
    g_xpt[10]=0x300000;g_xpt[12]=0x301000;
    x_guest_write(0x9ff0,disk,512);assert(xk_variant_recover_unsigned(0x9ff0));
    xctx c={0};c.r[4]=0x6000;c.r[6]=666;
    X_M32(0x6004)=0x9ff0;X_M32(0x6008)=104;X_M32(0x600c)=0xbff5;
    X_M8(0xbff4)=0x5a;X_M8(0xc009)=0xa5;
    xv_hle_HaloSignSavedRecord(&c);
    uint8_t got[20];x_guest_read(got,0xbff5,20);assert(!memcmp(got,expected,20));
    assert(c.r[4]==0x6010 && c.r[6]==666 && X_M8(0xbff4)==0x5a && X_M8(0xc009)==0xa5);
    /* Profile helper keeps both unsigned compatibility and opt-in real XAPI ABI. */
    for(xapi_mode=0;xapi_mode<2;xapi_mode++) {
        c.r[4]=0x6000;c.r[6]=666;X_M32(0x6008)=48;X_M32(0x600c)=0xd000;
        memset(X_G(0xd000),0x5a,20);xapi_calls=0;xv_hle_HaloSignSavedRecord(&c);
        assert(c.r[4]==0x6010 && c.r[6]==666 && xapi_calls==(xapi_mode?3:1));
        for(unsigned i=0;i<20;i++)assert(X_M8(0xd000+i)==(xapi_mode?0x42:0x5a));
    }
    free(g_xram);free(g_xpt);
    puts("PASS: original variant loader reproduces renamed-Slayer fallback; recovery preserves All vehicles; HMAC, corrupt signatures, legacy playlists, split pages and profile ABI");
}
'''.replace('EXPECTED', ','.join(map(str, expected)))
build = root / 'recomp/host/build'
build.mkdir(exist_ok=True)
p = build / 'original_variant.c'
p.write_text(head + '\n'.join(functions.values()) + main)
binary = build / 'original_variant'
flags = shlex.split(os.environ.get('VARIANT_TEST_CFLAGS', '-O1 -g'))
subprocess.run(['cc', *flags, '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
                '-std=gnu11', '-I' + str(root / 'recomp'), str(p), str(root / 'recomp/xv_x86rt.c'),
                '-Wl,--gc-sections', '-lm', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
