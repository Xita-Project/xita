#!/usr/bin/env python3
"""Check call-local sampler translations against the retained ordered helper.

No game source is embedded. --output-dir retains a fixture for ARM/Pi execution.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='cc')
    ap.add_argument('--cflags', default='-fsanitize=address,undefined -fno-omit-frame-pointer')
    ap.add_argument('--output-dir', type=Path)
    ap.add_argument('--build-only', action='store_true')
    a = ap.parse_args()
    header = (ROOT / 'recomp/kernel/xk_material_sampler.h').read_text().replace('#pragma once', '').replace('../xv_x86rt.h', 'recomp/xv_x86rt.h')
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "recomp/xv_x86rt.h"
#define MEM 65536u
static unsigned char memory[MEM], initial[MEM], expected[MEM];
static uint32_t pt[1u<<20];
uint8_t *g_xram = memory;
uint32_t *g_xpt = pt;
static unsigned remap, write_index;
#undef X_GW
static void *write_pointer(uint32_t a) {
    unsigned word=(a-0x18f180u)&127u;
    if(remap && a>=0x18f180u && a<0x18f380u && word>=40u && word<=60u) {
        pt[0x18f]=((write_index++&1)?4:2)*4096;
    }
    return X_G(a);
}
#define X_GW(a) write_pointer((uint32_t)(a))
static uint32_t rng=713;
static uint32_t next(void) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }
static void fill(void *p, size_t n) { unsigned char *b=p; while(n--) *b++=next(); }
'''
    source += '\n#define XV_MATERIAL_SAMPLER_POINTERS 0\n' + header.replace('xv_material_sampler_defaults', 'reference').replace('static inline void', 'static __attribute__((noinline)) void')
    source += '\n#undef XV_MATERIAL_SAMPLER_POINTERS\n#define XV_MATERIAL_SAMPLER_POINTERS 1\n' + header.replace('static inline void', 'static __attribute__((noinline)) void')
    source += r'''
int main(int argc, char **argv) {
    if(argc>1) {
        xctx c={0}; c.r[4]=0x6800; pt[6]=8192; pt[0x18f]=16384;
        struct timespec a,b; unsigned candidate=atoi(argv[1]);
        clock_gettime(CLOCK_MONOTONIC,&a);
        for(unsigned i=0;i<2000000;i++) {
            if(candidate) xv_material_sampler_defaults(&c,i&3);
            else reference(&c,i&3);
        }
        clock_gettime(CLOCK_MONOTONIC,&b);
        double ns=(b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec;
        printf("candidate=%u ns/group=%.3f final=%u/%u/%u\n",candidate,ns/2000000,c.r[1],c.r[2],c.r[4]);
        return 0;
    }
    unsigned cases=0;
    for (unsigned stage=0; stage<4; stage++) for(unsigned k=0;k<4096;k++) {
        xctx local, saved, want;
        fill(memory,MEM); fill(&local,sizeof(local));
        /* Relocate the same virtual pages on every call. Include distinct
         * virtual pages aliasing one physical page, all table/stack overlaps,
         * unaligned and boundary-spanning stacks, and wrapped address space. */
        pt[0x18f]=((k&1)?2:4)*4096;
        pt[5]=4096; pt[6]=(k&2)?pt[0x18f]:6*4096; pt[7]=8*4096;
        pt[0]=9*4096; pt[0xfffff]=10*4096;
        local.r[4]= k<1024 ? 0x18f170u+k : k<2048 ? 0x6000u+(k&1023) :
                     k<3072 ? 0x6ff0u+(k&31) : (uint32_t)((int)(k&31)-8);
        xctx *c=&local;
        if (k>=4000) {
            /* Stack or texture writes can alias the host xctx: the candidate
             * must decline its pointer shortcut and retain ordered accesses. */
            c=(xctx *)(memory+pt[0x18f]);
            memcpy(c,&local,sizeof local);
        }
        remap=(k&4)!=0; write_index=0;
        uint32_t mapping=pt[0x18f];
        saved=*c; memcpy(initial,memory,MEM);
        reference(c,stage); want=*c; memcpy(expected,memory,MEM);
        memcpy(memory,initial,MEM); *c=saved; pt[0x18f]=mapping; write_index=0;
        xv_material_sampler_defaults(c,stage);
        if(memcmp(c,&want,sizeof want)||memcmp(memory,expected,MEM)) {
            fprintf(stderr,"mismatch stage %u case %u\n",stage,k); return 1;
        }
        cases++;
    }
    printf("%u full-context/memory sampler pointer comparisons passed\n",cases);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='xita-sampler-pointers-') as tmp:
        d = a.output_dir or Path(tmp)
        d.mkdir(parents=True, exist_ok=True)
        (d/'test.c').write_text(source)
        subprocess.run([a.cc, '-O2', '-fno-strict-aliasing', *shlex.split(a.cflags),
                        '-I', str(ROOT), str(d/'test.c'), '-o', str(d/'test')], check=True)
        if not a.build_only:
            subprocess.run([str(d/'test')], check=True)


if __name__ == '__main__':
    main()
