"""Typed collision BSP regions with the original traversal continuations.

Original guest bodies are derived only from the owned image. The helpers
decline without side effects; accepted regions return to existing labels.
Every original child call, recursive call, stack prologue/epilogue and taken
backedge remains in its original body. No GPR localization wrapper remains.
"""
import hashlib

SPANS = {
    0x87E10: (130, 'f68295756aa6cae3b590a9c11c376ec01d9fb68d64e144c8b491b901a36fa620'),
    0x87EA0: (612, '2cbba93bbbd09d18a167e2658d435583bc2e9cfafef4acfd8c9ab728f9835d56'),
}


def matches(image):
    return all(hashlib.sha256(image.bytes_at(a, n) or b'').hexdigest() == h
               for a, (n, h) in SPANS.items())


def hook(address, body):
    if not __debug__:raise ValueError('Assertions required for traversal generation')
    assert address in SPANS
    assert 'xv_ct_node' not in body and 'xv_ct_project' not in body, 'already transformed'
    if address == 0x87E10:
        assert body.count('X_PREEMPT()') == 1
        assert 'L_00087E68:' in body
        # Both the typed decision and its original fallback live in a bounded
        # native block that returns before any recursive child or preempt. This
        # prevents temporary FP/alias storage accumulating in recursive frames.
        first=body.index('    /* 00087E20 ')
        second=body.index('L_00087E20:\n',first)
        end=body.index('L_00087E68:\n',second)
        initial=body[first:second]; original=body[second:end]
        assert 'X_PREEMPT' not in original and 'X_PUSH32' not in original
        assert original.count('goto L_00087E68;')==1
        fallback=original.replace('goto L_00087E68;','return;')
        helper='''#ifdef XV_NATIVE_COLLISION_TRAVERSAL
#include "kernel/xk_collision_traversal.h"
static __attribute__((noinline)) void xv_ct_node2_block_00087E10(xctx *c, uint8_t *xram_, const uint32_t *xpt_)
{
    if (xv_collision_traversal_mode && xv_ct_node2(c, xram_, xpt_)) return;
'''+fallback+'''    return;
}
#endif
'''
        call='    xv_ct_node2_block_00087E10(c, xram_, xpt_);\n    goto L_00087E68;\n'
        body=body[:first]+'#ifdef XV_NATIVE_COLLISION_TRAVERSAL\n'+call+'#else\n'+initial+'#endif\n'+ \
            'L_00087E20:\n#ifdef XV_NATIVE_COLLISION_TRAVERSAL\n'+call+'#else\n'+ \
            original[len('L_00087E20:\n'):]+'#endif\n'+body[end:]
        # No FP operation remains in the recursive shell when ON. Otherwise
        # GCC hoists integer flag-store constants into saved VFP registers,
        # growing every recursive frame despite outlining the FP region.
        signature='void f_00087E10(xctx *restrict c)'
        assert body.count(signature)==1
        body=body.replace(signature,'#if defined(XV_NATIVE_COLLISION_TRAVERSAL) && defined(__arm__)\n'
            '__attribute__((target("general-regs-only"), optimize("Os")))\n#endif\n'+signature,1)
        return helper+body
    else:
        assert body.count('X_PREEMPT()') == 4
        for label in ('L_00087F94:','L_00087F0F:','L_00087F9B:'):
            assert label in body
        start='    /* 00087EE6 '
        entry=('''#ifdef XV_NATIVE_COLLISION_TRAVERSAL
    if (xv_collision_traversal_mode) {
        unsigned ct_next_ = xv_ct_node3(c, xram_, xpt_);
        if (ct_next_ == 1) goto L_00087F94;
        if (ct_next_ == 2) goto L_00087F0F;
        if (ct_next_ == 3) goto L_00087F9B;
    }
#endif
''')
    assert body.count(start)==2, 'first/loop-copy drift'
    body=body.replace(start,entry+start)
    if address==0x87EA0:
        label='L_00087FE7:\n'
        assert body.count(label)==1 and '    /* 000880E1 ' in body
        body=body.replace('    /* 000880E1 ', '#ifdef XV_NATIVE_COLLISION_TRAVERSAL\nL_xv_ct_projection_done:\n#endif\n    /* 000880E1 ',1)
        body=body.replace(label,label+'''#ifdef XV_NATIVE_COLLISION_TRAVERSAL
    if (xv_collision_traversal_mode && xv_ct_project(c, xram_, xpt_)) goto L_xv_ct_projection_done;
#endif
''',1)
    return '#ifdef XV_NATIVE_COLLISION_TRAVERSAL\n#include "kernel/xk_collision_traversal.h"\n#endif\n'+body
