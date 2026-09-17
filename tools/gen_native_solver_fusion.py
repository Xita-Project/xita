#!/usr/bin/env python3
"""Compose the qualified solver into the shared query/solver generation step.

The query generator owns code_028.c publication, so two Make jobs cannot race
to rewrite its caller. This module validates/reverses only its exact wrapper
and creates the separate solver unit from the owned executable. It embeds no
guest instruction bodies. The production entry point is
gen_native_query_fusion.py --solver-fusion 1.
"""
import json
from pathlib import Path
from types import SimpleNamespace

from tools import prototype_collision_solver as prototype

FEATURE = 'XV_NATIVE_SOLVER_FUSION'
FRAMES = 4
GUARD = f'#ifndef {FEATURE}\n#define {FEATURE} 0\n#endif\n'
CHECKS = (f'#if {FEATURE} != 0 && {FEATURE} != 1\n'
          f'#error "{FEATURE} must be 0 or 1"\n#endif\n'
          f'#if {FEATURE} && defined(XV_OBJECT_SOLVER_EXPERIMENT)\n'
          '#error "solver fusion must retain the actor transaction"\n#endif\n')
PREFIX = GUARD + CHECKS + f'#if {FEATURE}\n#include "xv_x86rt.h"\n' \
         'void ns_solver_at_172cb8(xctx *,unsigned);\n#endif\n'
CALL = '    /* 00172CB8  call 00170C10h */\n    X_PUSH32(0x172CBDu);\n    f_00170C10(c);'
SELECTED_CALL = CALL.replace('    f_00170C10(c);',
    f'#if {FEATURE}\n    ns_solver_at_172cb8(c,0x172cb8u);\n#else\n    f_00170C10(c);\n#endif')


def generic_caller(text):
    """Return the caller before our exact reversible transformation."""
    if FEATURE not in text:
        if 'ns_solver_at_172cb8' in text:
            raise ValueError('unexpected solver hook without its guard')
        return text
    if not text.startswith(PREFIX) or text.count(SELECTED_CALL) != 1:
        raise ValueError('solver fusion caller prefix/callsite drift')
    result = text[len(PREFIX):].replace(SELECTED_CALL, CALL, 1)
    if FEATURE in result or 'ns_solver_at_172cb8' in result:
        raise ValueError('unexpected additional solver hook')
    return result


def selected_caller(text):
    if text.count(CALL) != 1 or FEATURE in text or 'ns_solver_at_172cb8' in text:
        raise ValueError('expected exactly one unmodified 172CB8 call')
    result = PREFIX + text.replace(CALL, SELECTED_CALL, 1)
    if generic_caller(result) != text:
        raise ValueError('solver caller transformation is not reversible')
    return result


def generate_units(xbe, manifest, units, directory):
    if not __debug__:
        raise RuntimeError('Refusing optimized Python: solver validation requires assertions')
    directory = Path(directory)
    retained = directory / 'retained'
    (retained / 'recomp').mkdir(parents=True)
    for number in (0, 13, 16, 28):
        (retained / 'recomp' / f'code_{number:03d}.c').write_text(units[number])
    out = directory / 'emitted'
    out.mkdir()
    args = SimpleNamespace(xbe=Path(xbe), manifest=Path(manifest), retained=retained,
        out=out, frames=FRAMES, trace=False, inline_primitives=True,
        negative_control=None, fixture_runtime=False)
    prototype.generate(args)
    text = (out / 'candidate-private.c').read_text()
    prototype_guard = '#ifndef XV_SOLVER_FUSION_PROTOTYPE\n#define XV_SOLVER_FUSION_PROTOTYPE 0\n#endif\n'
    if text.count(prototype_guard) != 1:
        raise ValueError('qualified solver guard drift')
    text = text.replace(prototype_guard, '', 1)
    text = text.replace('XV_SOLVER_FUSION_PROTOTYPE', FEATURE)
    include = f'#include "{out / "solver-primitives-private.h"}"'
    if text.count(include) != 1:
        raise ValueError('qualified solver primitive header drift')
    text = text.replace(include, '#include "solver_primitives.h"', 1)
    # These declarations are unused in uninstrumented generated code. Keep the
    # qualification code itself untouched; no private fixture symbol is used.
    text = GUARD + CHECKS + f'#if {FEATURE}\n' + text + '\n#endif\n'
    outputs = {'solver_fusion.c': text,
               'solver_primitives.h': (out / 'solver-primitives-private.h').read_text()}
    contract = json.loads((out / 'generation.json').read_text())
    contract.update(feature=FEATURE, default=0, separate_translation_unit=True,
        caller='0x172cb8', actor_transaction_unchanged=True,
        generic_closure_retained=True, bounded_frames=FRAMES,
        profile='original pointer, site 4, original cleanup scope',
        native_stack='CE guest and object worker threads 512 KiB; bootstrap 2 MiB; full native high-water unmeasured',
        deployment_admitted=False)
    return outputs, contract
