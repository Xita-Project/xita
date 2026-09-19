"""Pinned, private world-query capture variant; no production build wiring.

The ordinary query and shared runtime headers are untouched. All capture state
is explicit, including through private inline helpers: no shared active pointer
or TLS lookup is introduced. The output still executes the original search.
The selected callback-free entry captures the global arena/page roots. Scalar
notifications rely on those roots remaining equal throughout recording; helper
functions in this private variant are not standalone mismatched-root APIs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

PREFIX = 'query_f32_primitives/'
PRIVATE = 'query_capture_primitives/'
HEADERS = ('xv_recomp_protos.h', 'xv_x86rt.h', 'xv_phase.h',
           'kernel/xk_object_jobs.h', 'kernel/xk_light_census.h',
           'kernel/xk_collision_vertices.h', 'kernel/xk_segment_sphere.h',
           'kernel/xk_collision_traversal.h')
# Filled from the reviewed retained perf19 output, not arbitrary generated code.
PINS = {'query_fusion.c': 'f078a349ea6a2b70e0391e5ed4d4fa9c88836653797c6cfa61c2c4375d435fcb', 'query_world_run.h': '51edb531bc7deb1f782fce05235dd1c5054fa388d01095760fa551aaa4fe14b8', 'query_semantic_leaf.h': '6137c6e195ec68c644383f1fd88e2660390564a9268df67d4cb5aa9ce4efdb7c', 'query_f32_primitives/kernel/xk_collision_traversal.h': '3b8b4ed2cb84b7d679074e20dca98475d4dccd4c3aadf67154d6bb6b95b24ca0', 'query_f32_primitives/kernel/xk_collision_vertices.h': '5a5109b9c9f7d66b0b4e85b54e76b0e66aee24c629aeda2d447fb891fdb44923', 'query_f32_primitives/kernel/xk_light_census.h': '717239d3a7b1164dee99ff61ef04cd4576f907b5e08c89d7715f87f4f7183c66', 'query_f32_primitives/kernel/xk_object_jobs.h': 'e2508269725ec773eadaa9576a983cd1f14f09f1656be26c32154d6b684cb43c', 'query_f32_primitives/kernel/xk_segment_sphere.h': '62648a6201c785f52c794eed975965d2d5106ba500d8ae0a044b1430b7865988', 'query_f32_primitives/xv_phase.h': '11cc21c8eb7d5d3a78df629ae7d08b55fa974ca1bf21c8bd46b150e76be0ec63', 'query_f32_primitives/xv_recomp_protos.h': 'ec829feda3f437e26389666fed25470a947e12d18bda2f830652d6312698ba71', 'query_f32_primitives/xv_x86rt.h': 'd3d51d7a2a7aacc5bfc359740d0a349ab9cb758023e09378c6455ea41abbec24'}
DEPENDENT = set(('x_guest_read x_guest_write x_pop32 x87_load_f32 x87_load_f64 '
    'x87_load_i16 x87_load_i32 x87_load_i64 x87_store_f32 x87_store_f64 '
    'x87_store_i16 x87_store_i32 x87_store_i64 x_load128 x_store128 '
    'xv_ct_word xv_ct_span xv_ct_node2 xv_ct_node3 xv_ct_project '
    'xv_collision_vertices xv_segment_sphere nq_run_span nq_run3 '
    'nq_run_impl query_fused_172c95_171f94').split())


def masked(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  text, flags=re.S)


def close_paren(code, start):
    depth = 0
    for i in range(start, len(code)):
        if code[i] == '(':
            depth += 1
        elif code[i] == ')':
            depth -= 1
            if depth == 0:
                return i
    raise ValueError('unclosed call')


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('capture source shape changed: ' + old[:80])
    return text.replace(old, new)


def stores(text):
    """Evaluate the RHS before notifying a store, including read-modify-write.

    Not a general C parser: only pinned emitted/simple helper assignments are
    accepted. Nested parentheses/braces in a RHS are balanced before its ';'.
    """
    code = masked(text)
    edits = []
    for m in re.finditer(r'\bX_([MW])(8|16|32|64|F32)\s*\(', code):
        start = code.index('(', m.start())
        end = close_paren(code, start)
        rest = re.match(r'\s*(=|\+=|-=|\*=|/=|\+\+|--)', code[end + 1:])
        if not rest:
            continue
        eq = end + 1 + rest.end() - len(rest[1])
        if code[eq:eq + 2] == '==':
            continue
        if rest[1] != '=':
            raise ValueError('unreviewed compound memory store')
        depth = 0
        finish = None
        for pos in range(eq + 1, len(code)):
            if code[pos] in '([{':
                depth += 1
            elif code[pos] in ')]}':
                depth -= 1
            elif code[pos] == ';' and depth == 0:
                finish = pos
                break
        if finish is None or depth:
            raise ValueError('unreviewed memory store expression')
        address, value = text[start + 1:end], text[eq + 1:finish].strip()
        suffix = ('W' if m[1] == 'W' else '') + m[2]
        edits.append((m.start(), finish, f'NQ_STORE{suffix}({address}, ({value}))'))
    for a, b, value in reversed(edits):
        text = text[:a] + value + text[b:]
    return text, len(edits)


def parameters(text):
    code = masked(text)
    edits = []
    definitions = []
    for m in re.finditer(r'\b(' + '|'.join(sorted(DEPENDENT)) + r')\s*\(', code):
        start = code.index('(', m.start())
        end = close_paren(code, start)
        tail = code[end + 1:].lstrip()
        definition = tail.startswith('{')
        prefix = 'XvQueryMemory *nq_capture, ' if definition else 'nq_capture, '
        if definition:
            definitions.append(m[1])
        edits.append((start + 1, prefix))
    for where, prefix in reversed(edits):
        text = text[:where] + prefix + text[where:]
    return text, definitions


ACCESS = r'''
/* Keep the original pointer calculation; notification never substitutes a
 * mapped/split load for a physically contiguous typed access. */
static inline void *nq_capture_pointer(XvQueryMemory *capture, uint32_t address,
                                     void *pointer, uint32_t size, unsigned write)
{
    xv_query_capture_touch(capture,g_xram,g_xpt,address,pointer,size,write);
    return pointer;
}
'''


def scalar_accessors():
    result = []
    types = {'8': ('uint8_t', 'uint8_t', 1), '16': ('uint16_t', 'xu16_u', 2),
             '32': ('uint32_t', 'xu32_u', 4), '64': ('uint64_t', 'xu64_u', 8),
             'F32': ('float', 'xf32_u', 4)}
    for suffix, (value, storage, size) in types.items():
        result.append(f'''static inline void nq_store{suffix}(XvQueryMemory *capture,
    uint32_t address, void *pointer, {value} value)
{{ nq_capture_pointer(capture,address,pointer,{size},1); *({storage} *)pointer=value; }}
#define NQ_STORE{suffix}(a,v) nq_store{suffix}(nq_capture,(uint32_t)(a),X_G(a),({value})(v))
#define NQ_STOREW{suffix}(a,v) nq_store{suffix}(nq_capture,(uint32_t)(a),X_GW(a),({value})(v))
''')
    return ''.join(result)


def runtime(text):
    text = replace_once(text, '#pragma once', '#pragma once\n#include <kernel/xk_query_capture.h>\n'
        '#ifdef XV_CHECK_GUEST_ADDRESS\n#error "query capture checked-address side effects are not qualified"\n#endif')
    text = replace_once(text, 'void x_guest_read_pages(', ACCESS + '\nvoid x_guest_read_pages(')
    # Match original page-splitting behavior, including the uncommon slow path.
    for name, body in {
        'x_guest_read': '''
    unsigned char *p=dst;
    while(size) {
        size_t n=4096u-(a&4095u); if(n>size)n=size;
        void *source=X_G(a);
        nq_capture_pointer(nq_capture,a,source,(uint32_t)n,0);
        memcpy(p,source,n); p+=n; a+=(uint32_t)n; size-=n;
    }
''',
        'x_guest_write': '''
    const unsigned char *p=src;
    while(size) {
        size_t n=4096u-(a&4095u); if(n>size)n=size;
        void *target=X_GW(a);
        nq_capture_pointer(nq_capture,a,target,(uint32_t)n,1);
        memcpy(target,p,n); p+=n; a+=(uint32_t)n; size-=n;
    }
'''
    }.items():
        code = masked(text)
        m = re.search(r'\b' + name + r'\(', code)
        end = close_paren(code, code.index('(', m.start()))
        start = code.index('{', end)
        depth = 1
        for last in range(start + 1, len(code)):
            if code[last] == '{': depth += 1
            elif code[last] == '}': depth -= 1
            if depth == 0: break
        text = text[:start + 1] + body + text[last:]
    marker = '#define X_M8(a)'
    at = text.index(marker)
    text = text[:at] + scalar_accessors() + text[at:]
    for suffix, size in [('8',1),('16',2),('32',4),('64',8),('F32',4)]:
        pattern = r'(#define X_M' + suffix + r'\(a\)[^\n]*)'
        old = re.search(pattern, text)[1]
        text = replace_once(text, old, old.replace('X_G(a)',
            f'nq_capture_pointer(nq_capture,(uint32_t)(a),X_G(a),{size},0)'))
    return text


def generate(recomp_dir):
    root = Path(recomp_dir)
    contents = {name: (root / name).read_text() for name in PINS}
    actual = {name: hashlib.sha256(text.encode()).hexdigest() for name, text in contents.items()}
    if actual != PINS:
        raise ValueError('capture requires the reviewed source inventory: ' +
                         ', '.join(n for n in actual if actual[n] != PINS[n]))
    outputs = {}
    inventories = {}
    query = contents['query_fusion.c']
    marker = '/* Selection comes from an actual specialized caller'
    assert query.count(marker) == 1
    query = query[:query.index(marker)] + '\n#endif /* XV_NATIVE_QUERY_FUSION */\n'
    query = replace_once(query,
        '#if XV_QUERY_WORLD_RUN\nstatic __attribute__((noinline,noclone)) void nq_run_impl(xctx *restrict guest,unsigned world_run)\n'
        '#else\n__attribute__((noinline)) void query_fused_172c95_171f94(xctx *restrict guest)\n#endif\n{',
        'static __attribute__((noinline,noclone)) void nq_run_impl(xctx *restrict guest,unsigned world_run)\n{')
    query = '#if !XV_QUERY_WORLD_RUN\n#error "capture requires reviewed world-run path"\n#endif\n' + query
    query = query.replace(PREFIX, PRIVATE).replace('"query_world_run.h"', '"query_capture_world_run.h"')
    query = query.replace('"query_semantic_leaf.h"', '"query_capture_semantic_leaf.h"')
    old = '*(xu32_u *)(g_xram+g_xpt[q4>>12]+(q4&4095u))'
    query = replace_once(query, old,
        '*(xu32_u *)nq_capture_pointer(nq_capture,q4,(void *)(g_xram+g_xpt[q4>>12]+(q4&4095u)),4,0)')
    contents['query_fusion.c'] = query
    for name, text in contents.items():
        if name == PREFIX + 'xv_x86rt.h':
            text = runtime(text)
        # Only the reviewed alias-gate PTE expressions occur in these two units.
        if name in ('query_fusion.c', PREFIX + 'kernel/xk_segment_sphere.h'):
            text = replace_once(text, 'xpt_[0x1F0A68u >> 12] == xpt_[s >> 12]',
                'xv_query_capture_pte(nq_capture,xram_,xpt_,0x1F0A68u >> 12) == '
                'xv_query_capture_pte(nq_capture,xram_,xpt_,s >> 12)')
        if name == PREFIX + 'kernel/xk_collision_traversal.h':
            assert text.count('pages[a>>12]') == 1
            text = text.replace('pages[a>>12]', 'xv_query_capture_pte(nq_capture,arena,pages,a>>12)')
        if name == 'query_world_run.h':
            assert text.count('pages[a>>12]') == 2
            text = text.replace('pages[a>>12]', 'xv_query_capture_pte(nq_capture,arena,pages,a>>12)')
            text = replace_once(text, '*out=arena+offset;return 1;',
                '*out=arena+offset;xv_query_capture_physical(nq_capture,*out,n,0);return 1;')
            text = text.replace('xv_query_world_run_report', 'xv_query_capture_world_run_report')
        text, store_count = stores(text)
        text, definitions = parameters(text)
        # The original callback/fallback still executes with the original context.
        text, callbacks = re.subn(r'\bxv_preempt\((c|guest)\)',
            r'(xv_query_capture_abandon(nq_capture,XV_QM_CALLBACK),xv_preempt(\1))', text)
        text, fallbacks = re.subn(r'\bf_([0-9A-F]{8})\(guest\)',
            r'(xv_query_capture_abandon(nq_capture,XV_QM_UNKNOWN),f_\1(guest))', text)
        if name == 'query_fusion.c':
            text = text.replace('query_fused_172c95_171f94', 'query_captured_172c95_171f94')
            out = 'query_capture.c'
        elif name.startswith(PREFIX): out = PRIVATE + name[len(PREFIX):]
        else: out = name.replace('query_', 'query_capture_', 1)
        outputs[out] = text
        inventories[name] = dict(stores=store_count,definitions=definitions,
                                 callback_sites=callbacks,fallback_sites=fallbacks)
    contract = dict(input_sha256=actual,inventory=inventories,
                    output_sha256={n: hashlib.sha256(t.encode()).hexdigest() for n,t in outputs.items()},
                    production_wired=False,checked_address_supported=False)
    return outputs, contract


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--recomp-dir',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    source, output = a.recomp_dir.resolve(), a.out.resolve()
    if source == output or source in output.parents or output in source.parents:
        raise ValueError('use a disjoint capture output directory')
    outputs,contract=generate(a.recomp_dir)
    a.out.mkdir(parents=True,exist_ok=True)
    for name,text in outputs.items():
        path=a.out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
    (a.out/'capture-contract.json').write_text(json.dumps(contract,indent=2)+'\n')


if __name__=='__main__':main()
