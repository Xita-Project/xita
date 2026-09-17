"""Query-private ordered scalar reconstruction for the qualified edge scan.

No ELF, emulator, fixture or third-party dependencies. The replaced owned
interval is pinned byte-for-byte; authored replacement retains every scalar
entry/count read and the original backedge publication frontier.
"""
import hashlib

FEATURE = 'XV_QUERY_MEMBERSHIP_SCALAR'
START = 'L_0008711B:\n'
END = 'L_00087135:\n'
ORIGINAL_SHA256 = '55cf7b16b123ba8562dfe8ae9ca7dd464bb964755fe459cee954932b9422f643'
RETAINED_TEXT_SHA256 = '959993a9f34f47cf311e7424e5eff112874d4eacb4e4fe743d14dff8ab899596'
REPLACEMENT = '''L_0008711B:
    q1=0;
    {
        int32_t nq_edge_budget=c->preempt;
        uint32_t nq_edge_carry=c->f_cf;
        for(;;) {
            /* Compiler ordering only, not an ownership or CPU fence. Retain
             * each scalar entry/map read; do not prefetch a later entry. */
            __asm__ volatile("" ::: "memory");
            uint32_t nq_edge_value=X_M32(q0+q1*4u+0x408u);
            if(nq_edge_value==q7) {
                c->preempt=nq_edge_budget;c->f_cf=nq_edge_carry;
                X_FLAGS(XK_SUB,nq_edge_value,q7,nq_edge_value-q7,32);
                goto L_0008714E;
            }
            /* INC preserves CMP's carry in the raw f_cf backing field. */
            nq_edge_carry=nq_edge_value<q7;
            q2++;q1=(uint32_t)(int32_t)(int16_t)q2;
            __asm__ volatile("" ::: "memory");
            uint32_t nq_edge_count=X_M32(q0+0x404u);
            if((int32_t)q1>=(int32_t)nq_edge_count) {
                c->preempt=nq_edge_budget;c->f_cf=nq_edge_carry;
                X_FLAGS(XK_SUB,q1,nq_edge_count,q1-nq_edge_count,32);
                break;
            }
            /* Only the original taken 87133 backedge consumes budget. */
            if(--nq_edge_budget<=0) {
                c->preempt=nq_edge_budget;c->f_cf=nq_edge_carry;
                X_FLAGS(XK_SUB,q1,nq_edge_count,q1-nq_edge_count,32);
                NQ_SAVE(); memcpy(guest,c,sizeof *c);
                xv_preempt(guest);
                memcpy(c,guest,sizeof *c); NQ_LOAD();
                nq_edge_budget=c->preempt;nq_edge_carry=c->f_cf;
            }
        }
    }
'''


def generate(source):
    if ('#include "query_semantic_leaf.h"' not in source or
            'query_f32_primitives/xv_recomp_protos.h' not in source):
        raise ValueError('edge membership requires the qualified semantic query')
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError('edge membership labels changed')
    first = source.index(START)
    last = source.index(END, first)
    original = source[first:last]
    if hashlib.sha256(original.encode()).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('edge membership source interval drift')
    for label in ('L_00087120', 'L_00087129'):
        if label in source[:first] + source[last:]:
            raise ValueError('edge membership external interior entry')
    if FEATURE in source:
        raise ValueError('unexpected prior edge membership transformation')
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
