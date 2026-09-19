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


# Existing six-bucket emitted body, with only phase-index placement normalized.
DETAIL_BASE_SHA256 = '60f91933239e90b63285239167060ab19f4cf6c86f5c2caec3fb8ecc58177036'
DETAIL_CUTS = {0x5D4B0: 1, 0x5D4E7: 2, 0x5D4EC: 3, 0x5D4F1: 4, 0x5D4FB: 5}


def strip_detail(body):
    return re.sub(r'^#if defined\(XV_SCENE_BUCKET0_DETAIL\) && XV_SCENE_BUCKET0_DETAIL\n.*?^#endif\n', '', body, flags=re.M | re.S)


def detail_hook(body):
    if not __debug__:
        raise RuntimeError('Run without Python -O')
    canonical = re.sub(r'^    XV_PHASE_SCOPE\(c, \d+u\);\n', '', body, flags=re.M).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != DETAIL_BASE_SHA256:
        raise ValueError('primary 5D410 main-partition body drift')
    original = body
    for pc, bucket in DETAIL_CUTS.items():
        needle = '    /* ' + f'{pc:08X}' + '  '
        if body.count(needle) != 1:
            raise ValueError('primary 5D410 detail frontier drift')
        marker = '    /* XV_SCENE_BUCKET0_DETAIL_SCOPE: shared original scene token */\n' if bucket == 1 else ''
        step = ('#if defined(XV_SCENE_BUCKET0_DETAIL) && XV_SCENE_BUCKET0_DETAIL\n' + marker +
                '    { extern void xv_scene_bucket0_step(uint64_t *, void *, unsigned);\n' +
                f'      xv_scene_bucket0_step(&xv_scene_partition_scope_, c, {bucket}u); ' + '}\n' +
                '#endif\n')
        body = body.replace(needle, step + needle)
    if strip_detail(body) != original:
        raise ValueError('scene detail changes original body or main scopes')
    return body


# Second scene section: boundaries between ordered model/callback passes.
BUCKET1_CUTS = {0x5D517: 2, 0x5D5C3: 2, 0x5D5FC: 2, 0x5D693: 2,
                0x5D698: 2, 0x5D6F2: 2, 0x5D72F: 1, 0x5D759: 1,
                0x5D77F: 1, 0x5D7B0: 1, 0x5D7DB: 2}


def strip_bucket1(body):
    return re.sub(r'^#if defined\(XV_SCENE_BUCKET1_DETAIL\) && XV_SCENE_BUCKET1_DETAIL\n.*?^#endif\n', '', body, flags=re.M | re.S)


def bucket1_hook(body):
    if not __debug__:
        raise RuntimeError('Run without Python -O')
    if strip_bucket1(body) != body:
        raise ValueError('bucket1 observer already installed')
    canonical = re.sub(r'^    XV_PHASE_SCOPE\(c, \d+u\);\n', '', strip(strip_detail(body)), flags=re.M).rstrip()
    if hashlib.sha256(canonical.encode()).hexdigest() != BODY_SHA256:
        raise ValueError('primary 5D410 instruction/callback body drift')
    if body.count('xv_scene_partition_begin(&xv_scene_partition_scope_, c)') != 1:
        raise ValueError('bucket1 requires main scene observer')
    original = body
    for bucket, (pc, count) in enumerate(BUCKET1_CUTS.items(), 1):
        needle = '    /* ' + f'{pc:08X}' + '  '
        if body.count(needle) != count:
            raise ValueError(f'bucket1 frontier {pc:08X} drift')
        marker = '    /* XV_SCENE_BUCKET1_DETAIL_SCOPE: ordered callback passes */\n' if bucket == 7 else ''
        step = ('#if defined(XV_SCENE_BUCKET1_DETAIL) && XV_SCENE_BUCKET1_DETAIL\n' + marker +
                '    { extern void xv_scene_bucket1_step(uint64_t *, void *, unsigned);\n' +
                f'      xv_scene_bucket1_step(&xv_scene_partition_scope_, c, {bucket}u); ' + '}\n' +
                '#endif\n')
        body = body.replace(needle, step + needle)
    if strip_bucket1(body) != original:
        raise ValueError('bucket1 observer changes retained body')
    return body
