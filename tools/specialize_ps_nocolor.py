#!/usr/bin/env python3
"""Fold exact PSC1/PSC2 RGB in the audited private GREATER material source.

No sample or arithmetic is manually removed. Runtime mode 8 requires the full
mode-6 proof plus bit-exact positive zero/one constants. Other sources require
a new audit; generated game shaders remain outside the source repository.
"""
import argparse
import hashlib
from pathlib import Path

AUDITED_SHA256 = '61d82e0e3106f1ace794d6474bff6c2d01b2d258210b90a626bf684fba05a48d'


def specialize(source):
    if hashlib.sha256(source).hexdigest() != AUDITED_SHA256:
        raise ValueError('Source does not match the audited GREATER material')
    return (source.replace(b'psc[1].rgb', b'float3(0.0, 0.0, 0.0)')
            .replace(b'psc[2].rgb', b'float3(1.0, 1.0, 1.0)'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(specialize(args.input.read_bytes()))
