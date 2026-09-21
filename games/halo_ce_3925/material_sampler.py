"""Exact-body guarded batching of the four retained CE material sampler groups."""
import hashlib

GROUPS = ((459840, 459910, 'ad896bce95395142810e673c813cae1f3c8dfa4f640ad3ecf02f40af71a31269'), (459928, 460013, 'a048cc4df83c421c1b3bc57c7a56ea403181ed024ac21aa9afc24e85daf479dc'), (460031, 460116, '15a94681a5d798efd19172a0d679331b058e1a83f749271261f38f66a57fe5c0'), (460134, 460236, 'ae69819cfab3fba2e1a5b8b1d9c50c3241236ddf2fc16ae5dbbc9797a1b6d785'))
MARKER = "XV_MATERIAL_SAMPLER_GROUP"


def hook(body):
    if MARKER in body:
        raise ValueError("material sampler body already patched")
    for stage, (start, end, digest) in enumerate(GROUPS):
        begin = body.index(f"    /* {start:08X} ")
        finish = body.index(f"    /* {end:08X} ", begin)
        block = body[begin:finish]
        if hashlib.sha256(block.encode()).hexdigest() != digest:
            raise ValueError(f"material sampler stage {stage} body drift")
        replacement = (
            f"#ifdef XV_NATIVE_MATERIAL_SAMPLER\n"
            f"    /* {MARKER} {stage} */\n"
            "    { extern int xv_material_sampler_try(xctx *, unsigned);\n"
            f"      if (!xv_material_sampler_try(c, {stage}u)) {{\n"
            "#endif\n" + block +
            "#ifdef XV_NATIVE_MATERIAL_SAMPLER\n    } }\n#endif\n")
        body = body[:begin] + replacement + body[finish:]
    return body
