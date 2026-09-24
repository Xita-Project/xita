/* XV_SOUND_OBSTRUCTION=<frames>: reuse a looping sound's obstruction result while neither the sound nor its listener
 * moved, instead of casting the same collision ray again every frame.
 *
 * a30 (perf192, Sept 24 2026): the once-per-frame real-time update f_00108FD0 spent 30 of its 42 ms in the object
 * looping sound update 2BB70 -> 2B700 -> 27700 -> 26A50 -> f_0002B460, ~59 sounds per frame at ~300 us each.
 * f_0002B460(eax = listener index, ebx = sound, one stack argument = listener distance) checks the listener's and the
 * sound's clusters and, when they can see each other, casts a collision vector (f_001721B0, flags C0E1) from the
 * listener to the sound. Its only results are two fields of the sound, [ebx+38h] (obstruction) and [ebx+3Ch] (gain),
 * which feed the mixer; the game state never reads them back.
 *
 * With the knob on, the entry hook skips the whole call when the same sound was computed for the same listener within
 * the last <frames> frames and neither the sound position [ebx+0Ch..14h] nor the listener position (the listener
 * record at 271550h + index*29Ch) moved more than XV_SOUND_OBSTRUCTION_EPS world units (default 0.05). f_0002B460 is
 * the only writer of the two result fields and overwrites both on every call, so a skipped call leaves them exactly
 * as the last computation did; what can differ is a ray that would now hit something that moved in between (audio
 * only, at most <frames> frames late). Moving sources (a driven Warthog) and a moving listener still recompute every frame.
 * XV_SOUND_OBSTRUCTION_VERIFY=1 never skips: it counts how often a reuse would have left different values.
 * Default 0 (off). Entry hook from tools/patch_sound_obstruction_hook.py. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned xd3d_frame(void);

#define SLOTS 2048u
static struct entry {
    uint32_t sound, frame;
    int16_t listener; uint8_t used, pending;
    float s[3], l[3];
    uint32_t v38, v3c;               /* verify: the result fields a reuse would have kept */
} tab[SLOTS];
static int mode = -1, verify; static float eps2;
static unsigned n_calls, n_skipped, n_computed, n_moved, n_aged, n_new, n_verify_same, n_verify_diff;

static void config(void)
{
    const char *e = getenv("XV_SOUND_OBSTRUCTION"); mode = e ? atoi(e) : 0;
    if (mode < 0) mode = 0; if (mode > 60) mode = 60;
    e = getenv("XV_SOUND_OBSTRUCTION_VERIFY"); verify = e && atoi(e) != 0;
    e = getenv("XV_SOUND_OBSTRUCTION_EPS"); float eps = e ? (float)atof(e) : 0.05f; eps2 = eps * eps;
    if (mode) XK_LOG("[sound-obstruction] reuse up to %d frames while sound and listener move < %.3f%s\n", mode, sqrtf(eps2), verify ? " (verify: never skips)" : "");
}

static float f32(uint32_t a) { float v; uint32_t w = X_M32(a); memcpy(&v, &w, 4); return v; }
static float d2(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }

/* Entry hook of f_0002B460: nonzero = handled (the guest function returns at once; it is `ret 4` and its caller
 * 26A50 uses neither eax, the flags nor the x87 stack after the call). */
int xv_sound_obstruction(xctx *c)
{
    if (mode < 0) config();
    if (!mode) return 0;
    uint32_t sound = c->r[3]; int16_t li = (int16_t)c->r[0];
    if (li < 0 || li > 3) return 0;
    uint32_t lrec = 0x271550u + (uint32_t)li * 0x29Cu, frame = xd3d_frame();
    float s[3] = { f32(sound + 0x0Cu), f32(sound + 0x10u), f32(sound + 0x14u) };
    float l[3] = { f32(lrec), f32(lrec + 4u), f32(lrec + 8u) };
    struct entry *e = &tab[((sound >> 2) * 2654435761u) >> 21 & (SLOTS - 1u)];
    n_calls++;
    if (e->used && e->sound == sound && e->listener == li) {
        if (e->pending) {            /* verify: the guest ran instead of a reuse - did it leave the reused values? */
            e->pending = 0;
            if (X_M32(sound + 0x38u) == e->v38 && X_M32(sound + 0x3Cu) == e->v3c) n_verify_same++; else n_verify_diff++;
        }
        int fresh = frame - e->frame < (uint32_t)mode, still = d2(s, e->s) <= eps2 && d2(l, e->l) <= eps2;
        if (fresh && still) {
            if (verify) { e->pending = 1; e->v38 = X_M32(sound + 0x38u); e->v3c = X_M32(sound + 0x3Cu); n_computed++; return 0; }
            c->r[4] += 8u;           /* return address + the distance argument */
            n_skipped++; return 1;
        }
        if (!still) n_moved++; else n_aged++;
    } else n_new++;
    /* compute: remember the inputs; the guest body writes the results into the sound itself */
    e->used = 1; e->sound = sound; e->listener = li; e->frame = frame; e->pending = 0;
    memcpy(e->s, s, sizeof s); memcpy(e->l, l, sizeof l);
    n_computed++;
    return 0;
}

void xv_sound_obstruction_report(unsigned frames)
{
    if (mode <= 0 || !frames) return;
    char tail[80] = "";
    if (verify) snprintf(tail, sizeof tail, "; verify: a reuse would match %u differ %u", n_verify_same, n_verify_diff);
    XK_LOG("[sound-obstruction] %u frames: calls %u skipped %u computed %u (new %u moved %u aged %u)%s\n", frames, n_calls, n_skipped, n_computed, n_new, n_moved, n_aged, tail);
    n_calls = n_skipped = n_computed = n_moved = n_aged = n_new = n_verify_same = n_verify_diff = 0;
}
