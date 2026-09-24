#!/usr/bin/env python3
"""Install the native BSP sphere query under f_0004B9D0 (XV_NATIVE_4B9D0) into a retained stage (idempotent).
Usage: install_native_4b9d0.py <stage>          (the directory holding Makefile, games/, recomp/)

Copies recomp/kernel/xk_native_4b9d0.c from this checkout, adds the XV_NATIVE_4B9D0 Makefile block after the stage's
XV_NATIVE_92330 -ffp-contract rule, the runtime.mk source line, the weak report call in recomp/kernel/xd3d.c, and the
hook in recomp/kernel/xk_query_reuse.c (tools/patch_native_4b9d0_hooks.py). Build with XV_NATIVE_4B9D0=1 in the make
vars; see docs/native-4b9d0.md."""
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
    shutil.copy2(ROOT / 'recomp/kernel/xk_native_4b9d0.c', stage / 'recomp/kernel/xk_native_4b9d0.c')
    print(f'{stage}/recomp/kernel/xk_native_4b9d0.c: copied')
    mk = (ROOT / 'Makefile').read_text()
    i = mk.index('# Native BSP sphere query under f_0004B9D0')
    j = mk.index('$(RECOMP_BUILD)/kernel/xk_native_4b9d0.o:'); j = mk.index('\n', j) + 1
    edit(stage / 'Makefile', '$(RECOMP_BUILD)/kernel/xk_native_92330.o: RECOMP_CFLAGS += -ffp-contract=off\n', mk[i:j],
         'XV_NATIVE_4B9D0 ?=')
    edit(stage / 'games/halo_ce_3925/runtime.mk', 'ifeq ($(XV_NATIVE_92330),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_92330.c\nendif\n',
         'ifeq ($(XV_NATIVE_4B9D0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_4b9d0.c\nendif\n', 'xk_native_4b9d0.c')
    edit(stage / 'recomp/kernel/xd3d.c',
         '{ extern void xv_native_92330_report(unsigned) __attribute__((weak)); if (xv_native_92330_report) xv_native_92330_report(60); } ',
         '{ extern void xv_native_4b9d0_report(unsigned) __attribute__((weak)); if (xv_native_4b9d0_report) xv_native_4b9d0_report(60); } ',
         'xv_native_4b9d0_report')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_4b9d0_hooks.py'), str(stage / 'recomp')], check=True)
if __name__ == '__main__': main()
