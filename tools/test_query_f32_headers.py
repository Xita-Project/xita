#!/usr/bin/env python3
"""Check the bounded private-header transform without owned guest code."""
from pathlib import Path
import tempfile
from query_f32_primitives import HEADERS, INCLUDES, INCLUDE_DIR, generate, specialize_unit

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not __debug__: raise SystemExit('Refusing optimized Python')
    with tempfile.TemporaryDirectory(prefix='query-f32-headers-') as directory:
        root = Path(directory)
        for name in HEADERS:
            path = root / name;path.parent.mkdir(parents=True,exist_ok=True)
            if name == 'xv_recomp_protos.h':
                path.write_text('#include "xv_x86rt.h"\n#include "xv_phase.h"\n#include "kernel/xk_object_jobs.h"\n')
            else:path.write_bytes((ROOT/'recomp'/name).read_bytes())
        before={name:(root/name).read_bytes() for name in HEADERS}
        query=''.join('#include "'+name+'"\n' for name in INCLUDES)+'#undef X_G\n'
        off=specialize_unit(query,False);assert off==query
        text,outputs,canonical=generate(root,query)
        assert len(outputs)==8 and set(canonical)==set(HEADERS)
        assert text.count(INCLUDE_DIR+'/')==4
        for name in HEADERS:
            output=outputs[INCLUDE_DIR+'/'+name].encode()
            if name=='xv_x86rt.h':
                assert output.replace(b'static inline __attribute__((always_inline)) double x87_load_f32(',b'static inline double x87_load_f32(').replace(b'static inline __attribute__((always_inline)) void x87_store_f32(',b'static inline void x87_store_f32(')==before[name]
            else:assert output==before[name]
        poison=root/INCLUDE_DIR/INCLUDE_DIR/'poison.h';poison.parent.mkdir(parents=True);poison.write_text('#error recursive copy\n')
        assert generate(root,query)==(text,outputs,canonical)
        assert all((root/name).read_bytes()==data for name,data in before.items())
        def reject(value):
            path=root/'kernel/xk_light_census.h';original=path.read_bytes();path.write_bytes(original+value)
            try:
                try:generate(root,query)
                except ValueError:pass
                else:raise AssertionError('unreviewed include accepted: '+repr(value))
            finally:path.write_bytes(original)
        for value in (b'\n#include "unknown.h"\n',b'\n#include "../../escape.h"\n',
                      b'\n#include SOME_HEADER\n',b'\n#include "/absolute.h"\n'):
            reject(value)
        (root/'xv_phase.h').unlink()
        try:generate(root,query)
        except FileNotFoundError:pass
        else:raise AssertionError('missing canonical header accepted')
    print('PASS bounded query header closure, unchanged canonical inputs, two attributes, no recursion, invalid includes')


if __name__=='__main__':main()
