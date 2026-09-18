"""Local x87 scratch for one pinned Halo 3925 visibility leaf.

Keep every guest load, stack access, comparison and integer instruction. Only
the two temporary x87 slots live in native locals until this call returns.
Owned lifted code is an input, never part of this module.
"""
import hashlib
import re

FLAG = 'XV_LOCAL_VISIBILITY_OUTCODE'
PIN = 'f2de260d868001c186b6a9d1a404e48296d9ae66bfd093e0ea5ba26b76fe3d23'


def transform(body):
    if hashlib.sha256(body.encode()).hexdigest() != PIN:
        raise ValueError('unqualified 12420 body')
    depth = 0
    pushes = pops = returns = 0
    lines = []
    for line in body.splitlines():
        match = re.fullmatch(r'    x87_push\(c, (.*)\);', line)
        if match:
            depth += 1
            pushes += 1
            if depth not in (1, 2):
                raise ValueError('unexpected x87 depth')
            line = '    vo_' + str(depth) + ' = ' + match[1] + ';'
            if pushes == 1:
                # First input is read before the guest PUSH ESI, including
                # when the pushed word aliases one of the input fields.
                line += '\n    c->fsp = vo_top;'
        elif line == '    x87_pop(c);':
            depth -= 1
            pops += 1
            if depth not in (0, 1):
                raise ValueError('unexpected x87 pop')
            line = ''
        elif 'X_ST(' in line:
            if 'x87_compare(' in line and depth != 1:
                raise ValueError('unexpected comparison depth')
            line = re.sub(r'X_ST\(([01])\)',
                          lambda m: 'vo_' + str(depth - int(m[1])), line)
        if line == '    c->r[4] += 4; return;':
            returns += 1
            line = ('    c->st[vo_top] = vo_1;\n'
                    '    c->st[(vo_top - 1u) & 7u] = vo_2;\n'
                    '    c->fsp = (vo_top + 1u) & 7u;\n' + line)
        lines.append(line)
    if (depth, pushes, pops, returns) != (0, 12, 12, 2):
        raise ValueError('unexpected leaf shape')
    changed = '\n'.join(lines) + '\n'
    changed = changed.replace('{\n', '{\n    const unsigned vo_top = (c->fsp - 1u) & 7u;\n    double vo_1, vo_2;\n', 1)
    if any(token in changed for token in ('X_ST(', 'x87_push(', 'x87_pop(')):
        raise ValueError('unhandled x87 operation')
    return ('#ifndef ' + FLAG + '\n#define ' + FLAG + ' 0\n#endif\n'
            '#if ' + FLAG + ' != 0 && ' + FLAG + ' != 1\n'
            '#error "' + FLAG + ' must be 0 or 1"\n#endif\n'
            '#if ' + FLAG + '\n' + changed + '#else\n' + body + '#endif\n')
