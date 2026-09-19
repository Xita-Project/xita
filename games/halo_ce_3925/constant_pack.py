"""Bounded native prefix; final translated iteration and HLE call remain."""
import hashlib
import re

BODY_SHA256 = "8833e4b6b0990ebe97b04dac136e52bf26d42c091c29132e1fb55c5612118a2e"
GUARD = "#ifdef XV_NATIVE_CONSTANT_PACK\n"
INSERT = GUARD + "    { extern int xv_constant_pack_prefix(xctx *); (void)xv_constant_pack_prefix(c); }\n#endif\n"

def hook(body):
    canonical = re.sub(r"^    XV_PHASE_SCOPE\(c, \d+u\);\n", "", body, flags=re.M).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != BODY_SHA256:
        raise ValueError("7E530 constant packing body drift")
    needle = "    /* 0007E540  mov edi,[esi] */"
    if body.count(needle) != 2:
        raise ValueError("7E530 constant packing loop drift")
    # Initial fallthrough only: never run again at the backedge label.
    return body.replace(needle, INSERT + needle, 1)
