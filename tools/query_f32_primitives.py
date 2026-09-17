"""Query-only private primitive headers; never rewrite shared runtime headers."""
from pathlib import Path, PurePosixPath
import posixpath
import re

HELPERS = ('x87_load_f32', 'x87_store_f32')
INCLUDE_DIR = 'query_f32_primitives'
INCLUDES = ('xv_recomp_protos.h', 'kernel/xk_collision_vertices.h',
            'kernel/xk_segment_sphere.h', 'kernel/xk_collision_traversal.h')
# Exact quoted-include closure, independent of generated files already present.
# Make tracks these same inputs and all eight corresponding private outputs.
HEADERS = ('xv_recomp_protos.h', 'xv_x86rt.h', 'xv_phase.h',
           'kernel/xk_object_jobs.h', 'kernel/xk_light_census.h',
           'kernel/xk_collision_vertices.h', 'kernel/xk_segment_sphere.h',
           'kernel/xk_collision_traversal.h')


def selective_header(text):
    original = text
    for helper in HELPERS:
        pattern = r'static inline ([A-Za-z_][A-Za-z_0-9 *]*\b' + helper + r'\()'
        text, count = re.subn(pattern, r'static inline __attribute__((always_inline)) \1', text)
        if count != 1:
            raise ValueError('unexpected primitive declaration: ' + helper)
    restored = text
    for helper in HELPERS:
        pattern = r'static inline __attribute__\(\(always_inline\)\) ([A-Za-z_][A-Za-z_0-9 *]*\b' + helper + r'\()'
        restored, count = re.subn(pattern, r'static inline \1', restored)
        if count != 1:
            raise ValueError('primitive change is not reversible: ' + helper)
    if restored != original:
        raise ValueError('private header changed more than the two attributes')
    if '#define X_G(a)      ((void *)(g_xram + g_xpt[' not in text:
        raise ValueError('global mapping definition drift')
    return text


def specialize_unit(text, enabled):
    include = '#include "xv_recomp_protos.h"'
    if text.count(include) != 1 or text.index(include) > text.index('#undef X_G'):
        raise ValueError('query must parse global memory helpers before captured roots')
    if INCLUDE_DIR in text:
        raise ValueError('query already specialized')
    if not enabled:
        return text
    # The original uses pragma once (file identity), not a named include guard.
    # Copy its tiny include tree so every transitive ../xv_x86rt.h resolves to
    # the same private file. All other headers remain byte-identical.
    for name in INCLUDES:
        old = '#include "' + name + '"'
        if text.count(old) != 1 or text.index(old) > text.index('#undef X_G'):
            raise ValueError('unexpected query include ordering: ' + name)
        text = text.replace(old, '#include "' + INCLUDE_DIR + '/' + name + '"', 1)
    return text


def generate(recomp_dir, query):
    """Validate the complete include closure before returning any output."""
    root = Path(recomp_dir).resolve()
    contents = {}
    for name in HEADERS:
        path = root / name
        if not path.resolve().is_relative_to(root):
            raise ValueError('query header escapes canonical root: ' + name)
        contents[name] = path.read_bytes().decode('utf-8')
    for name, text in contents.items():
        for directive in re.findall(r'^\s*#\s*include\b([^\n]*)', text, re.M):
            directive = directive.strip()
            if re.fullmatch(r'<[^>]+>', directive):
                continue
            quoted = re.fullmatch(r'"([^"]+)"', directive)
            if not quoted:
                raise ValueError('unreviewed query include directive: ' + name + ': ' + directive)
            include = quoted[1]
            relative = posixpath.normpath(str(PurePosixPath(name).parent / include))
            target = (root / relative).resolve()
            if relative not in contents or target != (root / relative).absolute():
                raise ValueError('unreviewed query header include: ' + name + ': ' + include)
    output = {INCLUDE_DIR + '/' + name: text for name, text in contents.items()}
    output[INCLUDE_DIR + '/xv_x86rt.h'] = selective_header(contents['xv_x86rt.h'])
    return specialize_unit(query, True), output, contents
