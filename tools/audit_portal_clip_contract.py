#!/usr/bin/env python3
"""Pin the hot portal clip caller and report its proposed native data boundary.

No executable bytes or generated game bodies are written. This is a bounded
caller audit, not an exhaustive pointer/alias or scheduler-lifetime proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games.halo_ce_3925.clip_region import IMAGE_SHA256, SPANS
from recompiler.xita_recomp import Image
from iced_x86 import Decoder, Formatter, FormatterSyntax, Mnemonic, OpKind


def audit(xbe, stage):
    image = Image(str(xbe))
    if hashlib.sha256(image.data).hexdigest() != IMAGE_SHA256:
        raise ValueError('unsupported executable revision')
    for pc, (size, digest) in SPANS.items():
        if hashlib.sha256(image.bytes_at(pc,size)).hexdigest() != digest:
            raise ValueError('clipping span signature changed')
    found = set()
    for file in (stage/'recomp').glob('code_*.c'):
        found.update(int(x,16) for x in re.findall(r'/\* ([0-9A-F]{8})  call 000B7F10h',file.read_text()))
    expected = {0x530ce,0x534d5,0x561e5}
    if found != expected:
        raise ValueError('reachable direct caller inventory changed: '+str(sorted(found)))
    for pc in found:
        ins = Decoder(32,image.bytes_at(pc,15),ip=pc).decode()
        if ins.mnemonic != Mnemonic.CALL or ins.op0_kind != OpKind.NEAR_BRANCH32 or ins.near_branch_target != 0xb7f10:
            raise ValueError('owned caller does not match generated inventory')
    raw = image.bytes_at(0x532e0,0x53534-0x532e0)
    instructions = list(Decoder(32,raw,ip=0x532e0))
    if any(i.is_invalid for i in instructions) or instructions[-1].next_ip != 0x53534:
        raise ValueError('portal traversal boundary changed')
    fmt = Formatter(FormatterSyntax.INTEL)
    text = {i.ip:fmt.format(i) for i in instructions}
    pinned = {0x534d5:'call 000B7F10h', 0x534da:'test ax,ax',
        0x534dd:'mov [esp+834h],ax',0x534e5:'jle short 000534F0h',
        0x534e7:'lea edx,[esp+834h]',0x534f0:'cmp ax,0FFFFh',
        0x534f6:'mov edx,esi',0x534f8:'mov ecx,edi',0x534fa:'call 000532E0h',
        0x534ff:'mov eax,[esp+18h]',0x53503:'mov ecx,[ebx+5Ch]'}
    if any(text.get(pc)!=s for pc,s in pinned.items()):
        raise ValueError('portal result consumption changed')
    if any(s.split()[0].startswith('f') for s in text.values()):
        raise ValueError('portal traversal now directly consumes x87 state')
    return dict(image_sha256=IMAGE_SHA256, direct_call_sites=[hex(x) for x in sorted(found)],
        hot_caller=dict(entry='0x532e0',call='0x534d5',return_pc='0x534da',
          sha256=hashlib.sha256(raw).hexdigest(),direct_x87_instructions=0,
          observations=[
            'AX is tested and stored as a signed polygon vertex count immediately after return.',
            'Positive count recurses using the output polygon at caller ESP+0x834.',
            'Count -1 recurses using the parent polygon at ESI; zero skips recursion.',
            'ECX and EDX are set before recursion; EAX/ECX are overwritten at the loop continuation.',
            'The immediate TEST replaces condition flags; no x87 instructions occur in this caller.']),
        interface=dict(input='ECX low signed count and EDX vertex pointer',
          stack_args=['edge count','boundary vertices','capacity','output vertices','tolerance float'],
          input_point_bytes=8, stack_cleanup_bytes=24,
          proposed_preserved=['EBX','ESP after cleanup','EBP','ESI','EDI','logical x87 depth']),
        limitations=[
          'Inventory is reachable generated direct calls; it does not prove absence of indirect/interior entries.',
          'Only 0x534d5 is a candidate admission site; other callers retain the original implementation.',
          'Physical x87 scratch slots and flags are not proven globally dead by this local audit.',
          'Input/output physical-page aliases, concurrent workers, diagnostic hooks and scheduler events still need admission checks.',
          'Original preemption also joins object workers through xk_yield; dropping that boundary is not valid.'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe',type=Path,required=True);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=audit(a.xbe,a.stage)
    with a.output.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print('Verified three direct callers; hot portal caller consumes polygon count/data. Runtime admission remains unqualified.')


if __name__=='__main__':main()
