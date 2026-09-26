#!/usr/bin/env python3
"""Check hook drift/idempotence and compile all native/world-run branches."""
from pathlib import Path
import subprocess
import tempfile
from patch_native_object_query import BODY, ENTRY, HOOK, REFERENCE, patch

original = '#if XV_QUERY_OBJECT_SPACE\n' + ENTRY + '    c->entered++;\n' + BODY + '\n}\n#endif\n'
hooked = patch(original)
assert patch(hooked) == hooked
assert hooked.replace(REFERENCE, '').replace(HOOK, BODY) == original
for invalid in (original.replace('nq_run_impl(c,0)', 'nq_run_impl(c,1)'),
                original + original, hooked + hooked, hooked.replace(HOOK, BODY)):
    try:
        patch(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('drift was accepted')

prefix = '''
#include <assert.h>
typedef struct { int entered, native, object, generic; } xctx;
static void nq_run_impl(xctx *c, unsigned world) { assert(world == 0); c->object++; }
static void query_fused_172c95_171f94(xctx *c) { c->generic++; }
void xv_native_4b9d0_object_query(xctx *c, void (*reference)(xctx *))
{ c->native++; reference(c); }
'''
suffix = '''
int main(void) {
    xctx c = {0}; nq_query_at_17301b(&c);
    assert(c.entered == 1 && c.native == XV_NATIVE_4B9D0);
    assert(c.object == XV_QUERY_WORLD_RUN && c.generic == !XV_QUERY_WORLD_RUN);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xita-object-hook-') as tmp:
    root = Path(tmp)
    source = root / 'hook.c'
    source.write_text(prefix + hooked + suffix)
    for native in (0, 1):
        for world in (0, 1):
            exe = root / f'hook-{native}-{world}'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-Wno-unused-function',
                            '-DXV_QUERY_OBJECT_SPACE=1', f'-DXV_NATIVE_4B9D0={native}',
                            f'-DXV_QUERY_WORLD_RUN={world}', str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
print('Object-query hook: four compiled routes, drift rejection and idempotence passed')
