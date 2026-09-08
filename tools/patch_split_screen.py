#!/usr/bin/env python3
"""Allow solo split-screen in the user's generated Halo 3925 host initializer.

Keep the original System Link minimum. This changes the lobby's own advertised
minimum, so its readiness checks, countdown, and normal start callback agree.
The source signature is checked before any files are changed; no game binary or
generated function body is distributed with this patch.
"""
from pathlib import Path
import sys

OLD = """    /* 0009C4C1  mov byte ptr [ebp+115h],2 */
    X_M8((c->r[5]+0x115u)) = 0x2u;"""
NEW = """    /* 0009C4C1: solo split-screen minimum; 2E3630 is System Link. */
    X_M8((c->r[5]+0x115u)) = X_IMG8(0x2E3630u) ? 2u : 1u;"""

STATUS_PATCHES = []
for address in ('000C7550', '000C8ED2'):
    old = f"""    /* {address}  cmp word ptr [eax+224h],2 */
    {{ uint16_t a_ = X_M16((c->r[0]+0x224u)), b_ = 0x2u; X_FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }}"""
    new = old.replace('cmp word ptr [eax+224h],2', 'compare lobby players with the solo/System Link minimum').replace('b_ = 0x2u;', 'b_ = X_IMG8(0x2E3630u) ? 2u : 1u;')
    STATUS_PATCHES.append((old, new, 1))

PAUSE_OLD = """    /* 000D0541  mov [esi+13h],al */
    X_M8((c->r[6]+0x13u)) = X_R8L(0);"""
PAUSE_NEW = """    /* 000D0541: keep the identified split-screen lobby background running.
     * Map preloading changes the last-file flag before these widgets exist. */
    { uint32_t tag_ = c->r[3]; uint8_t pause_ = X_R8L(0);
      if (tag_ == 0xE36801F2u || tag_ == 0xE36901F3u) {
          char name_[32]; x_guest_read(name_, c->r[5] + 4u, sizeof name_);
          const char *expected_ = tag_ == 0xE36801F2u ?
              "splitscreen_pregame_wrapper" : "splitscreen_pregame_screen";
          if (!strncmp(name_, expected_, sizeof name_)) pause_ = 0;
      }
      X_M8((c->r[6]+0x13u)) = pause_; }"""


def patch(directory):
    sources = [(p, p.read_text()) for p in Path(directory).glob('code_*.c')]
    replacements = [(OLD, NEW, 1), *STATUS_PATCHES, (PAUSE_OLD, PAUSE_NEW, 7)]
    # Check all generated signatures before changing any files. Already-applied
    # patches and the older initializer-only patch are both accepted.
    for old, new, expected in replacements:
        counts = (sum(s.count(old) for _, s in sources), sum(s.count(new) for _, s in sources))
        if counts not in ((expected, 0), (0, expected)):
            raise ValueError(f'Solo lobby patch needs Halo 3925 signature: {old.splitlines()[0]} ({counts})')
    for path, before in sources:
        after = before
        for old, new, _ in replacements:
            after = after.replace(old, new)
        if after != before:
            path.write_text(after)


if __name__ == '__main__':
    patch(sys.argv[1] if len(sys.argv) > 1 else 'recomp')
