"""Query-private ordered scalar reconstruction for the qualified ancestor scan.

No ELF, emulator, fixture or third-party dependencies. The replaced owned
interval is pinned byte-for-byte; authored replacement retains every scalar
entry/count read and the original backedge publication frontier.
"""
import hashlib

FEATURE = 'XV_QUERY_ANCESTOR_SCALAR'
START = 'L_00087F7C:\n'
END = 'L_00087F8F:\n'
ORIGINAL_SHA256 = '8b7f876a00567e70d06aa99f883785037cf0320fa8214c7b3219d400b75c50c7'
RETAINED_TEXT_SHA256 = '06b9b249c7a014175c27d624ec682c014d9f17cd2fc609f5efac05f52336d0fa'
REPLACEMENT = '''L_00087F7C:
    q1=X_M32(q3);q0=0;
    {
        int32_t nq_ancestor_budget=c->preempt;
        uint32_t nq_ancestor_carry=c->f_cf;
        for(;;) {
            /* Retain scalar captured-root reads in original order. */
            __asm__ volatile("" ::: "memory");
            uint32_t nq_ancestor_value=X_M32(q6+q0*4u+0x1Cu);
            if(nq_ancestor_value==q1) {
                c->preempt=nq_ancestor_budget;c->f_cf=nq_ancestor_carry;
                X_FLAGS(XK_SUB,nq_ancestor_value,q1,nq_ancestor_value-q1,32);
                goto L_00087FE7;
            }
            nq_ancestor_carry=nq_ancestor_value<q1;
            q2++;q0=(uint32_t)(int32_t)(int16_t)q2;
            __asm__ volatile("" ::: "memory");
            uint32_t nq_ancestor_count=X_M32(q6+0x18u);
            if((int32_t)q0>=(int32_t)nq_ancestor_count) {
                c->preempt=nq_ancestor_budget;c->f_cf=nq_ancestor_carry;
                X_FLAGS(XK_SUB,q0,nq_ancestor_count,q0-nq_ancestor_count,32);
                break;
            }
            if(--nq_ancestor_budget<=0) {
                c->preempt=nq_ancestor_budget;c->f_cf=nq_ancestor_carry;
                X_FLAGS(XK_SUB,q0,nq_ancestor_count,q0-nq_ancestor_count,32);
                NQ_SAVE(); memcpy(guest,c,sizeof *c);
                xv_preempt(guest);
                memcpy(c,guest,sizeof *c); NQ_LOAD();
                nq_ancestor_budget=c->preempt;nq_ancestor_carry=c->f_cf;
            }
        }
    }
'''


def generate(source):
    if '#if XV_QUERY_MEMBERSHIP_SCALAR\n' not in source:
        raise ValueError('ancestor membership requires qualified scalar membership')
    if ('#include "query_semantic_leaf.h"' not in source or
            'query_f32_primitives/xv_recomp_protos.h' not in source):
        raise ValueError('ancestor membership requires the qualified semantic query')
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError('ancestor membership labels changed')
    first = source.index(START)
    last = source.index(END, first)
    original = source[first:last]
    if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('ancestor membership source interval drift')
    for label in ('L_00087F80', 'L_00087F86'):
        if label in source[:first] + source[last:]:
            raise ValueError('ancestor membership external interior entry')
    if FEATURE in source:
        raise ValueError('unexpected prior ancestor membership transformation')
    guarded = '#if ' + FEATURE + '\n' + REPLACEMENT + '#else\n' + original + '#endif\n'
    prefix = ('#ifndef ' + FEATURE + '\n#define ' + FEATURE + ' 0\n#endif\n'
              '#if ' + FEATURE + ' != 0 && ' + FEATURE + ' != 1\n'
              '#error "' + FEATURE + ' must be 0 or 1"\n#endif\n')
    changed = prefix + source[:first] + guarded + source[last:]
    return changed, dict(feature=FEATURE, default=0,
        original_interval_sha256=ORIGINAL_SHA256,
        replacement_sha256=hashlib.sha256(REPLACEMENT.encode()).hexdigest(),
        earliest_match=True, ordered_scalar_reads=True, hoisted_guest_reads=False,
        guest_stores_changed=False, captured_roots_unchanged=True,
        original_callback_frontier=True, generic_fallback_unchanged=True)
