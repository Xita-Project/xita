/* XV_OBJTRACE=1 (diagnostic): which objects reach the per-object render entry f_0005B4A0 in each scene, and which
 * vanish for exactly one scene (present in scenes N-1 and N+1, absent in N) - the overlap flicker's signature
 * (Vita perf177/178 XV_FRAME_DRAWS: one object's 17-23 draws missing for a frame ~0.3 times a second, only with the
 * overlap). An object missing here was never collected; one present here but missing on screen was skipped inside
 * 5B4A0 (its 5A9A0 / 0x40000 / +C8 checks). Hooks: tools/patch_objtrace.py (5B4A0 entry); scene bounds from
 * xk_scene_thread.c helper_main. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OT_MAX 512
static uint32_t ot_set[3][OT_MAX]; static unsigned ot_n[3], ot_cur;   /* ring of the last three scenes */
static unsigned ot_scenes, ot_gaps, ot_logged; static int ot_on = -1;
static struct { uint32_t handle, tag; unsigned n; } ot_top[32]; static unsigned ot_ntop;

static int on(void) { if (ot_on < 0) { const char *e = getenv("XV_OBJTRACE"); ot_on = e && atoi(e); if (ot_on) XK_LOG("[objtrace] on\n"); } return ot_on; }
void xv_objtrace_note(uint32_t handle)   /* helper, inside the scene */
{
    if (!on()) return;
    unsigned c = ot_cur, n = ot_n[c];
    for (unsigned i = 0; i < n; ++i) if (ot_set[c][i] == handle) return;
    if (n < OT_MAX) { ot_set[c][n] = handle; ot_n[c] = n + 1; }
}
static int has(unsigned s, uint32_t h) { for (unsigned i = 0; i < ot_n[s]; ++i) if (ot_set[s][i] == h) return 1; return 0; }
void xv_objtrace_scene_begin(void)
{
    if (!on()) return;
    ot_scenes++;
    if (ot_scenes >= 3) {   /* scenes: a = cur-2, b = cur-1 (the one tested), c = cur (just finished) */
        unsigned c = ot_cur, b = (c + 2) % 3, a = (c + 1) % 3;
        for (unsigned i = 0; i < ot_n[a]; ++i) { uint32_t h = ot_set[a][i];
            if (has(c, h) && !has(b, h)) {
                ot_gaps++;
                uint32_t tbl = X_IMG32(0x2FC6ACu), elems = X_M32(tbl + 0x34u), e = elems + (h & 0xFFFFu) * 12u, body = X_M32(e + 8u), tag = body ? X_M32(body) : 0;
                unsigned k; for (k = 0; k < ot_ntop; ++k) if (ot_top[k].handle == h) { ot_top[k].n++; break; }
                if (k == ot_ntop && ot_ntop < 32) { ot_top[ot_ntop].handle = h; ot_top[ot_ntop].tag = tag; ot_top[ot_ntop].n = 1; ot_ntop++; }
                if (ot_logged < 40) { ot_logged++; XK_LOG("[objtrace] scene %u: object %08X (tag %08X, type word %08X) not collected; present in the scenes before and after (%u / %u / %u objects)\n",
                    ot_scenes - 1, h, tag, X_M32(e + 4u), ot_n[a], ot_n[b], ot_n[c]); }
            } }
    }
    ot_cur = (ot_cur + 1) % 3; ot_n[ot_cur] = 0;
}
void xv_objtrace_report(unsigned frames)
{
    if (!on() || !ot_scenes) return;
    char line[600]; int ln = snprintf(line, sizeof line, "[objtrace] %u frames: %u scenes, %u one-scene gaps so far; by object (handle tag n):", frames, ot_scenes, ot_gaps);
    for (unsigned k = 0; k < ot_ntop && ln < 540; ++k) ln += snprintf(line + ln, sizeof line - ln, " %08X %08X %u", ot_top[k].handle, ot_top[k].tag, ot_top[k].n);
    XK_LOG("%s\n", line);
}
