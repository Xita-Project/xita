"""Exact-input fog reuse; only the primary 70110 entry can receive hooks."""
import hashlib
import re
from recompiler.halo_flare_hooks import matches_image

ENTRY = 0x70110
BODY_SHA256 = "3cd82eb01831e928756569d16975ea7d065a630e21defac7baad2b85713f3900"
REGION_SHA256 = "b086cb4dd27f46793d61357df55191f718531709c00c107c4568d4dc35b27f0e"
PREFIX = "#if defined(XV_MODEL_FOG) && XV_MODEL_FOG\n"


def strip(body):
    return re.sub(r'^#if defined\(XV_MODEL_FOG\) && XV_MODEL_FOG\n.*?^#endif\n',
                  '', body, flags=re.M | re.S)


def hook(image, address, body):
    if address != ENTRY or not matches_image(image):
        return body
    if hashlib.sha256(image.bytes_at(0x70A42, 0x2C7) or b'').hexdigest() != REGION_SHA256:
        raise ValueError('primary 70110 fog instruction bytes drift')
    # Production symbols turn direct guest calls into HLE calls. Their mapping
    # is outside the fog region, which calls only three retained pure helpers.
    canonical = re.sub(r'^    XV_PHASE_SCOPE\(c, \d+u\);\n', '', body, flags=re.M)
    canonical = re.sub(r'XV_HLE_CALL\(0x([0-9A-F]+)u, \w+\);',
                       lambda m: f'f_{int(m[1], 16):08X}(c);', canonical).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != BODY_SHA256:
        raise ValueError('primary 70110 emitted body drift')
    original = body
    header = 'void f_00070110(xctx *restrict c)\n{\n'
    if not body.startswith(header):
        raise ValueError('primary 70110 header drift')
    declarations = (PREFIX + '    /* XV_MODEL_FOG_SCOPE: primary 70110 only */\n'
                    '    extern int xk_model_fog_begin(xctx *, const uint8_t *, const uint32_t *, const uint8_t *, unsigned *);\n'
                    '    extern void xk_model_fog_end(xctx *, unsigned);\n'
                    '    unsigned xv_model_fog_token_ = 0;\n#endif\n')
    body = body.replace(header, header + declarations, 1)
    for pc, lines in (
        (0x70A42, '    if (xk_model_fog_begin(c, xram_, xpt_, imgb_, &xv_model_fog_token_)) goto L_00070D12;\n'),
        (0x70D07, '    xk_model_fog_end(c, xv_model_fog_token_);\n')):
        needle = f'    /* {pc:08X}  '
        if body.count(needle) != 1:
            raise ValueError('primary 70110 fog frontier drift')
        body = body.replace(needle, PREFIX + lines + '#endif\n' + needle, 1)
    if strip(body) != original:
        raise ValueError('fog hook changed original instructions')
    return body
