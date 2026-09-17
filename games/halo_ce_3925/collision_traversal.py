"""Owned-image-qualified native GPR lifetime for the two collision BSP walks.

No guest instructions or game data live here. Generate the candidate from the
audited emitter body, retaining every FP operation, guest write and branch.
Only GPR storage between observable calls/yields changes. Unknown calls fail
generation, rather than silently executing against unpublished context.
"""
import hashlib
import re

SPANS = {
    0x87E10: (130, 'f68295756aa6cae3b590a9c11c376ec01d9fb68d64e144c8b491b901a36fa620'),
    0x87EA0: (612, '2cbba93bbbd09d18a167e2658d435583bc2e9cfafef4acfd8c9ab728f9835d56'),
}


def matches(image):
    return all(hashlib.sha256(image.bytes_at(a, n) or b'').hexdigest() == h
               for a, (n, h) in SPANS.items())


def hook(address, body):
    if not __debug__:
        raise ValueError('Assertions required for native traversal generation')
    assert address in SPANS
    label = f'L_{address:08X}:\n'
    assert body.count(label) == 1 and body.rstrip().endswith('}')
    # All callees receiving c are either pure runtime arithmetic or explicit
    # observation boundaries. Extend this allowlist only after auditing them.
    calls = set(re.findall(r'\b(\w+)\(c(?:\s*[,)]|\s*\))', body))
    allowed = {'x87_push', 'x87_pop', 'x87_load_f32', 'x87_store_f32',
               'x87_compare', 'x_shl32', 'XF_S', 'XF_O', 'XF_Z', 'XF_C', 'XF_P',
               'f_00087EA0', 'f_00087E10', 'f_00086F50',
               'xv_bsp_sphere_plane_distance'}
    assert calls <= allowed, (address, calls - allowed)
    region = body[body.index(label):body.rfind('}')]
    assert region.count('X_PREEMPT()') == (1 if address == 0x87E10 else 4)
    native = re.sub(r'c->r\[(\d)\]', r'ct_r\1', region)
    for callee in ('f_00087EA0', 'f_00087E10', 'f_00086F50'):
        native = native.replace(callee + '(c);',
                                'CT_SAVE(); ' + callee + '(c); CT_LOAD();')
    # xk_geometry.c's pure distance helper reads only EAX/ECX/EBP and writes
    # only EAX plus x87 state. It has no scheduler, lock or observer callback.
    # Guest child calls still receive all eight published GPRs.
    native = native.replace('xv_bsp_sphere_plane_distance(c)',
                            '({ c->r[0]=ct_r0; c->r[1]=ct_r1; c->r[5]=ct_r5; '
                            'int ct_ok = xv_bsp_sphere_plane_distance(c); ct_r0=c->r[0]; ct_ok; })')
    native = native.replace('return;', 'CT_SAVE(); return;')
    roots = ('    uint8_t *const xram_ = ct_arena; const uint32_t *const xpt_ = ct_pages;\n'
             '    uint8_t *const imgb_ = ct_image; (void)imgb_;\n'
             '    uint32_t '+','.join(f'ct_r{i}' for i in range(8))+'; CT_LOAD();\n')
    macros = {
        'X_R16(i)': '(*(uint16_t *)&CT_REG(i))',
        'X_R8L(i)': '(*(uint8_t *)&CT_REG(i))',
        'X_R8H(i)': '(*((uint8_t *)&CT_REG(i) + 1))',
        'X_PUSH32(v)': 'do { uint32_t ct_v = (uint32_t)(v); ct_r4 -= 4; X_M32(ct_r4) = ct_v; } while (0)',
        # x_pop32 was compiled against global mapping roots in x86rt.h, unlike
        # the generated X_PUSH32 macro. Preserve that distinction after yields.
        'X_POP32()': '({ uint32_t ct_v = *(xu32_u *)(g_xram+g_xpt[ct_r4>>12]+(ct_r4&4095u)); ct_r4 += 4; ct_v; })',
        'X_PREEMPT()': 'do { if (--c->preempt <= 0) { CT_SAVE(); xv_preempt(c); CT_LOAD(); } } while (0)',
    }
    pre = '#ifdef XV_NATIVE_COLLISION_TRAVERSAL\n#include "kernel/xk_collision_traversal.h"\n'
    pre += '#define CT_REG(i) ct_r##i\n'
    pre += '#define CT_SAVE() do { '+''.join(f'c->r[{i}]=ct_r{i};' for i in range(8))+' } while (0)\n'
    pre += '#define CT_LOAD() do { '+''.join(f'ct_r{i}=c->r[{i}];' for i in range(8))+' } while (0)\n'
    for macro, value in macros.items():
        name = macro.split('(')[0]
        pre += f'#pragma push_macro("{name}")\n#undef {name}\n#define {macro} {value}\n'
    name = f'xv_collision_traversal_{address:08X}'
    pre += f'static inline __attribute__((always_inline)) void {name}(xctx *restrict c, uint8_t *ct_arena, const uint32_t *ct_pages, uint8_t *ct_image)\n{{\n' + roots + native + '}\n'
    for macro in reversed(macros):
        pre += f'#pragma pop_macro("{macro.split("(")[0]}")\n'
    pre += '#undef CT_SAVE\n#undef CT_LOAD\n#undef CT_REG\n#endif\n'
    dispatch = ('#ifdef XV_NATIVE_COLLISION_TRAVERSAL\n'
                f'    if (xv_collision_traversal_mode) {{ {name}(c, xram_, xpt_, imgb_); return; }}\n'
                '#endif\n')
    return pre + body.replace(label, dispatch + label, 1)
