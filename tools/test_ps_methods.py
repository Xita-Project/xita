#!/usr/bin/env python3
"""Exercise the actual NV2A state handlers without a GPU or game data."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'recomp/kernel/xd3d.c').read_text()
def function(signature):
    begin = src.index(signature + '\n{') if signature + '\n{' in src else src.index(signature + ' {')
    brace = src.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[begin:end]

code = r'''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "xv_stencil.h"
struct { xv_stencil stencil; uint32_t alpha_test, alpha_blend, color_mask, fog_color, alpha_func,
 alpha_ref, src_blend, dst_blend, blend_op, z_func, z_write, ps_shadow[60], ps_dirty; } xd3d_state;
struct { unsigned m,n,last; } g_hm[128];
unsigned g_nhm;
static int ps_synced;
static int xd3d_hist_active(void) { return 0; }
#define D3DLOG(...) ((void)0)
#define xk_os_log(...) ((void)0)
#define XD3D_COUNT(...) ((void)0)
typedef struct { uint32_t arg; unsigned popped; } xctx;
#define X_ARG(n) (c->arg)
#define X_RET(n) do { c->popped = (n); return; } while (0)
'''
code += function('static int ps_method_to_def(uint32_t m)') + '\n'
code += function('static void rs_method(uint32_t method, uint32_t v)') + '\n'
code += function('void xv_hle_D3DDevice_SetRenderState_PSTextureModes(xctx *c)') + '\n'
code += function('void xv_hle_D3DDevice_SetRenderState_StencilEnable(xctx *c)') + '\n'
code += function('void xv_hle_D3DDevice_SetRenderState_StencilFail(xctx *c)') + '\n'
code += r'''
int main(void) {
    /* A first zero-valued method must initialize the shader identity. */
    rs_method(0x1E70, 0);
    assert(xd3d_state.ps_dirty);
    ps_synced = 1;
    xd3d_state.ps_dirty = 0;
    rs_method(0x1E70, 0);
    assert(!xd3d_state.ps_dirty);
    /* CUBEMAP, DOTPRODUCT, DOT_ST: the camo coordinate pipeline. */
    uint32_t mode = 3 | (17 << 5) | (9 << 10);
    assert(ps_method_to_def(0x1E70) == 0xD8);
    assert(ps_method_to_def(0x1D90) == -1); /* color clear is unrelated */
    rs_method(0x41E70, mode); /* push method includes packet size */
    assert(xd3d_state.ps_dirty && xd3d_state.ps_shadow[0xD8 / 4] == mode);
    xd3d_state.ps_dirty = 0;
    rs_method(0x1D90, 0xFF8800FF);
    assert(!xd3d_state.ps_dirty && xd3d_state.ps_shadow[0xD8 / 4] == mode);
    xctx c = {1,0};
    xv_hle_D3DDevice_SetRenderState_PSTextureModes(&c);
    assert(c.popped == 1 && xd3d_state.ps_dirty && xd3d_state.ps_shadow[0xD8 / 4] == 1);
    rs_method(0x1E74, 0x11);
    rs_method(0x1E78, 0x10000);
    assert(xd3d_state.ps_shadow[0xDC / 4] == 0x11);
    assert(xd3d_state.ps_shadow[0xE0 / 4] == 0x10000);
    c.arg=1; xv_hle_D3DDevice_SetRenderState_StencilEnable(&c);
    assert(c.popped==1 && xd3d_state.stencil.enabled);
    c.arg=0x1e01; xv_hle_D3DDevice_SetRenderState_StencilFail(&c);
    assert(c.popped==1 && xd3d_state.stencil.fail==2);
    rs_method(0x40378,0x1e01); rs_method(0x40364,0x202); rs_method(0x40368,0x101);
    rs_method(0x4036c,0x103); rs_method(0x40360,0x1ff);
    assert(xd3d_state.stencil.pass==2 && xd3d_state.stencil.func==2 && xd3d_state.stencil.ref==1);
    assert(xd3d_state.stencil.read_mask==3 && xd3d_state.stencil.write_mask==255);
    puts("NV2A texture-mode and stencil handlers: pass");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-ps-methods-') as tmp:
    test = Path(tmp) / 'test.c'
    test.write_text(code)
    exe = Path(tmp) / 'test'
    subprocess.run(['cc', '-I', str(root), '-std=c11', '-Wall', '-Wextra', '-Werror', str(test), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
