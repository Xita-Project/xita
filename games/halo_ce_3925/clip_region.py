"""Exact full-entry-only admission; interior dispatch entries remain unchanged."""
import hashlib

IMAGE_SHA256 = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
SPANS = {
    0xB7F10: (262, '231c54953646ada36746a087ec3b3f0b670bcb705f7f86a69b1b8351139b6921'),
    0x117B0: (133, '3c725257ae54c9341dacb68935ea5951b32e942265bebf6dacd021d08d00e873'),
    0xB71C0: (874, '34bf76203325f8157fcc9595b80b4be8851d829baabc556d45941533aa6c22e3'),
    0x1D130: (16, '810ef7f224dd7cd8feb82821c31ca0997daab985ffc11449ffa287d879ada54e'),
}


def matches_spans(image):
    return all(hashlib.sha256(image.bytes_at(a,n) or b'').hexdigest()==sha
               for a,(n,sha) in SPANS.items())


def hook(body):
    # Keep original setup, saved-stack writes, signed count tests and the first
    # taken preemption edge. Only the full entry's initial positive branch is
    # redirected. Existing loopbacks/interior entries retain their old labels.
    branch='if ((!XF_Z(c)&&(XF_S(c)==XF_O(c)))) goto L_000B7F50;'
    assert body.count(branch)==1, 'unexpected clip wrapper entry branch'
    first=body[body.index('    /* 000B7F50'):body.index('L_000B7F55:')]
    assert first.count('X_PREEMPT()')==1
    assert body.rstrip().endswith('}')
    marked=int('XV_FN(0x000B7F10u);' in body)
    body=body.replace(branch,branch.replace('L_000B7F50','CLIP_REGION_INITIAL'))
    return body.rstrip()[:-1]+'''CLIP_REGION_INITIAL:
'''+first+f'''#ifdef XV_NATIVE_CLIP_REGION
    {{ extern int xv_math_clip_region(xctx *, int);
      if (xv_math_clip_region(c, {marked})) return; }}
#endif
    goto L_000B7F55;
}}
'''
