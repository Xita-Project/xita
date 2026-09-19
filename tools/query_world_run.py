"""Opt-in world-query single-child runs, after the qualified scalar transforms.

No owned game bytes are embedded. The full input is pinned; generic functions,
first-node fallthrough, callback frontiers and object-query routing stay intact.
"""
import hashlib
from pathlib import Path

FEATURE = 'XV_QUERY_WORLD_RUN'
INPUT_SHA256 = '7b6fbbc8d4e821317bdb62782ffa0681674b3b03de5eb73b0510cab3f97e44e0'
OUTPUT = 'query_world_run.h'
SIGNATURE = '__attribute__((noinline)) void query_fused_172c95_171f94(xctx *restrict guest)'
LABEL = 'L_00087EC1:\n'
MARKER = '/* Selection comes from an actual specialized caller'
OBJECT = 'void nq_query_at_17301b(xctx *c)'
GUARD = '''#ifndef XV_QUERY_WORLD_RUN
#define XV_QUERY_WORLD_RUN 0
#endif
#if XV_QUERY_WORLD_RUN != 0 && XV_QUERY_WORLD_RUN != 1
#error "XV_QUERY_WORLD_RUN must be 0 or 1"
#endif
#if XV_QUERY_WORLD_RUN && (!XV_QUERY_OBJECT_SPACE || !XV_QUERY_ANCESTOR_SCALAR || !defined(XV_EXPERIMENTAL_OBJECT_JOBS))
#error "world query runs require object routing, ancestor scalar and object jobs"
#endif
'''


def generate(source):
    if hashlib.sha256(source.encode()).hexdigest() != INPUT_SHA256:
        raise ValueError('world-query run input drift')
    for marker in (SIGNATURE, LABEL, MARKER, OBJECT, '#include "query_semantic_leaf.h"'):
        if source.count(marker) != 1:
            raise ValueError('world-query run boundary drift: ' + marker)
    header = Path(__file__).with_suffix('.h').read_text()
    changed = source.replace('#include "query_semantic_leaf.h"',
        '#include "query_semantic_leaf.h"\n#if XV_QUERY_WORLD_RUN\n#include "query_world_run.h"\n#endif')
    changed = changed.replace(SIGNATURE,
        '#if XV_QUERY_WORLD_RUN\nstatic __attribute__((noinline,noclone)) void nq_run_impl(xctx *restrict guest,unsigned world_run)\n#else\n' + SIGNATURE + '\n#endif')
    changed = changed.replace(LABEL, LABEL +
        '#if XV_QUERY_WORLD_RUN\n    if(world_run) nq_run3(c,guest,xram_,xpt_,&q2,q3,q5,q1,q6,q4);\n#endif\n')
    changed = changed.replace(MARKER,
        '#if XV_QUERY_WORLD_RUN\nvoid query_fused_172c95_171f94(xctx *c){nq_run_impl(c,1);}\n#endif\n' + MARKER)
    index = changed.index(OBJECT)
    tail = changed[index:]
    call = '    query_fused_172c95_171f94(c);'
    if tail.count(call) != 1:
        raise ValueError('world-query object adapter drift')
    changed = changed[:index] + tail.replace(call,
        '#if XV_QUERY_WORLD_RUN\n    nq_run_impl(c,0);\n#else\n' + call + '\n#endif')
    return GUARD + changed, {OUTPUT: header}, dict(
        feature=FEATURE, default=0, input_sha256=INPUT_SHA256,
        helper_sha256=hashlib.sha256(header.encode()).hexdigest(),
        backedge='0x87ec1', world_adapter='0x171f94', object_adapter_bypassed='0x17301b',
        max_run=32, last_original_node=True, actor_guard_retained=True,
        explicit_route=True, generic_functions_unchanged=True)
