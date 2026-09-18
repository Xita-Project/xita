"""Resolve two bounded guest spans for audited clipping distance tests.

No addresses or input values survive this straight-line interval. The original
float conversions, double arithmetic and operand order remain. Cross-page
inputs take the unchanged mapped-load sequence.
"""
import hashlib

FLAG = 'XV_CLIP_DISTANCE_SPANS'
INTERVALS = (
    ('000B7227', '000B723F', 2, 'r0', 'r6', 1,
     '27e7ecf3a99d59ea7b43c100c63ecc459165c9c98cf3e541c8d787aeb326ef9a'),
    ('000B7280', '000B728F', 1, 'r7', 'r6', 1,
     'a35a7ba2919c2e226144cb3e3571cce19206b66a9e42301aaa8a3763f577dcf0'),
    ('000B72FC', '000B731A', 2, 'r7', 'r0', 2,
     'bc339f8aa109949e734cefaeb06dc904fabfa6177ca1a4a0780194a26486b450'),
)


def transform(text):
    for spec in INTERVALS:
        text = interval(text, *spec)
    header = ('#ifndef ' + FLAG + '\n#define ' + FLAG + ' 0\n#endif\n'
              '#if ' + FLAG + ' != 0 && ' + FLAG + ' != 1\n'
              '#error "' + FLAG + ' must be 0 or 1"\n#endif\n')
    return header + text


def interval(text, begin, end, copies, point, plane, plane_uses, pin):
    marker = '    /* ' + begin + ' '
    if text.count(marker) != copies:
        raise ValueError('ambiguous clipping distance interval')
    first = text.index(marker)
    last = text.index('    /* ' + end + ' ', first)
    original = text[first:last]
    if hashlib.sha256(original.encode()).hexdigest() != pin:
        raise ValueError('unqualified clipping distance interval')
    if text.count(original) != copies:
        raise ValueError('clipping distance copies differ')
    changed = original
    loads = [(point+'+0x4u', 'point[1]', 1), (plane+'+0x4u', 'plane[1]', plane_uses),
             (plane, 'plane[0]', plane_uses), (point, 'point[0]', 1), (plane+'+0x8u', 'plane[2]', 1)]
    for address, field, count in loads:
        old = 'x87_load_f32(c, (' + address + '))'
        if changed.count(old) != count:
            raise ValueError('clipping load shape changed')
        changed = changed.replace(old, '(double)cd_' + field)
    native = ('#if ' + FLAG + '\n'
              '    if (('+point+' & 4095u) <= 4088u && ('+plane+' & 4095u) <= 4084u) {\n'
              '        const xf32_u *cd_point = X_G('+point+');\n'
              '        const xf32_u *cd_plane = X_G('+plane+');\n' + changed +
              '    } else\n#endif\n    {\n' + original + '    }\n')
    return text.replace(original, native)
