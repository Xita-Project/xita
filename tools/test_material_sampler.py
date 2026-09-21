#!/usr/bin/env python3
"""Compare a native sampler sequence with the user's retained generated CE body.

No game code is embedded in this test. Supply the private code unit containing
70110; the reference is extracted into a temporary directory, then removed.
"""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile

from test_draw_state_batch import function

ROOT = Path(__file__).resolve().parents[1]
BOUNDS = ((0x70440, 0x70486), (0x70498, 0x704ED),
          (0x704FF, 0x70554), (0x70566, 0x705CC))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--shard', type=Path, required=True)
    args = ap.parse_args()
    body = function(args.shard.read_text(), 'f_00070110')
    refs = []
    for stage, (start, end) in enumerate(BOUNDS):
        a = body.index(f'    /* {start:08X} ')
        b = body.index(f'    /* {end:08X} ', a)
        block = body[a:b]
        if block.count('XV_HLE_CALL(') != (6 if stage == 3 else 5) or '\nL_' in block:
            raise ValueError('unexpected sampler control flow')
        if block.count('xv_hle_D3DDevice_SetTextureState_Deferred') != (6 if stage == 3 else 5):
            raise ValueError('unexpected sampler callees')
        refs.append(f'static void reference{stage}(xctx *c) {{\n{block}\n}}\n')
    kernel = (ROOT / 'recomp/kernel/xd3d.c').read_text()
    original = function(kernel, 'xv_hle_D3DDevice_SetTextureState_Deferred')
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/xv_x86rt.h"
#define MEM (2u * 1024u * 1024u)
static unsigned char memory[MEM], expected[MEM];
#undef X_M32
#undef X_W32
/* Distinct physical halves exercise virtual-to-host translation as well as
 * unaligned table/stack overlap. Fixture addresses never straddle MEM. */
static void *guest(uint32_t a) {
    unsigned offset = (a ^ 0x100000u) & (MEM - 1u);
    assert(offset + 4 <= MEM);
    return memory + offset;
}
#define X_M32(a) (*(const xu32_u *)guest(a))
#define X_W32(a) (*(xu32_u *)guest(a))
#define D3D_G_TEXTURESTATE 0x18F180u
#define XD3D_COUNT(name) ((void)0)
#define XV_HLE_CALL(address, fn) fn(c)
#include "recomp/kernel/xk_material_sampler.h"
'''
    wrapper = function(kernel, 'xv_material_sampler_try')
    source += """
+#define XV_EXPERIMENTAL_OBJECT_JOBS 1
+#define XV_OWNER_SCENE 1
+static int worker, owner = 1;
static unsigned material_sampler_groups[4];
+static int xv_is_object_job(const xctx *c) { (void)c; return worker; }
+static int xv_owner_phase_active(void *c, unsigned phase, uint32_t *generation) {
+    assert(c && phase == XV_OWNER_SCENE && generation && !*generation);
+    *generation = 1; return owner;
+}
+""".replace("\n+", "\n")
    source += original + '\n' + wrapper + '\n' + '\n'.join(refs)
    source += r'''
static uint32_t rng = 731;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void fill(void *p, size_t n) { unsigned char *b=p; while(n--) *b++=(unsigned char)next(); }
int main(int argc, char **argv) {
    (void)argv;
    xctx rejected = {0}, saved;
    rejected.r[4] = 0x6000;
    saved = rejected;
    memcpy(expected, memory, MEM);
    if (argc > 1) {
        assert(setenv("XV_D3D_HIST", "1", 1) == 0);
        assert(!xv_material_sampler_try(&rejected, 0));
        assert(!memcmp(&rejected, &saved, sizeof saved));
        assert(!memcmp(memory, expected, MEM));
        puts("diagnostic fallback preserves context and memory"); return 0;
    }
    assert(!xv_material_sampler_try(NULL, 0));
    assert(!xv_material_sampler_try(&rejected, 4));
    worker = 1; assert(!xv_material_sampler_try(&rejected, 0)); worker = 0;
    owner = 0; assert(!xv_material_sampler_try(&rejected, 0)); owner = 1;
    assert(!memcmp(&rejected, &saved, sizeof saved));
    assert(!memcmp(memory, expected, MEM));
    void (*refs[4])(xctx *) = {reference0, reference1, reference2, reference3};
    unsigned cases=0;
    for (unsigned stage=0; stage<4; ++stage) {
        for (unsigned k=0; k<1024; ++k) {
            xctx baseline, candidate;
            fill(&baseline, sizeof baseline);
            /* Include every byte alignment and overlapping stack/table slot. */
            baseline.r[4] = k < 544 ? 0x18F170u + k : 0x6000u + (next() & 8191u);
            candidate = baseline;
            fill(memory + ((0x18F000u ^ 0x100000u) & (MEM-1)), 4096);
            fill(memory + ((0x5000u ^ 0x100000u) & (MEM-1)), 16384);
            memcpy(expected, memory, MEM);
            refs[stage](&baseline);
            /* Preserve reference result while restoring the initial guest RAM. */
            for (unsigned i=0; i<MEM; ++i) { unsigned char t=memory[i]; memory[i]=expected[i]; expected[i]=t; }
            assert(xv_material_sampler_try(&candidate, stage) == 1);
            if (memcmp(&baseline, &candidate, sizeof baseline)) {
                fprintf(stderr, "context mismatch stage=%u case=%u\n", stage, k);
                for (unsigned j=0; j<sizeof baseline; ++j)
                    if (((unsigned char*)&baseline)[j] != ((unsigned char*)&candidate)[j])
                        fprintf(stderr, " byte %u: %02x != %02x\n", j, ((unsigned char*)&baseline)[j], ((unsigned char*)&candidate)[j]);
                abort();
            }
            assert(!memcmp(memory, expected, MEM));
            ++cases;
        }
    }
    for (unsigned stage=0; stage<4; ++stage) assert(material_sampler_groups[stage] == 1024);
    printf("%u sampler context/memory comparisons passed\n", cases);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='xita-material-sampler-') as d:
        p = Path(d)
        (p / 'test.c').write_text(source)
        cmd = [os.environ.get('CC', 'cc'), '-O2', '-fno-strict-aliasing',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(ROOT), str(p/'test.c'), '-o', str(p/'test')]
        subprocess.run(cmd, check=True)
        subprocess.run([str(p/'test'), 'diagnostic'], check=True)
        subprocess.run([str(p/'test')], check=True)


if __name__ == '__main__':
    main()
