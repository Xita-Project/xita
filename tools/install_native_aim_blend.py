#!/usr/bin/env python3
"""Install the native aim/look overlay blend (XV_NATIVE_AIM_BLEND) into a retained stage (idempotent).
Usage: install_native_aim_blend.py <stage>          (the directory holding Makefile, games/, recomp/)

Copies recomp/kernel/xk_native_aim_blend.c from this checkout, adds the XV_NATIVE_AIM_BLEND Makefile block after the
stage's XV_NATIVE_VISIBILITY -ffp-contract rule, the runtime.mk source line, the weak report call in
recomp/kernel/xd3d.c, and the entry hook in f_000A39B0 (tools/patch_native_aim_blend_hooks.py). Build with
XV_NATIVE_AIM_BLEND=1 in the make vars; see docs/native-aim-blend.md."""
import re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def edit(path, anchor, text, marker):
    s = path.read_text()
    if marker in s: print(f'{path}: already present'); return
    if s.count(anchor) != 1: sys.exit(f'{path}: anchor not found exactly once: {anchor.strip()[:80]}')
    path.write_text(s.replace(anchor, anchor + text)); print(f'{path}: updated')
def main():
    stage = Path(sys.argv[1])
    shutil.copy2(ROOT / 'recomp/kernel/xk_native_aim_blend.c', stage / 'recomp/kernel/xk_native_aim_blend.c')
    print(f'{stage}/recomp/kernel/xk_native_aim_blend.c: copied')
    mk = (ROOT / 'Makefile').read_text()
    i = mk.index('# Native 2-D aim/look overlay blend')
    j = mk.index('$(RECOMP_BUILD)/kernel/xk_native_aim_blend.o:'); j = mk.index('\n', j) + 1
    edit(stage / 'Makefile', '$(RECOMP_BUILD)/kernel/xk_native_visibility.o: RECOMP_CFLAGS += -ffp-contract=off\n', '\n' + mk[i:j],
         'XV_NATIVE_AIM_BLEND ?=')
    edit(stage / 'games/halo_ce_3925/runtime.mk', 'ifeq ($(XV_NATIVE_VISIBILITY),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_visibility.c\nendif\n',
         'ifeq ($(XV_NATIVE_AIM_BLEND),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_aim_blend.c\nendif\n', 'xk_native_aim_blend.c')
    edit(stage / 'recomp/kernel/xd3d.c',
         '{ extern void xv_native_visibility_report(unsigned) __attribute__((weak)); if (xv_native_visibility_report) xv_native_visibility_report(60); } ',
         '{ extern void xv_native_aim_blend_report(unsigned) __attribute__((weak)); if (xv_native_aim_blend_report) xv_native_aim_blend_report(60); } ',
         'xv_native_aim_blend_report')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_aim_blend_hooks.py'), str(stage / 'recomp')], check=True)
if __name__ == '__main__': main()
