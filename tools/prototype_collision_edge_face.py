#!/usr/bin/env python3
"""Bounded private 8709A..872BA x87-local experiment; no production hooks.

Reuse the complete owned-image query oracle and current typed helpers. Only the
residual edge/face interval receives a local x87 stack. All guest reads/stores,
traversal, deduplication, callbacks and original taken-backedge budgets remain.
The generic original bodies and their interior entries remain the oracle.
"""
from pathlib import Path
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
from tools import prototype_collision_query as query

START = 'L_0008709A:\n'
END = '    /* 000872BA  pop edi */'
REGION_SHA = 'f22e235d24f38780aa98eb7b867a47aed6141d21c2c6cb271b86870d2125f992'
SITES = {0x87133: 0x87120, 0x87162: 0x870A0,
         0x87281: 0x871C1, 0x872A6: 0x87294}
SAVE = 'do { c->fsp=ef_top; memcpy(c->st,ef_st,sizeof ef_st); } while(0)'
LOAD = 'do { ef_top=c->fsp; memcpy(ef_st,c->st,sizeof ef_st); } while(0)'


def transform(body, style):
    if not __debug__:
        raise RuntimeError('Run without Python -O: identity assertions are required')
    assert style in ('fixed', 'dynamic')
    assert body.count(START) == 1 and body.count(END) == 2
    begin = body.index(START) + len(START)
    end = body.index(END, begin)
    region = body[begin:end]
    digest = hashlib.sha256(region.encode()).hexdigest()
    assert digest == REGION_SHA, ('edge/face instruction drift', digest)
    assert region.count('f_000B0CB0(c);') == 2  # emitted edge-loop head copies
    assert region.count('X_PREEMPT();') == 5  # repeated 87162 block
    assert set(re.findall(r'\bf_([0-9A-F]{8})\(', region)) == {'000B0CB0'}
    labels = set(re.findall(r'L_([0-9A-F]{8}):', region))
    assert labels == {'000870A0', '0008710C', '0008711B', '00087120',
                      '00087129', '00087135', '0008713D', '0008714E',
                      '00087153', '00087168', '00087174', '000871C1',
                      '000871C5', '00087279', '00087287', '00087292',
                      '00087294', '0008729E', '000872A8', '000872B0'}
    assert set(re.findall(r'goto L_([0-9A-F]{8})', region)) <= labels | {'000872BA'}
    assert len(re.findall(r'\bfk_[abr]\b', region)) == 0
    candidate = re.sub(r'\bL_([0-9A-F]{8})\b', r'EF_\1', region)
    candidate = candidate.replace('EF_000872BA', 'EF_DONE')
    lines = []
    pc = None
    stack_depth = 0
    touched = set()
    for line in candidate.splitlines(True):
        match = re.search(r'/\* ([0-9A-F]{8}) ', line)
        if match:
            pc = int(match[1], 16)
        if style == 'fixed' and (re.match(r'EF_\w+:', line) or 'goto EF_' in line):
            assert stack_depth == 0, ('nonzero branch/label stack depth', pc, stack_depth)
        # Exact stack operations. Float loads/stores keep their global roots
        # and original order, including page-aware accesses and stack aliases.
        if style == 'dynamic':
            line = re.sub(r'X_ST\((\d+)\)', r'ef_st[(ef_top+\1u)&7u]', line)
            push = re.fullmatch(r'    x87_push\(c, (.*)\);\n', line)
            if push:
                line = '    { double value_=' + push[1] + '; ef_top=(ef_top-1u)&7u; ef_st[ef_top]=value_; }\n'
            line = line.replace('x87_pop(c);', 'ef_top=(ef_top+1u)&7u;')
        else:
            def slot(match):
                index = (stack_depth + int(match[1])) & 7
                touched.add(index)
                return f'ef{index}'
            line = re.sub(r'X_ST\((\d+)\)', slot, line)
            push = re.fullmatch(r'    x87_push\(c, (.*)\);\n', line)
            if push:
                stack_depth -= 1
                touched.add(stack_depth & 7)
                line = f'    ef{stack_depth & 7} = ' + push[1] + ';\n'
            if 'x87_pop(c);' in line:
                assert line.strip() == 'x87_pop(c);'
                stack_depth += 1
                line = ''
            assert -4 <= stack_depth <= 0
        # Match the original ordered VFP subtraction; do not let local stack
        # values enable VMLA/VMLS contractions or a changed NaN sign.
        subtract = re.fullmatch(r'    (ef_st\[.*?\]|ef[4-7]) = (ef_st\[.*?\]|ef[4-7]) - (.*);\n', line)
        if subtract:
            line = f'    {subtract[1]} = xv_ss_sub({subtract[2]}, {subtract[3]});\n'
        if 'x87_compare(c,' in line:
            line = (f'    c->fsp=(ef_top+{stack_depth & 7}u)&7u;\n' if style == 'fixed' else '    c->fsp=ef_top;\n') + line
        if 'f_000B0CB0(c);' in line:
            # Publication precedes the actual child, not an invented observer.
            line = '    EF_SAVE();\n' + line + '    EF_LOAD();\n'
            line += '    if(!xv_collision_vertices_fp_ok()) goto L_EF_ORIGINAL_00087108;\n'
        if 'X_PREEMPT();' in line:
            assert pc in SITES
            target = SITES[pc]
            replacement = ('if(c->preempt<=1){ EF_SAVE(); X_PREEMPT(); EF_LOAD(); '
                           f'if(!xv_collision_vertices_fp_ok()) goto L_{target:08X}; '
                           '} else --c->preempt;')
            line = line.replace('X_PREEMPT();', replacement)
        lines.append(line)
    assert stack_depth == 0
    if style == 'fixed':
        assert touched == {4, 5, 6, 7}
    candidate = ''.join(lines)
    assert 'x87_push(c,' not in candidate and 'x87_pop(c)' not in candidate
    assert 'X_ST(' not in candidate
    # Use an original label just before the post-child instruction; both
    # emitted loop heads can resume here because their suffixes are identical.
    original = region.replace('    /* 00087108  test al,al */',
                              'L_EF_ORIGINAL_00087108:\n    /* 00087108  test al,al */', 1)
    save, load, declarations = SAVE, LOAD, 'double ef_st[8]; unsigned ef_top;'
    if style == 'fixed':
        save = 'do { c->fsp=ef_top; ' + ' '.join(f'c->st[(ef_top+{i}u)&7u]=ef{i};' for i in range(4, 8)) + ' } while(0)'
        load = 'do { ef_top=c->fsp; ' + ' '.join(f'ef{i}=c->st[(ef_top+{i}u)&7u];' for i in range(4, 8)) + ' } while(0)'
        declarations = 'double ef4,ef5,ef6,ef7; unsigned ef_top;'
    native = ('#if defined(XV_NATIVE_COLLISION_EDGE_FACE)\n'
              '    if(xv_collision_vertices_fp_ok()) {\n'
              f'    {declarations}\n'
              f'#define EF_SAVE() {save}\n#define EF_LOAD() {load}\n'
              '    EF_LOAD();\n' + candidate +
              'EF_DONE:\n    EF_SAVE();\n    goto L_000872BA;\n'
              '#undef EF_SAVE\n#undef EF_LOAD\n    }\n#endif\n')
    return body[:begin] + native + original + body[end:]


def main():
    if not __debug__:
        raise SystemExit('Run without Python -O: identity assertions are required')
    if '--out' in sys.argv:
        output = Path(sys.argv[sys.argv.index('--out')+1]).resolve()
        if output.is_relative_to(ROOT):
            raise SystemExit('Owned outputs must be outside the source checkout')
    style = 'fixed'
    if '--edge-style' in sys.argv:
        index = sys.argv.index('--edge-style')
        style = sys.argv[index+1]
        del sys.argv[index:index+2]
    assert style in ('fixed', 'dynamic')
    original_fused = query.fused

    def edge_fused(bodies, args):
        modified = dict(bodies)
        modified[0x86F50] = transform(bodies[0x86F50], style)
        return '#define XV_NATIVE_COLLISION_EDGE_FACE 1\n' + original_fused(modified, args)

    query.fused = edge_fused
    query.main()


if __name__ == '__main__':
    main()
