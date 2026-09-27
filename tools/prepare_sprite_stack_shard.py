#!/usr/bin/env python3
"""Stage the opt-in sprite stack experiment without changing retained inputs."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--shard',type=Path,required=True)
p.add_argument('--reference',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args();s=a.shard.read_text();r=a.reference.read_text()
sig='void f_000597CB(xctx *restrict c)\n';start=s.index(sig);end=s.index('\nvoid f_',start+len(sig));body=s[start+len(sig):end]
assert hashlib.sha256(body.encode()).hexdigest()=='91b7b7807aadde2948e03f00ca66739c7a56e6a0ee2ef90a5abc884e9112df74'
assert r.split('void original(xctx *restrict c)\n',1)[1].split('\nvoid candidate(',1)[0].rstrip()==body.rstrip()
r=r.replace('original(', 'sprite_stack_original(').replace('void sprite_stack_original(', 'static void sprite_stack_original(').replace('void candidate(', 'void f_000597CB(')
r+='\n#undef SPRITE_REFRESH\n#undef SPRITE_PTR\n#undef SPRITE_LOAD\n#undef SPRITE_STORE\n#undef SPRITE_PREEMPT\n'
replacement='#if defined(XV_SPRITE_STACK_PAGE) && XV_SPRITE_STACK_PAGE && !defined(XV_CHECK_GUEST_ADDRESS)\n'+r+'\n#else\n'+s[start:end]+'\n#endif\n'
result=s[:start]+replacement+s[end:];assert result.replace(replacement,s[start:end],1)==s
with a.out.open('x') as f:f.write(result)
a.out.with_suffix('.audit.json').write_text(json.dumps({'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'reference_sha256':hashlib.sha256(a.reference.read_bytes()).hexdigest(),'outside_target_restores_exactly':True,'default_enabled':False,'checked_address_fallback':True},indent=2)+'\n')
