#!/usr/bin/env python3
"""Integrate the native lens-flare visibility test (XV_NATIVE_63C00) into a stage tree (idempotent).
Usage: install_native_63c00.py <stage>          (the directory holding Makefile, recomp/, games/)

  1. copies recomp/kernel/xk_native_63c00.c from this checkout into the stage;
  2. Makefile: the XV_NATIVE_63C00 block (flag, _DEFAULT, -ffp-contract=off for the unit) after the
     XV_NATIVE_VISIBILITY block's -ffp-contract=off line;
  3. games/halo_ce_3925/runtime.mk: the unit under ifeq ($(XV_NATIVE_63C00),1), after the XV_NATIVE_VISIBILITY entry;
  4. recomp/kernel/xd3d.c: the HLE-entry tap in XD3D_COUNT (verify/timing mode only, compiled with the flag) and the
     weak xv_native_63c00_report(60) call after xv_native_visibility_report;
  5. the entry hook in f_00063C00 (tools/patch_native_63c00_hooks.py).
Then build with XV_NATIVE_63C00=1 in the make variables. Nothing changes without the flag."""
import shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAKE_ANCHOR = '$(RECOMP_BUILD)/kernel/xk_native_visibility.o: RECOMP_CFLAGS += -ffp-contract=off\n'
MAKE_BLOCK = '''# Native lens-flare visibility test (f_00063C00 with f_000637A0, f_000B5EA0 and its four f_00019E7B floors; the D3D HLE
# calls are made as the guest makes them): recomp/kernel/xk_native_63c00.c, entry hook installed by
# tools/patch_native_63c00_hooks.py. Env XV_NATIVE_63C00 0 off / 1 verify / 2 native; XV_NATIVE_63C00_TIME=1 us/call.
XV_NATIVE_63C00 ?= 0
XV_NATIVE_63C00_DEFAULT ?= 0
ifneq ($(filter $(XV_NATIVE_63C00),0 1),$(XV_NATIVE_63C00))
$(error XV_NATIVE_63C00 must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_63C00_DEFAULT),0 1 2),$(XV_NATIVE_63C00_DEFAULT))
$(error XV_NATIVE_63C00_DEFAULT must be 0, 1 or 2)
endif
ifeq ($(XV_NATIVE_63C00),1)
CFLAGS += -DXV_NATIVE_63C00=1 -DXV_NATIVE_63C00_DEFAULT=$(XV_NATIVE_63C00_DEFAULT)
RECOMP_CFLAGS += -DXV_NATIVE_63C00=1 -DXV_NATIVE_63C00_DEFAULT=$(XV_NATIVE_63C00_DEFAULT)
endif
$(RECOMP_BUILD)/kernel/xk_native_63c00.o: RECOMP_CFLAGS += -ffp-contract=off
'''
MK_ANCHOR = 'ifeq ($(XV_NATIVE_VISIBILITY),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_visibility.c\nendif\n'
MK_BLOCK = 'ifeq ($(XV_NATIVE_63C00),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_63c00.c\nendif\n'
D3D_COUNT = '#define XD3D_COUNT(nm) (xd3d_count(nm), xd3d_hash_call(nm, c))\n'
D3D_TAP = ('#if defined(XV_NATIVE_63C00) && XV_NATIVE_63C00\n'
           '/* XV_NATIVE_63C00 verify/timing (kernel/xk_native_63c00.c): observes every D3D HLE entry (it filters by context). */\n'
           'void (*volatile xv_hle_tap)(xctx *, const char *);\n'
           '#define XD3D_TAP(nm) (xv_hle_tap ? xv_hle_tap(c, nm) : (void)0)\n'
           '#else\n'
           '#define XD3D_TAP(nm) ((void)0)\n'
           '#endif\n'
           '#define XD3D_COUNT(nm) (XD3D_TAP(nm), xd3d_count(nm), xd3d_hash_call(nm, c))\n')
D3D_REPORT = '{ extern void xv_native_visibility_report(unsigned) __attribute__((weak)); if (xv_native_visibility_report) xv_native_visibility_report(60); }'
D3D_REPORT_63 = ' { extern void xv_native_63c00_report(unsigned) __attribute__((weak)); if (xv_native_63c00_report) xv_native_63c00_report(60); }'

def edit(path, done_marker, anchor, new, what):
    s = path.read_text()
    if done_marker in s: print(f'{path}: {what} already present'); return
    if s.count(anchor) != 1: sys.exit(f'{path}: anchor for {what} found {s.count(anchor)} times')
    path.write_text(s.replace(anchor, new)); print(f'{path}: {what} added')

def main():
    if len(sys.argv) != 2: sys.exit(__doc__)
    stage = Path(sys.argv[1]).resolve()
    unit = stage / 'recomp/kernel/xk_native_63c00.c'
    shutil.copyfile(ROOT / 'recomp/kernel/xk_native_63c00.c', unit); print(f'{unit}: copied')
    edit(stage / 'Makefile', 'XV_NATIVE_63C00 ?= 0', MAKE_ANCHOR, MAKE_ANCHOR + MAKE_BLOCK, 'XV_NATIVE_63C00 block')
    edit(stage / 'games/halo_ce_3925/runtime.mk', 'xk_native_63c00.c', MK_ANCHOR, MK_ANCHOR + MK_BLOCK, 'unit')
    d3d = stage / 'recomp/kernel/xd3d.c'
    edit(d3d, 'xv_hle_tap', D3D_COUNT, D3D_TAP, 'HLE-entry tap')
    edit(d3d, 'xv_native_63c00_report', D3D_REPORT, D3D_REPORT + D3D_REPORT_63, 'report call')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_63c00_hooks.py'), str(stage / 'recomp')], check=True)

if __name__ == '__main__': main()
