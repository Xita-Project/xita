"""Opt-in primary 172F40 query route; leaves the fused arithmetic unchanged.

The owning generator validates the full original image before this transform.
No selection based on a runtime guest return address is introduced.
"""
import hashlib
from tools.prototype_collision_query import extract_text

FEATURE = 'XV_QUERY_OBJECT_SPACE'
BODY_SHA256 = '49faedaa2cda0cf55fbb763368c45d2528682f7140613d0bf63a45ea58be6327'
SIGNATURE = 'void f_00172F40('
CALL = '    /* 0017301B  call 00088110h */\n    X_PUSH32(0x173020u);\n    f_00088110(c);'
SELECTED = CALL.replace('    f_00088110(c);',
    '#if XV_QUERY_OBJECT_SPACE\n    nq_query_at_17301b(c);\n#else\n    f_00088110(c);\n#endif')
GUARD = ('#ifndef XV_QUERY_OBJECT_SPACE\n#define XV_QUERY_OBJECT_SPACE 0\n#endif\n'
         '#if XV_QUERY_OBJECT_SPACE != 0 && XV_QUERY_OBJECT_SPACE != 1\n'
         '#error "XV_QUERY_OBJECT_SPACE must be 0 or 1"\n#endif\n'
         '#if XV_QUERY_OBJECT_SPACE && !XV_NATIVE_QUERY_FUSION\n'
         '#error "XV_QUERY_OBJECT_SPACE requires XV_NATIVE_QUERY_FUSION=1"\n#endif\n')
PREFIX = GUARD + '#if XV_QUERY_OBJECT_SPACE\n#include "xv_recomp_protos.h"\nvoid nq_query_at_17301b(xctx *);\n#endif\n'
ADAPTER = '''
#if XV_QUERY_OBJECT_SPACE
void nq_query_at_17301b(xctx *c)
{
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_HOLD_PROFILE)
    extern unsigned xv_object_hold_children_enabled;
    extern unsigned xv_object_motion_begin(xctx *,unsigned);
    extern void xv_object_motion_end(unsigned *);
    unsigned motion_sample_ __attribute__((cleanup(xv_object_motion_end))) =
        xv_object_hold_children_enabled ? xv_object_motion_begin(c,6u) : 0;
#endif
    query_fused_172c95_171f94(c);
}
#endif
'''


def validate_body(body):
    if hashlib.sha256(body.encode()).hexdigest() != BODY_SHA256 or body.count(CALL) != 1:
        raise ValueError('primary 172F40 object-query body drift')


def generic_caller(text):
    if FEATURE not in text:
        if 'nq_query_at_17301b' in text:
            raise ValueError('object-query hook without its guard')
        return text
    if not text.startswith(PREFIX) or text.count(SELECTED) != 1:
        raise ValueError('object-query prefix/callsite drift')
    result = text[len(PREFIX):].replace(SELECTED, CALL, 1)
    if FEATURE in result or 'nq_query_at_17301b' in result:
        raise ValueError('unexpected additional object-query hook')
    validate_body(extract_text(result, SIGNATURE))
    return result


def generate(caller, query):
    if FEATURE in caller or 'nq_query_at_17301b' in caller or FEATURE in query:
        raise ValueError('object-query transformation already applied')
    body = extract_text(caller, SIGNATURE)
    validate_body(body)
    if caller.count(body) != 1:
        raise ValueError('ambiguous primary 172F40 body')
    if query.count('void query_fused_172c95_171f94(') != 1:
        raise ValueError('missing complete fused query')
    selected = PREFIX + caller.replace(body, body.replace(CALL, SELECTED, 1), 1)
    if generic_caller(selected) != caller:
        raise ValueError('object-query transformation is not reversible')
    return selected, GUARD + query + ADAPTER, dict(
        primary='0x172f40', call='0x17301b', default=0,
        primary_body_sha256=BODY_SHA256, full_context_query=True,
        actor_transaction_unchanged=True, deployment_qualified=False)
