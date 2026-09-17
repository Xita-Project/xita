"""Optional native body for the 0xB0CB0 segment/sphere test at its own entry."""
import hashlib

SPAN = (0xB0CB0, 0x113, 'f6e24841f75f45ab7e32dc7da2c7d60fa8736ca1a37eb063e167336b2c919041')
ENTRY = ['#ifdef XV_NATIVE_SEGMENT_SPHERE',
         '    { uint32_t ss_resume_ = xv_segment_sphere(c, xram_, xpt_);',
         '      if (ss_resume_ == 1u) return;',
         '      if (ss_resume_ == 2u) goto L_000B0CFC; }',
         '#endif']


def matches(image):
    address, size, digest = SPAN
    return hashlib.sha256(image.bytes_at(address, size) or b'').hexdigest() == digest


def hook(body):
    """Place the header before the function; ENTRY is emitted at its entry."""
    assert body.count('L_000B0CFC:') == 1 and body.count('X_PREEMPT()') == 1, 'segment/sphere drift'
    assert body.count('\n'.join(ENTRY)) == 1, 'segment/sphere entry hook missing'
    return ('#ifdef XV_NATIVE_SEGMENT_SPHERE\n#include "kernel/xk_segment_sphere.h"\n#endif\n' + body)
