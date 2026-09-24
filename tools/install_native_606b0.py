#!/usr/bin/env python3
"""Install the native lens-flare scene work (XV_NATIVE_606B0: the per-flare region of f_000606B0 and f_000602F0) into
a retained stage (idempotent).
Usage: install_native_606b0.py <stage>          (the directory holding Makefile, games/, recomp/)

Copies recomp/kernel/xk_native_606b0.c from this checkout, adds the XV_NATIVE_606B0 Makefile block after the stage's
XV_NATIVE_AIM_BLEND -ffp-contract rule, the runtime.mk source line, the weak report call in recomp/kernel/xd3d.c, and
the hooks in f_000606B0 / f_000602F0 (tools/patch_native_606b0_hooks.py). Build with XV_NATIVE_606B0=1 in the make
vars; see docs/native-606b0.md."""
import shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def edit(path, anchor, text, marker):
    s = path.read_text()
    if marker in s: print(f'{path}: already present'); return
    if s.count(anchor) != 1: sys.exit(f'{path}: anchor not found exactly once: {anchor.strip()[:80]}')
    path.write_text(s.replace(anchor, anchor + text)); print(f'{path}: updated')
def main():
    stage = Path(sys.argv[1])
    shutil.copy2(ROOT / 'recomp/kernel/xk_native_606b0.c', stage / 'recomp/kernel/xk_native_606b0.c')
    print(f'{stage}/recomp/kernel/xk_native_606b0.c: copied')
    mk = (ROOT / 'Makefile').read_text()
    i = mk.index('# Native lens-flare scene work')
    j = mk.index('$(RECOMP_BUILD)/kernel/xk_native_606b0.o:'); j = mk.index('\n', j) + 1
    edit(stage / 'Makefile', '$(RECOMP_BUILD)/kernel/xk_native_aim_blend.o: RECOMP_CFLAGS += -ffp-contract=off -fno-math-errno\n',
         '\n' + mk[i:j], 'XV_NATIVE_606B0 ?=')
    edit(stage / 'games/halo_ce_3925/runtime.mk', 'ifeq ($(XV_NATIVE_AIM_BLEND),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_aim_blend.c\nendif\n',
         'ifeq ($(XV_NATIVE_606B0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_606b0.c\nendif\n', 'xk_native_606b0.c')
    edit(stage / 'recomp/kernel/xd3d.c',
         '{ extern void xv_native_aim_blend_report(unsigned) __attribute__((weak)); if (xv_native_aim_blend_report) xv_native_aim_blend_report(60); } ',
         '{ extern void xv_native_606b0_report(unsigned) __attribute__((weak)); if (xv_native_606b0_report) xv_native_606b0_report(60); } ',
         'xv_native_606b0_report')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_606b0_hooks.py'), str(stage / 'recomp')], check=True)
if __name__ == '__main__': main()
