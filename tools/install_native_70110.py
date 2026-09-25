#!/usr/bin/env python3
"""Install the native material setup (XV_NATIVE_70110: f_00070110) into a retained stage (idempotent).
Usage: install_native_70110.py <stage>          (the directory holding Makefile, games/, recomp/)

Copies recomp/kernel/xk_native_70110.c from this checkout, adds the XV_NATIVE_70110 Makefile block after the stage's
XV_NATIVE_1721B0 -ffp-contract rule (or, in a stage without it, XV_NATIVE_4B9D0's or XV_NATIVE_92330's), the runtime.mk
source line and the weak report call in recomp/kernel/xd3d.c next to the same natives', and the hook with the tapped
copy of f_00070110 (tools/patch_native_70110_hooks.py). Every file it changes is rewritten as a new file (a stage copy
made with hard links keeps the original untouched). Build with XV_NATIVE_70110=1 in the make vars; see
docs/native-70110.md."""
import os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def write_new(path, text):
    tmp = path.with_name(path.name + '.n70tmp'); tmp.write_text(text); os.replace(tmp, path)
def edit(path, anchors, text, marker):
    """Insert text after the first of the anchors found exactly once (the stage's newest native integration)."""
    s = path.read_text()
    if marker in s: print(f'{path}: already present', flush=True); return
    for anchor in anchors:
        if s.count(anchor) == 1:
            write_new(path, s.replace(anchor, anchor + text)); print(f'{path}: updated (after {anchor.strip()[:60]!r})', flush=True)
            return
    sys.exit(f'{path}: no anchor found exactly once: {[a.strip()[:60] for a in anchors]}')
def main():
    stage = Path(sys.argv[1])
    dst = stage / 'recomp/kernel/xk_native_70110.c'
    if dst.exists(): dst.unlink()
    shutil.copy2(ROOT / 'recomp/kernel/xk_native_70110.c', dst)
    print(f'{dst}: copied', flush=True)
    mk = (ROOT / 'Makefile').read_text()
    i = mk.index('# Native material setup f_00070110')
    j = mk.index('$(RECOMP_BUILD)/kernel/xk_native_70110.o:'); j = mk.index('\n', j) + 1
    edit(stage / 'Makefile', ['$(RECOMP_BUILD)/kernel/xk_native_1721b0.o: RECOMP_CFLAGS += -ffp-contract=off\n',
                              '$(RECOMP_BUILD)/kernel/xk_native_4b9d0.o: RECOMP_CFLAGS += -ffp-contract=off\n',
                              '$(RECOMP_BUILD)/kernel/xk_native_92330.o: RECOMP_CFLAGS += -ffp-contract=off\n'], mk[i:j],
         'XV_NATIVE_70110 ?=')
    edit(stage / 'games/halo_ce_3925/runtime.mk', ['ifeq ($(XV_NATIVE_1721B0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_1721b0.c\nendif\n',
                                                   'ifeq ($(XV_NATIVE_4B9D0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_4b9d0.c\nendif\n',
                                                   'ifeq ($(XV_NATIVE_92330),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_92330.c\nendif\n'],
         'ifeq ($(XV_NATIVE_70110),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_70110.c\nendif\n', 'xk_native_70110.c')
    edit(stage / 'recomp/kernel/xd3d.c',
         ['{ extern void xv_native_1721b0_report(unsigned) __attribute__((weak)); if (xv_native_1721b0_report) xv_native_1721b0_report(60); }',
          '{ extern void xv_native_4b9d0_report(unsigned) __attribute__((weak)); if (xv_native_4b9d0_report) xv_native_4b9d0_report(60); }',
          '{ extern void xv_native_92330_report(unsigned) __attribute__((weak)); if (xv_native_92330_report) xv_native_92330_report(60); }'],
         ' { extern void xv_native_70110_report(unsigned) __attribute__((weak)); if (xv_native_70110_report) xv_native_70110_report(60); }',
         'xv_native_70110_report')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_70110_hooks.py'), str(stage / 'recomp')], check=True)
if __name__ == '__main__': main()
