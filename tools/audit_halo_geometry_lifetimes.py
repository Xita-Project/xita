#!/usr/bin/env python3
"""Audit selected Halo 3925 geometry lifetimes using an owned XBE.

Generated comments nominate direct-call sites; independent x86 decoding verifies
them and deduplicates overlapping lifted bodies. This is a bounded inventory,
not a proof that every payload writer has been found. Outputs metadata only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.xita_recomp import Image
from iced_x86 import Decoder, InstructionInfoFactory, Mnemonic, OpAccess, OpKind, Register

XBE_SHA256 = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
SPANS = (
    ('bsp_drain', 0x337f0, 0x33855),
    ('bsp_register', 0x33860, 0x338c4),
    ('model_drain', 0x338d0, 0x33921),
    ('model_register', 0x33930, 0x33989),
    ('vertex_pool_lock', 0x7a630, 0x7a6b0),
    ('vertex_pool_reserve', 0x7a6d0, 0x7a73a),
    ('index_pool_reserve', 0x7a740, 0x7a7ad),
    ('pool_release', 0x7a7b0, 0x7a80d),
    # End before the jump table at 0x7a918; those bytes are not instructions.
    ('pool_create', 0x7a810, 0x7a915),
    ('index_pool_pointer', 0x7a9d0, 0x7aa05),
)
TARGETS = {lo: name for name, lo, _ in SPANS}
TARGETS.update({0x184850: 'Resource_BlockUntilNotBusy', 0x1849d0: 'Resource_Release',
                0x184ab0: 'Resource_Register', 0x185800: 'CreateIndexBuffer',
                0x185870: 'CreateVertexBuffer', 0x1858d0: 'VertexBuffer_Lock'})
CALL_SITES = {
    0x3555f: 0x33860, 0x35387: 0x337f0, 0x58e8d: 0x337f0,
    0x3547e: 0x33930, 0x58471: 0x338d0,
    0x3380f: 0x184850, 0x3383a: 0x184850,
    0x33882: 0x184ab0, 0x338b2: 0x184ab0,
    0x338ea: 0x184850, 0x3390f: 0x184850, 0x33952: 0x184ab0,
    0x7a69d: 0x1858d0, 0x7a829: 0x185800,
    0x7a89d: 0x185870, 0x7a8f4: 0x185870,
    0x7a7c7: 0x1849d0, 0x7a7e4: 0x1849d0, 0x7a7fd: 0x1849d0,
}
WRITE_ACCESS = {OpAccess.WRITE, OpAccess.COND_WRITE, OpAccess.READ_WRITE,
                OpAccess.READ_COND_WRITE}
REGISTER_NAMES = {value: name.lower() for name, value in vars(Register).items()
                  if name.isupper() and isinstance(value, int)}
FUNCTION = re.compile(r'^void f_([0-9A-Fa-f]{8})\(')
CALL_COMMENT = re.compile(r'/\*\s+([0-9A-Fa-f]{8})\s+call\s+([0-9A-Fa-f]+)h\s*\*/')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify_image(path):
    img = Image(str(path))
    require(hashlib.sha256(img.data).hexdigest() == XBE_SHA256,
            'unsupported executable revision')
    return img


def decode_span(img, lo, hi):
    raw = img.bytes_at(lo, hi - lo)
    instructions = list(Decoder(32, raw, ip=lo))
    require(len(raw) == hi - lo and instructions and
            not any(i.is_invalid for i in instructions) and
            instructions[-1].next_ip == hi and instructions[-1].mnemonic == Mnemonic.RET,
            'invalid instruction interval at ' + hex(lo))
    return raw, instructions


def verify_call(img, pc, target):
    ins = Decoder(32, img.bytes_at(pc, 15), ip=pc).decode()
    require(ins.mnemonic == Mnemonic.CALL and ins.op0_kind == OpKind.NEAR_BRANCH32 and
            ins.near_branch_target == target, 'direct-call mismatch at ' + hex(pc))


def collect_calls(img, files):
    sites = {target: {} for target in TARGETS}
    for path in files:
        owner = None
        with path.open() as source:
            for line in source:
                fn = FUNCTION.match(line)
                if fn:
                    owner = int(fn[1], 16)
                comment = CALL_COMMENT.search(line)
                if not comment:
                    continue
                pc, target = (int(x, 16) for x in comment.groups())
                if target not in sites:
                    continue
                require(owner is not None, 'call comment outside a lifted function')
                verify_call(img, pc, target)
                sites[target].setdefault(pc, set()).add(owner)
    return {hex(target): dict(name=TARGETS[target], unique_sites=len(rows), sites=[
        dict(pc=hex(pc), lifted_owners=[hex(o) for o in sorted(owners)])
        for pc, owners in sorted(rows.items())]) for target, rows in sorted(sites.items())}


def audit(img, files):
    factory = InstructionInfoFactory()
    intervals = []
    decoded = {}
    for name, lo, hi in SPANS:
        raw, instructions = decode_span(img, lo, hi)
        writes, calls = [], []
        for ins in instructions:
            decoded[ins.ip] = ins
            if ins.mnemonic == Mnemonic.CALL:
                require(ins.op0_kind == OpKind.NEAR_BRANCH32, 'unexpected indirect child call')
                calls.append(dict(pc=hex(ins.ip), target=hex(ins.near_branch_target)))
            for mem in factory.info(ins).used_memory():
                if mem.access in WRITE_ACCESS:
                    writes.append(dict(pc=hex(ins.ip), base=REGISTER_NAMES[mem.base],
                                       index=REGISTER_NAMES[mem.index], scale=mem.scale,
                                       displacement=hex(mem.displacement), memory_size=mem.memory_size))
        intervals.append(dict(name=name, start=hex(lo), end_exclusive=hex(hi),
                              sha256=hashlib.sha256(raw).hexdigest(),
                              instructions=len(instructions), calls=calls, writes=writes))
    for pc, target in CALL_SITES.items():
        verify_call(img, pc, target)

    def operand(pc, mnemonic, dest, base, displacement, memory_operand):
        ins = decoded[pc]
        require(ins.mnemonic == mnemonic and ins.memory_base == base and
                ins.memory_index == Register.NONE and ins.memory_displacement == displacement and
                ins.op_kind(memory_operand) == OpKind.MEMORY and
                ins.op_register(1 - memory_operand) == dest,
                'pointer data-flow signature changed at ' + hex(pc))

    operand(0x7a682, Mnemonic.LEA, Register.EDI, Register.EAX, 12, 1)
    operand(0x7a6a6, Mnemonic.MOV, Register.EAX, Register.EDI, 0, 1)
    operand(0x7a9dd, Mnemonic.MOV, Register.EDX, Register.EDX, 4, 1)
    operand(0x7a9f7, Mnemonic.MOV, Register.ECX, Register.EAX, 8, 0)
    operand(0x7aa01, Mnemonic.MOV, Register.EAX, Register.EAX, 8, 1)
    strides = struct.unpack('<12H', img.bytes_at(0x1e0ab4, 24))
    require(strides == (56, 32, 20, 8, 68, 32, 24, 36, 20, 16, 16, 8), 'stride table changed')
    files = sorted(files)
    require(files, 'no generated code_*.c files found')
    calls = collect_calls(img, files)
    # Ensure the supplied stage contains the selected evidence, while making no
    # claim that comments enumerate indirect calls or unlifted code.
    for pc, target in CALL_SITES.items():
        require(hex(pc) in {r['pc'] for r in calls[hex(target)]['sites']},
                'selected call missing from generated stage at ' + hex(pc))
    return dict(result='PASS', xbe_sha256=XBE_SHA256, intervals=intervals,
                verified_call_sites={hex(k): hex(v) for k, v in CALL_SITES.items()},
                strides=strides, generated_files=len(files), direct_calls=calls,
                findings=[
                    'Vertex lock passes record+12 as its output-pointer address and returns that retained pointer.',
                    'Vertex reserve writes format/start/count but does not clear the prior record+12 pointer.',
                    'Index pointer acquisition reads resource Data directly, stores record+8 and makes no lock call.',
                    'BSP and model registration use distinct resource tables; these calls do not prove payload immutability.',
                ], limits=[
                    'Only the listed original intervals and nominated direct calls are checked.',
                    'Memory operands are syntactic metadata, not a complete alias or payload-writer analysis.',
                    'Generated call counts are deduplicated code sites, not runtime frequencies.',
                    'No game data, runtime invalidation policy or FPS result is produced.',
                ])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe', required=True, type=Path)
    p.add_argument('--generated', required=True, type=Path, help='directory containing code_*.c')
    p.add_argument('--output', required=True, type=Path)
    args = p.parse_args()
    result = audit(verify_image(args.xbe), args.generated.glob('code_*.c'))
    with args.output.open('x') as out:
        json.dump(result, out, indent=2)
        out.write('\n')
    print(json.dumps(dict(result='PASS', intervals=len(result['intervals']),
                          selected_calls=len(CALL_SITES), generated_files=result['generated_files'],
                          vertex_lock_sites=result['direct_calls']['0x1858d0']['unique_sites'])))


if __name__ == '__main__':
    main()
