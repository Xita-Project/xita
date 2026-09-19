"""Image/body-pinned model-scope UV replay; public 56F20 stays unchanged."""
import hashlib
import re
from recompiler.halo_flare_hooks import matches_image
from games.halo_ce_3925 import model_fog

PREFIX = '#if defined(XV_MODEL_UV) && XV_MODEL_UV\n'
BODY_SHA256 = {
    0x70110: '3cd82eb01831e928756569d16975ea7d065a630e21defac7baad2b85713f3900',
    0xA26B0: '975f6386ef06540c63fc5bc15b08e4681a0a1d2bda8687e05901b626c998da30',
}


def strip(body):
    return re.sub(r'^#if defined\(XV_MODEL_UV\) && XV_MODEL_UV\n.*?^#endif\n', '', body, flags=re.M | re.S)


def hook(image, address, body):
    if address not in BODY_SHA256 or not matches_image(image):
        return body
    canonical = model_fog.strip(body)
    canonical = re.sub(r'^    XV_PHASE_SCOPE\(c, \d+u\);\n', '', canonical, flags=re.M)
    canonical = re.sub(r'XV_HLE_CALL\(0x([0-9A-F]+)u, \w+\);', lambda m: f'f_{int(m[1],16):08X}(c);', canonical).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != BODY_SHA256[address]:
        raise ValueError(f'primary {address:X} UV emitted body drift')
    original = body
    header = f'void f_{address:08X}(xctx *restrict c)\n{{\n'
    if not body.startswith(header):
        raise ValueError('UV primary header drift')
    if address == 0xA26B0:
        decl = ('    /* XV_MODEL_UV_SCOPE: primary A26B0 to A2380 only */\n'
                '    extern unsigned xk_model_uv_scope_begin(xctx *, const uint8_t *, const uint32_t *, const uint8_t *);\n'
                '    extern void xk_model_uv_scope_end(xctx *, unsigned);\n'
                '    unsigned xv_model_uv_scope_ = 0;\n')
        needle = '    X_PUSH32(0xA2968u);\n    f_000A2380(c);\n'
        if body.count(needle) != 2:
            raise ValueError('UV model call frontier drift')
        replacement = ('    X_PUSH32(0xA2968u);\n' + PREFIX +
                       '    xv_model_uv_scope_ = xk_model_uv_scope_begin(c, xram_, xpt_, imgb_);\n#endif\n'
                       '    f_000A2380(c);\n' + PREFIX +
                       '    xk_model_uv_scope_end(c, xv_model_uv_scope_);\n#endif\n')
        body = body.replace(needle, replacement)
    else:
        decl = ('    /* XV_MODEL_UV_CALLS: primary 70110 two callsites only */\n'
                '    extern int xk_model_uv_begin(xctx *, const uint8_t *, const uint32_t *, const uint8_t *, unsigned, unsigned *);\n'
                '    extern void xk_model_uv_end(xctx *, unsigned);\n'
                '    unsigned xv_model_uv_token_ = 0;\n')
        for ret, site in ((0x70960, 0), (0x70E5C, 1)):
            needle = f'    X_PUSH32(0x{ret:X}u);\n    f_00056F20(c);\n'
            if body.count(needle) != 1:
                raise ValueError('UV material call frontier drift')
            replacement = (f'    X_PUSH32(0x{ret:X}u);\n' + PREFIX +
                           f'    if (!xk_model_uv_begin(c, xram_, xpt_, imgb_, {site}u, &xv_model_uv_token_)) {{\n#endif\n'
                           '    f_00056F20(c);\n' + PREFIX +
                           '    xk_model_uv_end(c, xv_model_uv_token_);\n    }\n#endif\n')
            body = body.replace(needle, replacement)
    body = body.replace(header, header + PREFIX + decl + '#endif\n', 1)
    if strip(body) != original:
        raise ValueError('UV hook changed retained original instructions')
    return body
