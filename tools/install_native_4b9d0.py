#!/usr/bin/env python3
"""Install the natives under f_0004B9D0 (XV_NATIVE_4B9D0: the BSP sphere query and the solver's feature test) into a
retained stage (idempotent).
Usage: install_native_4b9d0.py <stage>          (the directory holding Makefile, games/, recomp/)

Copies recomp/kernel/xk_native_4b9d0.c from this checkout, adds the XV_NATIVE_4B9D0 Makefile block after the stage's
XV_NATIVE_92330 -ffp-contract rule (or, in a stage without it, XV_NATIVE_VISIBILITY's), the runtime.mk source line and
the weak report call in recomp/kernel/xd3d.c next to the same native's, and the hooks in recomp/kernel/xk_query_reuse.c
and recomp/solver_fusion.c (tools/patch_native_4b9d0_hooks.py). Build with XV_NATIVE_4B9D0=1 in the make vars; see
docs/native-4b9d0.md."""
import shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def edit(path, anchors, text, marker):
    """Insert text after the first of the anchors found exactly once (the stage's newest native integration)."""
    s = path.read_text()
    if marker in s: print(f'{path}: already present', flush=True); return
    for anchor in anchors:
        if s.count(anchor) == 1:
            path.write_text(s.replace(anchor, anchor + text)); print(f'{path}: updated (after {anchor.strip()[:60]!r})', flush=True)
            return
    sys.exit(f'{path}: no anchor found exactly once: {[a.strip()[:60] for a in anchors]}')
def main():
    stage = Path(sys.argv[1])
    shutil.copy2(ROOT / 'recomp/kernel/xk_native_4b9d0.c', stage / 'recomp/kernel/xk_native_4b9d0.c')
    print(f'{stage}/recomp/kernel/xk_native_4b9d0.c: copied', flush=True)
    mk = (ROOT / 'Makefile').read_text()
    i = mk.index('# Native BSP sphere query under f_0004B9D0')
    j = mk.index('$(RECOMP_BUILD)/kernel/xk_native_4b9d0.o:'); j = mk.index('\n', j) + 1
    # anchors: the 92330 integration (overlap-candidate/build-x87), else the visibility one (overlap-candidate/build)
    edit(stage / 'Makefile', ['$(RECOMP_BUILD)/kernel/xk_native_92330.o: RECOMP_CFLAGS += -ffp-contract=off\n',
                              '$(RECOMP_BUILD)/kernel/xk_native_visibility.o: RECOMP_CFLAGS += -ffp-contract=off\n'], mk[i:j],
         'XV_NATIVE_4B9D0 ?=')
    edit(stage / 'games/halo_ce_3925/runtime.mk', ['ifeq ($(XV_NATIVE_92330),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_92330.c\nendif\n',
                                                   'ifeq ($(XV_NATIVE_VISIBILITY),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_visibility.c\nendif\n'],
         'ifeq ($(XV_NATIVE_4B9D0),1)\nXITA_GAME_SRCS += recomp/kernel/xk_native_4b9d0.c\nendif\n', 'xk_native_4b9d0.c')
    edit(stage / 'recomp/kernel/xd3d.c',
         ['{ extern void xv_native_92330_report(unsigned) __attribute__((weak)); if (xv_native_92330_report) xv_native_92330_report(60); } ',
          '{ extern void xv_native_visibility_report(unsigned) __attribute__((weak)); if (xv_native_visibility_report) xv_native_visibility_report(60); } '],
         '{ extern void xv_native_4b9d0_report(unsigned) __attribute__((weak)); if (xv_native_4b9d0_report) xv_native_4b9d0_report(60); } ',
         'xv_native_4b9d0_report')
    subprocess.run([sys.executable, str(ROOT / 'tools/patch_native_4b9d0_hooks.py'), str(stage / 'recomp')], check=True)
if __name__ == '__main__': main()
