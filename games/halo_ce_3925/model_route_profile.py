"""Passive primary 5B760 child boundaries; emitted instructions stay intact."""
import hashlib
import re

BODY_SHA256 = "429b9ea96f983821e99babe6c6d8d15b329373821e9cfa294bbfb85ff6d6accd"
GUARD = "#if defined(XV_SCENE_BUCKET0_DETAIL) && XV_SCENE_BUCKET0_DETAIL\n"
ENTRY = GUARD + """    extern void xv_scene_model_begin(uint64_t *, void *);
    extern void xv_scene_model_step(uint64_t *, void *, unsigned);
    extern void xv_scene_model_end(uint64_t *);
    uint64_t xv_scene_model_scope_ __attribute__((cleanup(xv_scene_model_end))) = 0;
    xv_scene_model_begin(&xv_scene_model_scope_, c);
#endif
"""


def strip(body):
    return re.sub(r"^#if defined\(XV_SCENE_BUCKET0_DETAIL\) && XV_SCENE_BUCKET0_DETAIL\n.*?^#endif\n", "", body, flags=re.M | re.S)


def hook(body):
    canonical = re.sub(r"^    XV_PHASE_SCOPE\(c, \d+u\);\n", "", body, flags=re.M).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != BODY_SHA256:
        raise ValueError("primary 5B760 instruction/callback drift")
    original = body
    entry = re.match(r"void f_0005B760\(xctx \*restrict c\)\n\{\n(?:    XV_PHASE_SCOPE\(c, \d+u\);\n)?", body)
    if not entry:
        raise ValueError("primary 5B760 entry drift")
    body = body[:entry.end()] + ENTRY + body[entry.end():]
    for pc, bucket in ((0x5B77A, 1), (0x5B78A, 2), (0x5B7CA, 3)):
        needle = f"    /* {pc:08X}  "
        if body.count(needle) != 1:
            raise ValueError("primary 5B760 child frontier drift")
        step = GUARD + f"    xv_scene_model_step(&xv_scene_model_scope_, c, {bucket}u);\n#endif\n"
        body = body.replace(needle, step + needle)
    if strip(body) != original:
        raise ValueError("model observer changes instructions")
    return body
