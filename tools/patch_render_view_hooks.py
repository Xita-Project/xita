#!/usr/bin/env python3
"""Selectively install the render-view hooks into an existing recomp/ stage (no whole-game regeneration).

The stage's code_*.c shards are hand-maintained (owner-phase, scene-partition, bucket cuts were installed
by selective tools), so a whole regeneration is not possible. This applies the two textual changes that
recompiler/xita_recomp.py (9848f30) and games/halo_ce_3925/hooks.py (7a04751) would emit:
  1. every shard preamble maps g_xpt/g_img_base to X_PT/X_IMG_BASE under XV_THREAD_PAGE_TABLE, so the
     per-function caches read the thread's page table and image base;
  2. f_000BCB30 (scene half) gets the XV_RENDER_VIEW entry scope, before the caches like the other
     observers, so the function body hash checks of the profile tools are unaffected.
Idempotent; instructions are never touched.  Usage: patch_render_view_hooks.py <stage>/recomp
"""
import re, sys
from pathlib import Path

PREAMBLE_ANCHOR = "#define X_IMG32(a) (*(xu32_u  *)(imgb_ + (uint32_t)(a)))\n"
PREAMBLE = """/* Under XV_THREAD_PAGE_TABLE the per-function caches read the thread's table and image base
 * (xv_x86rt.h X_PT / X_IMG_BASE); function bodies are unchanged so profile body hashes hold. */
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
#define g_xpt X_PT
#define g_img_base X_IMG_BASE
#endif
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
/* Render view: image globals translate through the page table (only listed image pages shadowed). */
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#define X_IMG8(a)  (*(uint8_t *)X_G(a))
#define X_IMG16(a) (*(xu16_u  *)X_G(a))
#define X_IMG32(a) (*(xu32_u  *)X_G(a))
#endif
"""
SCENE_HOOK = """#if XV_SCENE_THREAD
    { extern int xv_scene_thread_run(void *); if (xv_scene_thread_run(c)) return; }   /* XV_SCENE_THREAD: body ran on the helper */
#endif
"""
HOOK = """#if XV_RENDER_VIEW
    /* XV_RENDER_VIEW_SCOPE: scene half on the render page table */
    extern int xv_render_view_enabled;
    extern void xv_render_view_enter(unsigned *, void *);
    extern void xv_render_view_leave(unsigned *);
    unsigned xv_render_view_scope_ __attribute__((cleanup(xv_render_view_leave))) = 0;
    if (xv_render_view_enabled) xv_render_view_enter(&xv_render_view_scope_, c);
#endif
"""

def main():
    root = Path(sys.argv[1])
    shards = sorted(root.glob("code_*.c"))
    if not shards: raise SystemExit("no shards in " + str(root))
    preambles = hooks = 0
    for shard in shards:
        text = shard.read_text()
        if "#define g_xpt X_PT" not in text or "Render view: image globals" not in text:
            if text.count(PREAMBLE_ANCHOR) != 1: raise SystemExit(f"{shard}: preamble anchor drift")
            old_block = text[text.index(PREAMBLE_ANCHOR) + len(PREAMBLE_ANCHOR):]
            if old_block.startswith("/* Under XV_THREAD_PAGE_TABLE"):   # replace an older preamble
                end = old_block.index("#endif\n") + len("#endif\n"); text = text.replace(old_block[:end], "", 1)
            text = text.replace(PREAMBLE_ANCHOR, PREAMBLE_ANCHOR + PREAMBLE, 1); preambles += 1
        entry = "void f_000BCB30(xctx *restrict c)\n{\n"
        if entry in text and "XV_RENDER_VIEW_SCOPE" not in text:
            if text.count(entry) != 1: raise SystemExit("BCB30 entry drift")
            text = text.replace(entry, entry + HOOK, 1); hooks += 1
        if entry in text and "XV_SCENE_THREAD" not in text:
            text = text.replace(entry, entry + SCENE_HOOK, 1); hooks += 1
        shard.write_text(text)
    if not any("XV_RENDER_VIEW_SCOPE" in s.read_text() for s in shards): raise SystemExit("f_000BCB30 not found")
    print(f"patched: {preambles} preambles, {hooks} BCB30 hooks ({len(shards)} shards)")

if __name__ == "__main__": main()
