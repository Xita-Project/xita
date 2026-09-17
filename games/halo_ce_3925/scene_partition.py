"""Passive primary-scene boundaries. No original instruction replacement."""
import hashlib
import re

BODY_SHA256 = '4aa4a3d56a471c986dd70995253b460b05da51d9458c693de55d6fca9fbb936b'
CUTS = {0x5D500: (1, 1), 0x5D7ED: (2, 2), 0x5D80A: (3, 2),
        0x5D8C1: (4, 2), 0x5D8DD: (5, 3)}
ENTRY = '''#if defined(XV_SCENE_PARTITION) && XV_SCENE_PARTITION
    /* XV_SCENE_PARTITION_SCOPE: primary 5D410 only */
    extern void xv_scene_partition_begin(uint64_t *, void *);
    extern void xv_scene_partition_step(uint64_t *, void *, unsigned);
    extern void xv_scene_partition_end(uint64_t *);
    uint64_t xv_scene_partition_scope_ __attribute__((cleanup(xv_scene_partition_end))) = 0;
    xv_scene_partition_begin(&xv_scene_partition_scope_, c);
#endif
'''


def strip(body):
    return re.sub(r'^#if defined\(XV_SCENE_PARTITION\) && XV_SCENE_PARTITION\n.*?^#endif\n', '', body, flags=re.M | re.S)


def hook(body):
    if not __debug__:
        raise RuntimeError('Run without Python -O')
    # Phase placement differs in retained stages; remove only its exact line
    # for canonical comparison, while preserving it byte-for-byte in output.
    canonical = re.sub(r'^    XV_PHASE_SCOPE\(c, \d+u\);\n', '', body, flags=re.M).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != BODY_SHA256:
        raise ValueError('primary 5D410 emitted instruction/callback boundary drift')
    original = body
    header = 'void f_0005D410(xctx *restrict c)\n{\n'
    if not body.startswith(header):
        raise ValueError('primary 5D410 entry drift')
    phase = re.match(re.escape(header) + r'(    XV_PHASE_SCOPE\(c, \d+u\);\n)', body)
    insertion = header + (phase[1] if phase else '')
    body = body.replace(insertion, insertion + ENTRY, 1)
    for pc, (bucket, count) in CUTS.items():
        needle = '    /* ' + f'{pc:08X}' + '  '
        if body.count(needle) != count:
            raise ValueError('primary 5D410 duplicated frontier drift')
        step = ('#if defined(XV_SCENE_PARTITION) && XV_SCENE_PARTITION\n'
                f'    xv_scene_partition_step(&xv_scene_partition_scope_, c, {bucket}u);\n'
                '#endif\n')
        body = body.replace(needle, step + needle)
    if strip(body) != original:
        raise ValueError('scene observer changes original instructions')
    return body
