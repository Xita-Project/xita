#!/usr/bin/env python3
"""qlocals v2: guest GPRs and lazy flags held in C locals inside generated functions.

Why: generated guest memory accesses use may_alias types and the context
pointer escapes into callees, so GCC must assume every guest load/store can
alias c->r[] and c->f_*.  Every guest instruction then reloads/stores the
context.  Locals whose address never escapes cannot alias guest memory.

Semantics (per transformed function, after its prologue):
  * q0..q7 mirror c->r[0..7]; qf mirrors the nine lazy-flag fields.
  * Before any barrier (a call that may observe/modify guest registers) the
    registers that may have changed since the last synchronisation are
    stored (dirty mask from a conservative forward dataflow over the
    function's labels/gotos); after it all eight are reloaded (GCC removes
    dead reloads).
  * Flags: x86 code never keeps flags across call/ret (the recompiler's own
    dead-flag analysis already relies on this), so guest calls, indirect
    guest calls, HLE calls and returns do not synchronise flags.  Every other
    barrier stores all flags before and reloads them after.
  * Yields (X_PREEMPT slow path) store/reload all registers; flags stay in
    locals (only this fiber reads them).
Anything not understood excludes the function (it keeps the original body).
"""
import collections, re, sys

# ----------------------------------------------------------------------------------------------
# Calls that never read or write c->r (verified against xv_x86rt.h/xv_x87reg.h and shard-local
# inline definitions).  Flag helpers are renamed to xq_* variants before classification.
SAFE = set('''
X_M8 X_M16 X_M32 X_M64 X_MF32 X_W8 X_W16 X_W32 X_W64 X_WF32 X_G X_GW X_GWN X_IMG8 X_IMG16 X_IMG32
X_ST X_QS8 X_QS16 X_QS32 XQF_FLAGS XQF_FLAGS_C
x87_load_f32 x87_pop x87_store_f32 x87_push x87_load_i32 x87_load_f64
x87_round x87_trunc x87_store_i32 x87_store_f64 x87_load_f80 x87_store_f80 x87_store_i64 x87_load_i64 x87_load_i16 x87_store_i16
x_shufps x_load128 x_store128 x_bitops128 x_unpcklps x_unpckhps x_cmpss x_cmpps x_movmskps x_minmaxps x_rsqrtps
x_sat8 x_sat16 x_usat8 x_usat16
fabs sqrt sin cos tan atan2 atan log2 exp2 ldexp trunc floor ceil fmod pow log log10 exp sqrtf fabsf
__builtin_expect memcpy x_guest_read x_guest_write
effects_load_f32 effects_store_f32 x87r_fxam x87_fxam x_rdtsc __builtin_ctz __builtin_clz truncf strncmp
xv_scene_phase_begin xv_scene_phase_end xv_object_hold_child_begin xv_object_hold_child_end
XQ_R16 XQ_R8L XQ_R8H XQ_PUSH32 XQ_POP32 XQ_PREEMPT
'''.split())
SAFE_PREFIX = ('x_mmx_', 'XFI_', 'xq_')
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'defined', '__typeof__', 'typeof', 'do', 'else', 'case', 'goto'}
# Barriers after which flags are dead by the x86 calling convention (no flag sync).
# Flag sync around guest calls: 2 = reload after only (callee flags reach the caller as in the base code);
# 3 = also store before (callee may read caller flags).  QL_GUEST_FLAGS overrides for experiments.
import os
GUEST_FLAGS = int(os.environ.get('QL_GUEST_FLAGS', '2'))
GUEST_CALL = re.compile(r'f_[0-9A-F]{8}|xv_call|XV_HLE_CALL')

# Flag helpers that get xq_ variants taking (c, qf-pointer, ...).
FLAG_FUNCS = ['xf_mask', 'xf_msb', 'XF_Z', 'XF_S', 'XF_P', 'XF_C', 'XF_O', 'xf_eflags', 'xf_set_eflags',
              'x_shld', 'x_shrd', 'x_imul32', 'x_imul16', 'x87_compare', 'x_comiss', 'x87r_compare']
SHIFT_FUNCS = [f'x_{op}{b}' for op in ('shl', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr') for b in (8, 16, 32)]
FLAG_HELPERS = FLAG_FUNCS + SHIFT_FUNCS
FLAG_FIELDS = ['f_kind', 'f_op1', 'f_op2', 'f_res', 'f_bits', 'f_cf_override', 'f_cf', 'f_of_override', 'f_of']

UNSUPPORTED_ANY = re.compile(r'#\s*(define|undef)\b|\bq[0-7]\b|\bqf\b|\basm\b|__asm__|\bVP_|\bSPRITE_|\bpath_(load|store)|\bxq_t_\b'
                             r'|&c->r|&c->f_|\bX_ARG\b|\bX_RET\b|\bx_pop32\b|\bxq_|\bXQ_|goto\s*\*')
UNSUPPORTED_REST = re.compile(r'__attribute__|\bvolatile\b|\bXV_PHASE_SCOPE\b|\bXV_OBJECT_MATH_GUARD\b')

FUNC_RE = re.compile(r'^void (f_[0-9A-F]{8})\(xctx \*restrict c\)\n\{\n', re.M)
LABEL_LINE_RE = re.compile(r'^(L_[0-9A-F]{8}:|M_\w+:|    goto L_[0-9A-F]{8};)', re.M)
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)[ \t]*\(')


# ----------------------------------------------------------------------------------------------
# xq_ helper generation from the runtime headers (mechanical: only the context-flag accesses change)
def _extract_function(text, name):
    m = re.search(r'static inline[^;{()]*?\b' + re.escape(name) + r'\s*\(', text)
    if not m:
        return None
    i = text.index('{', m.end())
    depth, j = 0, i
    while True:
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[m.start():j + 1]
        j += 1


def _flagify(src):
    src = re.sub(r'\bc->f_', 'xqfp->f_', src)
    src = re.sub(r'\bX_FLAGS_C\(', 'XQF_FLAGS_C(xqfp, ', src)
    src = re.sub(r'\bX_FLAGS\(', 'XQF_FLAGS(xqfp, ', src)
    for h in FLAG_FUNCS:
        src = re.sub(r'\b' + h + r'\(c\)', f'xq_{h}(c, xqfp)', src)
        src = re.sub(r'\b' + h + r'\(c,', f'xq_{h}(c, xqfp,', src)
    src = re.sub(r'\bx_(shl|shr|sar|rol|ror|rcl|rcr)##B\(xctx \*c,', r'xq_x_\1##B(xctx *c, xqf *xqfp,', src)
    return src


def build_helpers(header_text):
    out = ['/* ---- qlocals v2: flag helpers on a local flag struct (generated from xv_x86rt.h/xv_x87reg.h) ---- */',
           'typedef struct xqf { uint32_t ' + ', '.join(FLAG_FIELDS) + '; } xqf;',
           '#define XQF_FLAGS(f, kind, op1, op2, res, bits) do { (f)->f_kind = (kind); (f)->f_op1 = (uint32_t)(op1); '
           '(f)->f_op2 = (uint32_t)(op2); (f)->f_res = (uint32_t)(res); (f)->f_bits = (bits); (f)->f_cf_override = 0; '
           '(f)->f_of_override = 0; } while (0)',
           '#define XQF_FLAGS_C(f, kind, op1, op2, res, bits, cfin) do { XQF_FLAGS(f, kind, op1, op2, res, bits); (f)->f_cf = (cfin); } while (0)']
    for name in FLAG_FUNCS:
        fn = _extract_function(header_text, name)
        if fn is None:
            raise ValueError(f'helper {name} not found')
        fn = re.sub(r'\b' + name + r'\s*\(', 'xq_' + name + '(', fn, count=1)
        fn = re.sub(r'\((const )?xctx \*c', lambda m: f'({m.group(1) or ""}xctx *c, xqf *xqfp', fn, count=1)
        body = _flagify(fn)
        body = body.replace('static inline', 'static inline __attribute__((always_inline, unused))', 1)
        out.append(body)
    m = re.search(r'#define X_DEF_SHIFTS\(B, T\)(.*?)\nX_DEF_SHIFTS\(8', header_text, re.S)
    if not m:
        raise ValueError('X_DEF_SHIFTS not found')
    macro = '#define XQ_DEF_SHIFTS(B, T)' + _flagify(m.group(1)).replace('static inline', 'static inline __attribute__((always_inline, unused))')
    out.append(macro)
    out += ['XQ_DEF_SHIFTS(8, uint8_t)', 'XQ_DEF_SHIFTS(16, uint16_t)', 'XQ_DEF_SHIFTS(32, uint32_t)']
    # every generated body must reference flags only through f
    joined = '\n'.join(out)
    if re.search(r'\bc->f_', joined):
        raise ValueError('generated helper still touches c->f_')
    return joined + '\n'


MACROS = r'''
/* ---- qlocals v2: register/flag locals ---- */
#define XQ_SAVE_M(m) do { if ((m) & 1u) c->r[0] = q0; if ((m) & 2u) c->r[1] = q1; if ((m) & 4u) c->r[2] = q2; if ((m) & 8u) c->r[3] = q3; \
    if ((m) & 16u) c->r[4] = q4; if ((m) & 32u) c->r[5] = q5; if ((m) & 64u) c->r[6] = q6; if ((m) & 128u) c->r[7] = q7; } while (0)
#define XQ_LOAD() do { q0 = c->r[0]; q1 = c->r[1]; q2 = c->r[2]; q3 = c->r[3]; q4 = c->r[4]; q5 = c->r[5]; q6 = c->r[6]; q7 = c->r[7]; } while (0)
#define XQF_SAVE() do { c->f_kind = qf.f_kind; c->f_op1 = qf.f_op1; c->f_op2 = qf.f_op2; c->f_res = qf.f_res; c->f_bits = qf.f_bits; \
    c->f_cf_override = qf.f_cf_override; c->f_cf = qf.f_cf; c->f_of_override = qf.f_of_override; c->f_of = qf.f_of; } while (0)
#define XQF_LOAD() do { qf.f_kind = c->f_kind; qf.f_op1 = c->f_op1; qf.f_op2 = c->f_op2; qf.f_res = c->f_res; qf.f_bits = c->f_bits; \
    qf.f_cf_override = c->f_cf_override; qf.f_cf = c->f_cf; qf.f_of_override = c->f_of_override; qf.f_of = c->f_of; } while (0)
/* statement barrier: m = registers to store; fl bit 0 = store flags before, bit 1 = reload flags after.
 * Returns always publish flags: the base code can leave a callee's flags in c->f_* for its caller. */
#define XQ_WRAPV_M(m, fl, call) do { XQ_SAVE_M(m); if ((fl) & 1) XQF_SAVE(); call; XQ_LOAD(); if ((fl) & 2) XQF_LOAD(); } while (0)
#define XQ_WRAP_M(m, fl, call) ({ XQ_SAVE_M(m); if ((fl) & 1) XQF_SAVE(); __auto_type xq_t_ = (call); XQ_LOAD(); if ((fl) & 2) XQF_LOAD(); xq_t_; })
#define XQ_RET_M(m) do { XQ_SAVE_M(m); XQF_SAVE(); return; } while (0)
#define XQ_R16(i)    (*(uint16_t *)&q##i)
#define XQ_R8L(i)    (*(uint8_t  *)&q##i)
#define XQ_R8H(i)    (*((uint8_t *)&q##i + 1))
#define XQ_PUSH32(v) do { uint32_t v__ = (uint32_t)(v); q4 -= 4; X_W32(q4) = v__; } while (0)
#define XQ_POP32()   ({ uint32_t v__ = X_M32(q4); q4 += 4; v__; })
#define XQ_PREEMPT() do { if (--c->preempt <= 0) { XQ_SAVE_M(255u); xv_preempt(c); XQ_LOAD(); } } while (0)
'''


# ----------------------------------------------------------------------------------------------
def match_paren(s, i):
    depth = 0
    while i < len(s):
        ch = s[i]
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        elif ch == '"':
            i += 1
            while s[i] != '"':
                i += 2 if s[i] == '\\' else 1
        elif ch == "'":
            i += 1
            while s[i] != "'":
                i += 2 if s[i] == '\\' else 1
        i += 1
    raise ValueError('unbalanced')


def blank_comments(s):
    s = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), s, flags=re.S)
    return re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), s)


def blank_for_calls(s):
    s = blank_comments(s)
    s = re.sub(r'(?m)^[ \t]*#[^\n]*', lambda m: ' ' * len(m.group(0)), s)
    return re.sub(r'\bextern\b[^;{}]*;', lambda m: ' ' * len(m.group(0)), s)


def classify(name):
    if name in KEYWORDS or name in SAFE or name.startswith(SAFE_PREFIX):
        return 'safe'
    return 'barrier'


def _closes_control(prev):
    depth = 0
    for k in range(len(prev) - 1, -1, -1):
        ch = prev[k]
        if ch == ')':
            depth += 1
        elif ch == '(':
            depth -= 1
            if depth == 0:
                return re.search(r'\b(if|while|for|switch)\s*$', prev[:k]) is not None
    return False


def wrap_barriers(body, stats, barriers):
    """Replace barrier calls by XQ_WRAPV_ID<n>( / XQ_WRAP_ID<n>( placeholders; record kind."""
    scan = blank_for_calls(body)
    out, pos, i = [], 0, 0
    while True:
        m = CALL_RE.search(scan, i)
        if not m:
            break
        name = m.group(1)
        if classify(name) == 'safe':
            i = m.end(); continue
        before = scan[max(0, m.start() - 40):m.start()]
        if re.search(r'\b(void|int|unsigned|uint32_t|uint64_t|double|float|char|xctx)\s*\**\s*$', before):
            i = m.end(); continue
        start = m.start()
        end = match_paren(scan, m.end() - 1)
        prev = scan[:start].rstrip()
        nxt = scan[end:].lstrip()
        is_stmt = nxt.startswith(';') and (not prev or prev[-1] in ';{}:' or prev.endswith('else') or
                                           (prev[-1] == ')' and _closes_control(prev)))
        n = len(barriers)
        guest = GUEST_CALL.fullmatch(name) is not None
        barriers.append({'stmt': is_stmt, 'guest': guest, 'name': name})
        out.append(body[pos:start])
        out.append(f'XQ_{"WRAPV" if is_stmt else "WRAP"}_ID{n}({body[start:end]})')
        stats['barrier:' + ('f_*' if re.fullmatch(r'f_[0-9A-F]{8}', name) else name)] += 1
        pos = i = end
    out.append(body[pos:])
    return ''.join(out)


ASSIGN_RE = re.compile(r'\bq([0-7])\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)|\+\+\s*q([0-7])\b|--\s*q([0-7])\b|\bq([0-7])\s*(?:\+\+|--)'
                       r'|\bXQ_R(?:16|8L|8H)\(([0-7])\)\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)|\b(XQ_PUSH32|XQ_POP32)\(|&q([0-7])\b')
EVENT_RE = re.compile(r'(?m)^(?P<label>[A-Za-z_]\w*):(?!:)|\bgoto\s+(?P<goto>[A-Za-z_]\w*)\s*;|\bXQ_(?P<wk>WRAPV|WRAP)_ID(?P<wid>\d+)\('
                      r'|\bXQ_RET_ID(?P<rid>\d+)\b|(?P<brace>[{}])|^[ \t]*#[ \t]*(?P<pp>if|ifdef|ifndef|elif|else|endif)\b')


def dirty_masks(rest, n_barriers, n_returns):
    """Forward 'may differ from c->r' dataflow.  Returns (barrier masks, return masks)."""
    scan = blank_comments(rest)
    events = []
    for m in ASSIGN_RE.finditer(scan):
        if m.group(6):
            regs = 1 << 4
        else:
            g = next(x for x in (m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(7)) if x is not None)
            regs = 1 << int(g)
        events.append((m.start(), 'assign', regs))
    for m in EVENT_RE.finditer(scan):
        if m.group('label'):
            if m.group('label') in ('default',):
                continue
            events.append((m.start(), 'label', m.group('label')))
        elif m.group('goto'):
            events.append((m.start(), 'goto', m.group('goto')))
        elif m.group('wid'):
            events.append((m.start(), 'barrier', (int(m.group('wid')), m.group('wk'))))
        elif m.group('rid'):
            events.append((m.start(), 'return', int(m.group('rid'))))
        elif m.group('brace'):
            events.append((m.start(), m.group('brace'), None))
        elif m.group('pp'):
            events.append((m.start(), 'pp', m.group('pp')))
    events.sort(key=lambda e: (e[0], 0 if e[1] in ('label',) else 1))
    labels = [e[2] for e in events if e[1] == 'label']
    if len(set(labels)) != len(labels):
        raise ValueError('duplicate label')

    def unconditional_stmt(pos, cond_stack):
        """A statement whose enclosing blocks are all unconditional plain blocks and that is not
        governed by a braceless if/else/while/for."""
        if any(cond_stack):
            return False
        prev = scan[:pos].rstrip()
        if not prev:
            return True
        if prev.endswith('else'):
            return False
        if prev[-1] == ')':
            return not _closes_control(prev)
        return prev[-1] in ';{}:'

    def last_is_jump(pos):
        """True when the code right before label position pos ends in an unconditional goto/return at depth 0
        with no preprocessor line in between (then there is no fallthrough edge)."""
        prev = scan[:pos].rstrip()
        lines = prev.split('\n')
        last = lines[-1] if lines else ''
        if re.match(r'^\s*#', last):
            return False
        return re.match(r'^    (goto [A-Za-z_]\w*;|XQ_RET_ID\d+;)\s*$', last) is not None

    in_state = collections.defaultdict(int)
    in_state['<entry>'] = 0
    changed = True
    bmask = [0] * n_barriers
    rmask = [0] * n_returns
    iterations = 0
    while changed:
        iterations += 1
        if iterations > 200:
            raise ValueError('dataflow did not converge')
        changed = False
        state = 0
        cond_stack = []
        pp_stack = []
        seen_labels = set()
        for pos, kind, val in events:
            if kind == 'label':
                fall = 0 if last_is_jump(pos) else state
                state = fall | in_state[val]
                seen_labels.add(val)
            elif kind == 'assign':
                state |= val
            elif kind == 'goto':
                new = in_state[val] | state
                if new != in_state[val]:
                    in_state[val] = new; changed = True
            elif kind == 'barrier':
                bid, wk = val
                if bmask[bid] | state != bmask[bid]:
                    bmask[bid] |= state
                if wk == 'WRAPV' and unconditional_stmt(pos, cond_stack) and not pp_stack:
                    state = 0
            elif kind == 'return':
                rmask[val] |= state
            elif kind == '{':
                prev = scan[:pos].rstrip()
                cond = (not prev) is False and (prev.endswith(('else', 'do')) or prev[-1] == '=' or
                                                (prev[-1] == ')' and _closes_control(prev)) or
                                                prev[-1] not in ';{}:')
                cond_stack.append(cond)
            elif kind == '}':
                if cond_stack: cond_stack.pop()
            elif kind == 'pp':
                if val in ('if', 'ifdef', 'ifndef'):
                    pp_stack.append([state, state])      # [state at #if, union of finished branches]
                elif val in ('elif', 'else'):
                    top = pp_stack[-1]; top[1] |= state; state = top[0]
                elif val == 'endif':
                    top = pp_stack.pop(); state |= top[1] | top[0]
    return bmask, rmask


def transform_function(name, body, stats):
    bad = UNSUPPORTED_ANY.search(body)
    if bad:
        stats['excluded_any:' + bad.group(0)[:24]] += 1
        return None
    m = LABEL_LINE_RE.search(body)
    if not m:
        stats['excluded_nolabel'] += 1
        return None
    prologue, rest = body[:m.start()], body[m.start():]
    bad = UNSUPPORTED_REST.search(rest)
    if bad:
        stats['excluded_rest:' + bad.group(0)[:24]] += 1
        return None
    rest = re.sub(r'c->r\[([0-7])\]', r'q\1', rest)
    rest = re.sub(r'\bX_R(16|8L|8H)\(', r'XQ_R\1(', rest)
    rest = re.sub(r'\bX_PUSH32\(', 'XQ_PUSH32(', rest)
    rest = re.sub(r'\bX_POP32\(\)', 'XQ_POP32()', rest)
    rest = re.sub(r'\bX_PREEMPT\(\)', 'XQ_PREEMPT()', rest)
    if re.search(r'XQ_R(16|8L|8H)\((?![0-7]\))', rest):
        stats['excluded_regindex'] += 1
        return None
    if re.search(r'\bc\s*->\s*r\b', rest):
        stats['excluded_residual_r'] += 1
        return None
    # flags
    rest = re.sub(r'\bc->f_', 'qf.f_', rest)
    rest = re.sub(r'\bX_FLAGS_C\(', 'XQF_FLAGS_C(&qf, ', rest)
    rest = re.sub(r'\bX_FLAGS\(', 'XQF_FLAGS(&qf, ', rest)
    for h in FLAG_HELPERS:
        rest = re.sub(r'\b' + h + r'\(c\)', f'xq_{h}(c, &qf)', rest)
        rest = re.sub(r'\b' + h + r'\(c\s*,', f'xq_{h}(c, &qf,', rest)
    if re.search(r'\b(' + '|'.join(FLAG_HELPERS) + r')\s*\(', blank_comments(rest)):
        stats['excluded_flag_helper_form'] += 1
        return None
    barriers = []
    rest = wrap_barriers(rest, stats, barriers)
    n_ret = 0
    def ret(_m):
        nonlocal n_ret
        n_ret += 1
        return f'XQ_RET_ID{n_ret - 1};'
    rest = re.sub(r'\breturn;', ret, rest)
    try:
        bmask, rmask = dirty_masks(rest, len(barriers), n_ret)
    except ValueError as e:
        stats['excluded_dataflow:' + str(e)] += 1
        return None
    def fill_b(m):
        bid = int(m.group(2)); b = barriers[bid]
        fl = GUEST_FLAGS if b['guest'] else 3
        stats['flush_regs'] += bin(bmask[bid]).count('1')
        stats['flush_sites'] += 1
        return f'XQ_{m.group(1)}_M({bmask[bid]}u, {fl}, '
    rest = re.sub(r'XQ_(WRAPV|WRAP)_ID(\d+)\(', fill_b, rest)
    rest = re.sub(r'XQ_RET_ID(\d+)', lambda m: f'XQ_RET_M({rmask[int(m.group(1))]}u)', rest)
    decl = ('    uint32_t q0 = c->r[0], q1 = c->r[1], q2 = c->r[2], q3 = c->r[3], q4 = c->r[4], q5 = c->r[5], q6 = c->r[6], q7 = c->r[7];\n'
            '    xqf qf = { ' + ', '.join(f'c->{f}' for f in FLAG_FIELDS) + ' };\n')
    # Locals are declared and loaded at the very top: prologue hook code can jump straight into the body
    # (f_00056670's worker-query success path does `goto L_000566DE`), which would otherwise skip their
    # initialisation.  The prologue itself stays untransformed (it uses c->r directly), so every entry from
    # the prologue into the body - its fallthrough and each prologue goto - reloads the locals.
    reload = 'XQ_LOAD(); XQF_LOAD();'
    prologue = re.sub(r'\bgoto\s+((?:L_|M_)\w+)\s*;', lambda m: '{ ' + reload + ' goto ' + m.group(1) + '; }', prologue)
    stats['transformed'] += 1
    return decl + prologue + '    ' + reload + '\n' + rest


def transform(text, stats, helpers, only=None):
    heads = list(FUNC_RE.finditer(text))
    out, pos = [], 0
    for h in heads:
        name = h.group(1)
        bstart = h.end()
        bend = text.index('\n}\n', bstart) + 1
        body = text[bstart:bend]
        new = None if (only is not None and name not in only) else transform_function(name, body, stats)
        out.append(text[pos:bstart])
        out.append(new if new is not None else body)
        pos = bend
    out.append(text[pos:])
    result = ''.join(out)
    first = heads[0].start() if heads else 0
    return result[:first] + helpers + MACROS + result[first:]


def load_helpers(recomp_dir):
    t = open(f'{recomp_dir}/xv_x86rt.h').read() + '\n' + open(f'{recomp_dir}/xv_x87reg.h').read()
    if re.search(r'\bxqfp\b|\bxqf\b', t):
        raise ValueError('runtime headers already use xqf/xqfp names')
    return build_helpers(t)


if __name__ == '__main__':
    recomp, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    stats = collections.Counter()
    helpers = load_helpers(recomp)
    open(dst, 'w').write(transform(open(src).read(), stats, helpers))
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1])[:40]:
        print(f'{v:8d} {k}', file=sys.stderr)
