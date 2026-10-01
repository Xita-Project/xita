#!/usr/bin/env python3
"""Compare emitted SSE C with native x86 SSE; requires iced-x86 and an x86 host."""
from pathlib import Path
from types import SimpleNamespace
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from iced_x86 import Decoder
from recompiler.xita_recomp import Emitter, Function

emitter = Emitter(SimpleNamespace(base=0x10000, m={'size_of_image': 0x1000}),
                  SimpleNamespace(functions={}), {}, {}, '', 1)
wrappers, checks = [], []
fixtures = [('movhlps', '0f12', 'movehl'), ('movlhps', '0f16', 'movelh'),
            ('unpcklps', '0f14', 'unpacklo'), ('unpckhps', '0f15', 'unpackhi'),
            ('minps', '0f5d', 'min'), ('maxps', '0f5f', 'max'),
            ('andnps', '0f55', 'andnot'), ('divps', '0f5e', 'div')]
for predicate, intrinsic in enumerate(['cmpeq', 'cmplt', 'cmple', 'cmpunord',
                                       'cmpneq', 'cmpnlt', 'cmpnle', 'cmpord']):
    fixtures += [(f'cmp{predicate}ps', '0fc2', intrinsic, predicate),
                 (f'cmp{predicate}ss', 'f30fc2', intrinsic, predicate)]
for fixture in fixtures:
    name, opcode, intrinsic, *imm = fixture
    for mode, modrm in [('reg', 'c1'), ('alias', 'c0'), ('mem', '00')]:
        if mode == 'mem' and name in ('movhlps', 'movlhps'):
            continue
        ident = f'{name}_{mode}'
        ins = Decoder(32, bytes.fromhex(opcode + modrm + (f'{imm[0]:02x}' if imm else '')), ip=0x1000).decode()
        lines = []
        emitter.lower(Function(0x1000), ins, lines)
        wrappers.append(f'static void {ident}(xctx *c) {{\n' + '\n'.join(lines) + '\n}')
        scalar = name.endswith('ss')
        source = 'a' if mode == 'alias' else 'b'
        checks.append(f'''reset(&c, av, bv, {4 if scalar else 16});
            {ident}(&c);
            expected = _mm_{intrinsic}_{'ss' if scalar else 'ps'}(a, {source});
            _mm_storeu_ps(want, expected);
            if (memcmp(c.xmm[0], want, 16)) {{ fprintf(stderr, "{ident} case %u failed\\n", n); return 1; }}'''
                      if name != 'divps' else f'''if (n == 0) {{
            reset(&c, av, bv, 16); {ident}(&c);
            _mm_storeu_ps(want, _mm_div_ps(a, {source}));
            if (memcmp(c.xmm[0], want, 16)) return 2;
            }}''')
lines = []
emitter.lower(Function(0x1000), Decoder(32, bytes.fromhex('0f50c1'), ip=0x1000).decode(), lines)
wrappers.append('static void mask(xctx *c) {\n' + '\n'.join(lines) + '\n}')
assert not emitter.unimpl, emitter.unimpl
# Out-of-function loop targets must use dispatch, never an undefined local label.
for opcode in ('e0', 'e1', 'e2'):
    lines = []
    emitter.lower(Function(0x1000), Decoder(32, bytes.fromhex(opcode+'80'), ip=0x1000).decode(), lines)
    assert 'goto L_' not in '\n'.join(lines)

source = r'''
#include "xv_x86rt.h"
#include <xmmintrin.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static unsigned access_size;
void x_guest_read_pages(void *dst, uint32_t a, size_t size) {
    if (size != access_size) abort();
    for (size_t i = 0; i < size; ++i) ((uint8_t *)dst)[i] = X_M8(a + i);
}
static void reset(xctx *c, const uint32_t *a, const uint32_t *b, unsigned size) {
    access_size = size;
    memset(c, 0, sizeof(*c)); memcpy(c->xmm[0], a, 16); memcpy(c->xmm[1], b, 16);
    c->r[0] = size == 4 ? 0x1ffc : 0x1ff8;
    for (unsigned i = 0; i < size; ++i) X_M8(c->r[0] + i) = ((const uint8_t *)b)[i];
}
''' + '\n'.join(wrappers) + r'''
int main(void) {
    uint32_t pt[3] = {0, 0, 8192}; uint8_t ram[12288];
    g_xpt = pt; g_xram = ram; memset(ram, 0xa5, sizeof(ram));
    const uint32_t values[][4] = {
        {0x3f800000, 0xc0000000, 0x40800000, 0x41000000},
        {0, 0x80000000, 0x7f800000, 0xff800000},
        {0x7fc12345, 0xffc54321, 0x80000000, 0},
        {0x7f812345, 0xff854321, 0x7fffffff, 0xffffffff}
    };
    for (unsigned n = 0; n < 16; ++n) {
        const uint32_t *av = values[n / 4], *bv = values[n % 4];
        __m128 a, b, expected; memcpy(&a, av, 16); memcpy(&b, bv, 16);
        xctx c; float want[4];
''' + '\n'.join(checks) + r'''
        reset(&c, av, bv, 16); mask(&c);
        if (c.r[0] != (unsigned)_mm_movemask_ps(b)) return 3;
    }
    puts("SSE lowering matches native SSE: register, alias, split-page memory and all comparison predicates");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xita-sse-') as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(source)
    for optimization in ('-O0', '-O2'):
        subprocess.run(['cc', '-std=gnu11', optimization, '-fno-strict-aliasing', '-I', str(ROOT / 'recomp'),
                        str(path / 'test.c'), '-lm', '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
