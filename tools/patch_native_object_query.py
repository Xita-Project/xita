#!/usr/bin/env python3
"""Opt-in native 88110 hook at the owned object-space fused query entry.

Usage: patch_native_object_query.py <stage>/recomp
The runtime defaults XV_NATIVE_OBJECT_QUERY to zero. The exact original
object-space reference remains the fallback and verification reference.
"""
from pathlib import Path
import sys

ENTRY = 'void nq_query_at_17301b(xctx *c)\n{\n'
BODY = '''#if XV_QUERY_WORLD_RUN
    nq_run_impl(c,0);
#else
    query_fused_172c95_171f94(c);
#endif'''
REFERENCE = '''/* Object-space reference: preserve world_run=0 on every fallback. */
static void nq_object_reference_17301b(xctx *c)
{
''' + BODY + '''
}
'''
HOOK = '''#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0
    extern void xv_native_4b9d0_object_query(xctx *, void (*)(xctx *));
    xv_native_4b9d0_object_query(c, nq_object_reference_17301b);
#else
    nq_object_reference_17301b(c);
#endif'''


def patch(text):
    if REFERENCE in text and HOOK in text:
        if text.count(REFERENCE) != 1 or text.count(HOOK) != 1 or text.count(ENTRY) != 1:
            raise ValueError("duplicate object-query hook")
        return text
    if 'nq_object_reference_17301b' in text:
        raise ValueError('partial object-query hook; inspect before patching')
    if text.count(ENTRY) != 1:
        raise ValueError('object query entry is missing or ambiguous')
    head, tail = text.split(ENTRY)
    end = tail.find('\n}\n')
    if end < 0 or tail[:end].count(BODY) != 1:
        raise ValueError('object query reference changed; refusing to guess')
    return head + REFERENCE + ENTRY + tail[:end].replace(BODY, HOOK) + tail[end:]


if __name__ == '__main__':
    path = Path(sys.argv[1]) / 'query_fusion.c'
    before = path.read_text()
    after = patch(before)
    if after != before:
        path.write_text(after)
    print('object query hook: ' + ('installed' if after != before else 'already present'))
