#!/usr/bin/env python3
"""Install the native effects/material helpers (XV_NATIVE_EFFECTS: 7E530, 7E420 + 7DBE0, 56F20, 80360 + 1290E0 + 80250,
11B60, 11610, 11BD0) into a retained stage (idempotent).
Usage: install_native_effects.py <stage>          (the directory holding Makefile, games/, recomp/)

Copies recomp/kernel/xk_native_effects.c from this checkout, adds the XV_NATIVE_EFFECTS Makefile block after the stage's
newest native -ffp-contract rule (1721B0, 4B9D0, 92330 or visibility), the runtime.mk source line and the weak report
call in recomp/kernel/xd3d.c, and the entry hooks (tools/patch_native_effects_hooks.py). Build with XV_NATIVE_EFFECTS=1
in the make vars; see docs/native-effects.md."""
import shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def edit(path, anchors, text, marker):
    s = path.read_text()
    if marker in s: print(f'{path}: already present', flush=True); return
    for anchor in anchors:
        if s.count(anchor) == 1:
            path.write_text(s.replace(anchor, anchor + text)); print(f'{path}: updated (after {anchor.strip()[:60]!r})', flush=True)
            return
    sys.exit(f'{path}: no anchor found exactly once: {[a.strip()[:60] for a in anchors]}')
def main():
    stage = Path(sys.argv[1])
    shutil.copy2(ROOT / 'recomp/kernel/xk_native_effects.c', stage / 'recomp/kernel/xk_native_effects.c')
    print(f'{stage}/recomp/kernel/xk_native_effects.c: copied', flush=True)
    mk = (ROOT / 'Makefile').read_text()
    i = mk.index('# Native effects and material helpers (XV_NATIVE_EFFECTS)')
    j = mk.index('$(RECOMP_BUILD)/kernel/xk_native_effects.o:'); j = mk.index('\n', j) + 1
    edit(stage / 'Makefile', ['$(RECOMP_BUILD)/kernel/xk_native_1721b0.o: RECOMP_CFLAGS += -ffp-contract=off\n',
                              '$(RECOMP_BUILD)/kernel/xk_native_4b9d0.o: RECOMP_CFLAGS += -ffp-contract=off\n',
                              '$(RECOMP_BUILD)/kernel/xk_native_92330.o: RECOMP_CFLAGS += -ffp-contract=off\n',
                              '$(RECOMP_BUILD)/kernel/xk_native_visibility.o: RECOMP_CFLAGS += -ffp-contract=off\n'], mk[i:j],
         'XV_NATIVE_EFFECTS ?=')
    edit(stage / 'games/halo_ce_3925/runtime.mk', ['ifeq ($(XV_NATIVE_1721B0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_1721b0.c\nendif\n',
                                                   'ifeq ($(XV_NATIVE_4B9D0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_4b9d0.c\nendif\n',
                                                   'ifeq ($(XV_NATIVE_92330),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_92330.c\nendif\n'],
         'ifeq ($(XV_NATIVE_EFFECTS),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_effects.c\nendif\n', 'xk_native_effects.c')
    edit(stage / 'recomp/kernel/xd3d.c',
         ['{ extern void xv_native_1721b0_report(unsigned) __attribute__((weak)); if (xv_native_1721b0_report) xv_native_1721b0_report(60); } ',
          '{ extern void xv_native_4b9d0_report(unsigned) __attribute__((weak)); if (xv_native_4b9d0_report) xv_native_4b9d0_report(60); } '],
         '{ extern void xv_native_effects_report(unsigned) __attribute__((weak)); if (xv_native_effects_report) xv_native_effects_report(60); } ',
         'xv_native_effects_report')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_effects_hooks.py'), str(stage / 'recomp')], check=True)
if __name__ == '__main__': main()
