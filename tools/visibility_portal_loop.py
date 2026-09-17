"""Private, default-OFF ordered register/flag portal-loop prototype.

Only the exact retained integer interval is accepted. Original guest accesses,
child calls, recursion and budget frontiers stay ordered. Owned bodies are
generated into private evidence, never authored into this module.
"""
import hashlib
import re

FLAG='XV_NATIVE_VISIBILITY_PORTAL_LOOP'
PIN='6e0fa2503e0ac6e5ad87ac869b53a7ef472941dd8fa0cabe50c73181c09870d8'
FIELDS=('f_kind','f_op1','f_op2','f_res','f_bits','f_cf_override','f_cf','f_of_override','f_of')
CACHED=(0,1,2)

def primitive_defs(header):
    """Same integer helpers, with only their nine used fields in a local type."""
    first=header.index('static inline uint32_t xf_mask(')
    last=header.index('static inline uint32_t xf_eflags(',first)
    flags=header[first:last]
    shifts=[]
    for name,end in [('shl','shr'),('sar','rol')]:
        first=header.index('static inline T x_'+name+'##B(')
        last=header.index('static inline T x_'+end+'##B(',first)
        text=header[first:last].replace('\\\n','\n').rstrip()
        text=text.replace('int##B##_t','int32_t').replace('##B','32')
        text=re.sub(r'\bB\b','32',text);text=re.sub(r'\bT\b','uint32_t',text)
        shifts.append(text+'\n')
    raw=flags+''.join(shifts)
    # Check these narrow primitives against the actual reviewed shared header.
    digest=hashlib.sha256(raw.encode()).hexdigest()
    if digest!='7a2d75e972996e8651429786c5a5d708d3516b608c1be1605f60d5ca092ca60f':raise ValueError('integer primitive drift: '+digest)
    private=raw.replace('xctx','vp_flags')
    private=re.sub(r'\b(XF_[A-Z]+|xf_mask|xf_msb|x_shl32|x_sar32)\b',r'vp_\1',private)
    private=private.replace('X_FLAGS(', 'VPH_FLAGS(')
    return ('typedef struct { '+''.join('uint32_t '+f+';' for f in FIELDS)+' } vp_flags;\n'
      '#define VPH_FLAGS(kind,a,b,r,bits) do { c->f_kind=(kind); c->f_op1=(uint32_t)(a); c->f_op2=(uint32_t)(b); c->f_res=(uint32_t)(r); c->f_bits=(bits); c->f_cf_override=0; c->f_of_override=0; } while(0)\n'
      +private+'#undef VPH_FLAGS\n')

def transform(body,header):
    start=body.index('    /* 000533DA ')
    end=body.index('    /* 0005351A ',start)
    original=body[start:end]
    if hashlib.sha256(original.encode()).hexdigest()!=PIN:raise ValueError('portal interval drift')
    labels=re.findall(r'^(L_[0-9A-F]+):',original,re.M)
    for label in labels:
        if re.search(r'goto '+label+r'\b',body[:start]+body[end:]):raise ValueError('external interior entry')
    changed=original
    for i in CACHED:changed=changed.replace(f'c->r[{i}]',f'q{i}')
    for field in FIELDS:changed=changed.replace('c->'+field,'flags.'+field)
    changed=re.sub(r'\b(XF_[A-Z]+|x_shl32|x_sar32)\(c\b',r'\1(&flags',changed)
    changed=re.sub(r'\b(XF_[A-Z]+|x_shl32|x_sar32)\(&flags',r'vp_\1(&flags',changed)
    changed=changed.replace('X_FLAGS(','VP_FLAGS(').replace('X_R16(','VP_R16(').replace('X_R8L(','VP_R8L(')
    changed=changed.replace('X_PUSH32(','VP_PUSH32(').replace('X_PREEMPT();','VP_PREEMPT();')
    changed=changed.replace('goto L_0005351A;','goto VP_EXIT;')
    changed=re.sub(r'^(    )(f_[0-9A-F]+)\(c\);',r'\1VP_SAVE(); \2(c); VP_LOAD();',changed,flags=re.M)
    # Each explicit guest memory operation retains its source-order barrier.
    # Do not strengthen pointer/table immutability or speculate later reads.
    changed='\n'.join(('    __asm__ volatile("" ::: "memory");\n'+line
        if any(token in line for token in ('X_M','X_IMG','VP_PUSH32')) else line)
        for line in changed.splitlines())+'\n'
    save=' '.join(f'c->r[{i}]=q{i};' for i in CACHED)+' '+ ' '.join('c->'+f+'=flags.'+f+';' for f in FIELDS)
    load=' '.join(f'q{i}=c->r[{i}];' for i in CACHED)+' '+ ' '.join('flags.'+f+'=c->'+f+';' for f in FIELDS)
    macros='''#define VP_SAVE() do { @SAVE@ } while(0)
#define VP_LOAD() do { @LOAD@ } while(0)
#define VP_R16(i) (*(uint16_t *)&VP_Q##i)
#define VP_R8L(i) (*(uint8_t *)&VP_Q##i)
#define VP_PUSH32(v) do { uint32_t v__=(uint32_t)(v); VP_Q4-=4; X_M32(VP_Q4)=v__; } while(0)
#define VP_FLAGS(kind,a,b,r,bits) do { flags.f_kind=(kind); flags.f_op1=(uint32_t)(a); flags.f_op2=(uint32_t)(b); flags.f_res=(uint32_t)(r); flags.f_bits=(bits); flags.f_cf_override=0; flags.f_of_override=0; } while(0)
#define VP_PREEMPT() do { if(--c->preempt<=0) { VP_SAVE(); xv_preempt(c); VP_LOAD(); } } while(0)
'''.replace('@SAVE@',save).replace('@LOAD@',load)
    macros+=''.join('#define VP_Q'+str(i)+' '+(f'q{i}' if i in CACHED else f'(c->r[{i}])')+'\n' for i in range(8))
    native='{\n    uint32_t '+','.join(f'q{i}' for i in CACHED)+'; vp_flags flags;\n    VP_LOAD();\n'+changed+'VP_EXIT:\n    VP_SAVE();\n    goto L_0005351A;\n}\n'
    guarded='#if '+FLAG+'\n'+native+'#else\n'+original+'#endif\n'
    prefix='#ifndef '+FLAG+'\n#define '+FLAG+' 0\n#endif\n#if '+FLAG+' != 0 && '+FLAG+' != 1\n#error "'+FLAG+' must be 0 or 1"\n#endif\n'+macros
    return primitive_defs(header)+prefix+body[:start]+guarded+body[end:]
