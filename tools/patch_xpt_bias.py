#!/usr/bin/env python3
"""Page-table host-bias cache for generated code (stage patch; exact-match, refuses drift).

Every guest page table (the live table in xk_mem.c and the render-view table) keeps its arena-offset entries
unchanged and gains a parallel array right after them:

    bias[vp] = (uintptr_t)g_xram + entries[vp] - (vp << 12)

so a guest address translates as `bias[a >> 12] + a` (one load, no mask/add) instead of
`g_xram + entries[a >> 12] + (a & 0xFFF)`. Only the generated shards use it (their per-file X_G override);
natives keep the entries. Every entry write goes through xk_pt_set(), and the whole bias array is rebuilt
when the arena is bound and when the render table is copied from the live table. XV_BIAS_CHECK=1 (host)
verifies both tables at every Present.

usage: patch_xpt_bias.py <stage-root>   (the directory holding recomp/ and runtime/)
"""
import re, sys
from pathlib import Path

S = Path(sys.argv[1])
MARK = 'xk_pt_set'


def edit(path, pairs):
    text = path.read_text()
    if MARK in text and path.name != 'xd3d.c':
        raise SystemExit(f'{path} already patched')
    for old, new in pairs:
        if text.count(old) != 1:
            raise SystemExit(f'{path}: expected exactly one match for:\n{old}')
        text = text.replace(old, new)
    path.write_text(text)


# ---- xk_mem.c -------------------------------------------------------------------------------------------
edit(S / 'recomp/kernel/xk_mem.c', [
    ('static struct { uint8_t *img_base; uint32_t entries[1u << 20]; } g_xpt_block;',
     'static struct { uint8_t *img_base; uint32_t entries[1u << 20]; uintptr_t bias[1u << 20]; } g_xpt_block;\n'
     '/* Generated code translates through bias[vp] = g_xram + entries[vp] - (vp << 12), stored right after the entries of\n'
     ' * every page table (tools/patch_xpt_bias.py). All entry writes go through xk_pt_set. */\n'
     'extern uint8_t *g_xram;\n'
     'void xk_pt_set(uint32_t *table, uint32_t vp, uint32_t off)\n'
     '{ table[vp] = off; ((uintptr_t *)(table + (1u << 20)))[vp] = (uintptr_t)g_xram + off - (vp << 12); }\n'
     'void xk_pt_rebias(uint32_t *table)\n'
     '{ uintptr_t *b = (uintptr_t *)(table + (1u << 20)); for (uint32_t vp = 0; vp < (1u << 20); ++vp) b[vp] = (uintptr_t)g_xram + table[vp] - (vp << 12); }\n'
     'int xk_pt_check(const uint32_t *table)\n'
     '{ const uintptr_t *b = (const uintptr_t *)(table + (1u << 20)); for (uint32_t vp = 0; vp < (1u << 20); ++vp) if (b[vp] != (uintptr_t)g_xram + table[vp] - (vp << 12)) return (int)vp + 1; return 0; }'),
    ('    g_xpt[vp] = arena_off;\n    if (xv_render_view_mirror) xv_render_view_mirror(vp, arena_off);',
     '    xk_pt_set(g_xpt, vp, arena_off);\n    if (xv_render_view_mirror) xv_render_view_mirror(vp, arena_off);'),
    ('    for (uint32_t p = 0; p < (1u << 20); ++p) g_xpt[p] = g_trash_off;',
     '    for (uint32_t p = 0; p < (1u << 20); ++p) xk_pt_set(g_xpt, p, g_trash_off);'),
    ('    g_xpt[vp] = g_trash_off;\n    if (xv_render_view_mirror) xv_render_view_mirror(vp, g_trash_off);',
     '    xk_pt_set(g_xpt, vp, g_trash_off);\n    if (xv_render_view_mirror) xv_render_view_mirror(vp, g_trash_off);'),
    ('{ g_xpt[g_guards[i].vpage] = g_guards[i].off; if (xv_render_view_mirror)',
     '{ xk_pt_set(g_xpt, g_guards[i].vpage, g_guards[i].off); if (xv_render_view_mirror)'),
    ('    g_img_base = g_xram + XRAM_SIZE - g_image_lo;\n    g_xpt_block.img_base = g_img_base;',
     '    g_img_base = g_xram + XRAM_SIZE - g_image_lo;\n    g_xpt_block.img_base = g_img_base;\n'
     '    xk_pt_rebias(g_xpt);   /* bias entries written before the arena existed used g_xram == NULL */'),
])
mem = (S / 'recomp/kernel/xk_mem.c').read_text()
if re.search(r'g_xpt\[[^]]+\]\s*=[^=]', mem):
    raise SystemExit('xk_mem.c: unpatched g_xpt entry write remains')

# ---- xk_render_view.c -----------------------------------------------------------------------------------
edit(S / 'recomp/kernel/xk_render_view.c', [
    ('static int thread_mode; static struct { uint8_t *img_base; uint32_t entries[1u << 20]; } *rt;',
     'static int thread_mode; static struct { uint8_t *img_base; uint32_t entries[1u << 20]; uintptr_t bias[1u << 20]; } *rt;\n'
     'void xk_pt_set(uint32_t *table, uint32_t vp, uint32_t off); void xk_pt_rebias(uint32_t *table);'),
    ('    rt->entries[vpage] = arena_off;', '    xk_pt_set(rt->entries, vpage, arena_off);'),
    ('SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, 5u << 20, NULL);',
     'SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, 9u << 20, NULL);   /* entries + bias (patch_xpt_bias.py) */'),
    ('memcpy(rt->entries, g_xpt, sizeof rt->entries); rt->img_base',
     'memcpy(rt->entries, g_xpt, sizeof rt->entries); xk_pt_rebias(rt->entries); rt->img_base'),
    ('retargets[retargets_n++] = (retarget_t){ vps[i], live, view }; VIEW_TABLE[vps[i]] = view;',
     'retargets[retargets_n++] = (retarget_t){ vps[i], live, view }; xk_pt_set(VIEW_TABLE, vps[i], view);'),
    ('if (VIEW_TABLE[vp] == live) { VIEW_TABLE[vp] = L.image_copy_off + ip * XK_PAGE; image_entries++; }',
     'if (VIEW_TABLE[vp] == live) { xk_pt_set(VIEW_TABLE, vp, L.image_copy_off + ip * XK_PAGE); image_entries++; }'),
    ('if (VIEW_TABLE[vp] == view) VIEW_TABLE[vp] = thread_mode ? g_xpt[vp] : L.image_off + ip * XK_PAGE;',
     'if (VIEW_TABLE[vp] == view) xk_pt_set(VIEW_TABLE, vp, thread_mode ? g_xpt[vp] : L.image_off + ip * XK_PAGE);'),
    ('if (VIEW_TABLE[retargets[i].vp] == retargets[i].view) VIEW_TABLE[retargets[i].vp] = thread_mode ? g_xpt[retargets[i].vp] : retargets[i].live;',
     'if (VIEW_TABLE[retargets[i].vp] == retargets[i].view) xk_pt_set(VIEW_TABLE, retargets[i].vp, thread_mode ? g_xpt[retargets[i].vp] : retargets[i].live);'),
])
rv = (S / 'recomp/kernel/xk_render_view.c').read_text()
if re.search(r'(VIEW_TABLE|rt->entries)\[[^]]+\]\s*=[^=]', rv):
    raise SystemExit('xk_render_view.c: unpatched entry write remains')

# ---- generated shards: X_G through the bias array -------------------------------------------------------
OLD_XG = '#define X_G(a) ((void *)(xram_ + xpt_[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu)))'
NEW_XG = ('#define X_G(a) ((void *)(((const uintptr_t *)(xpt_ + (1u << 20)))[(uint32_t)(a) >> 12] + (uint32_t)(a)))'
          '   /* bias[vp] = xram + entries[vp] - (vp << 12): tools/patch_xpt_bias.py */')
n = 0
for shard in sorted((S / 'recomp').glob('code_0*.c')):
    t = shard.read_text()
    if t.count(OLD_XG) != 1:
        raise SystemExit(f'{shard}: X_G override not found exactly once')
    shard.write_text(t.replace(OLD_XG, NEW_XG)); n += 1
print(f'patched xk_mem.c, xk_render_view.c and {n} shards')
