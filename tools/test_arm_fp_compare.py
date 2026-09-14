#!/usr/bin/env python3
"""Check compiled Cortex-A9 comparisons against the preceding portable helper.

Requires VitaSDK, Unicorn and pyelftools. No firmware/game input is needed.
Checks full guest context, an independent condition-code oracle, and complete
native FPSCR equality with exceptions masked. Counts are instructions, not
cycles or hardware FPS. Use --output-dir to preserve the linked test and report.
"""
import argparse
import json
import math
from pathlib import Path
import random
import statistics
import struct
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR,
)


def check(out, cases, optimization):
    root = Path(__file__).resolve().parents[1]
    binary = out / "compare.elf"
    command = ["arm-vita-eabi-gcc", optimization, "-mthumb", "-mcpu=cortex-a9",
               "-mfpu=neon", "-mfloat-abi=hard", "-fno-strict-aliasing",
               "-I" + str(root / "recomp"), "-nostdlib",
               "-Wl,-Ttext=0x10000", "-Wl,-e,original_compare",
               str(root / "tools/tests/arm_fp_compare.c"), "-lgcc",
               "-o", str(binary)]
    subprocess.run(command, check=True)
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
    uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
    for address, size in [(0, 0x100000), (0x200000, 0x10000),
                          (0x300000, 0x10000), (0x400000, 0x1000)]:
        uc.mem_map(address, size)
    with binary.open("rb") as file:
        elf = ELFFile(file)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD":
                uc.mem_write(segment["p_vaddr"], segment.data())
        symbols = {s.name: s["st_value"] for s in
                   elf.get_section_by_name(".symtab").iter_symbols()}
    size, kind_at, bits_at, top_at, status_at = struct.unpack(
        "<5I", uc.mem_read(symbols["compare_layout"], 20))
    count = [0]

    def step(machine, address, length, user):
        count[0] += 1

    uc.hook_add(UC_HOOK_CODE, step)
    rng = random.Random(0x875E)
    edges = [0, 1, 0xfffffffffffff, 0x10000000000000, 0x3fefffffffffffff,
             0x3ff0000000000000, 0x4000000000000000, 0x7fefffffffffffff,
             0x7ff0000000000000, 0x7ff0000000000001,
             0x7ff8000000000000, 0x7ff8123456789abc]
    edges += [v | (1 << 63) for v in edges]
    pairs = [(a, b) for a in edges for b in edges]
    pairs += [(rng.getrandbits(64), rng.getrandbits(64)) for _ in range(cases)]
    timings, executions = {}, 0
    for control in [i << 22 for i in range(16)]:
        for a, b in pairs:
            for eflags in [0, 1]:
                raw = bytearray(rng.getrandbits(8) for _ in range(size))
                struct.pack_into("<I", raw, kind_at, rng.randrange(6))
                struct.pack_into("<I", raw, bits_at, rng.choice([8, 16, 32]))
                top = rng.randrange(8)
                struct.pack_into("<I", raw, top_at, top)
                old_status = struct.unpack_from("<H", raw, status_at)[0]
                av, bv = [struct.unpack("<d", struct.pack("<Q", v))[0]
                          for v in (a, b)]
                if control & (1 << 24):  # VFP flushes subnormal inputs to zero.
                    if (a & 0x7ff0000000000000) == 0:
                        av = math.copysign(0.0, av)
                    if (b & 0x7ff0000000000000) == 0:
                        bv = math.copysign(0.0, bv)
                cc = (0x4500 if math.isnan(av) or math.isnan(bv) else
                      0x100 if av < bv else 0x4000 if av == bv else 0)
                expected_status = (old_status & ~0x4700) | cc | (top << 11)
                fpscr = control | rng.choice([0, 1, 0x9f, 0xa000009f])
                outputs, instructions = [], []
                operands = struct.pack("<QQ", a, b)
                for name in ["original", "candidate"]:
                    uc.mem_write(0x300000, bytes(raw))
                    uc.mem_write(0x301000, operands)
                    for register, value in [
                        (UC_ARM_REG_R0, 0x300000), (UC_ARM_REG_R1, 0x301000),
                        (UC_ARM_REG_R2, eflags), (UC_ARM_REG_SP, 0x20fff0),
                        (UC_ARM_REG_LR, 0x400001), (UC_ARM_REG_FPSCR, fpscr),
                    ]:
                        uc.reg_write(register, value)
                    count[0] = 0
                    uc.emu_start(symbols[name + "_compare"] | 1,
                                 0x400000, count=2000)
                    assert count[0] < 2000, "Fixture did not return"
                    got = bytes(uc.mem_read(0x300000, size))
                    assert bytes(uc.mem_read(0x301000, 16)) == operands
                    assert struct.unpack_from("<H", got, status_at)[0] == expected_status, (
                        name, hex(a), hex(b), hex(control), eflags)
                    if not eflags:
                        expected = raw[:]
                        struct.pack_into("<H", expected, status_at, expected_status)
                        assert got == bytes(expected), "Unexpected guest context change"
                    outputs.append((got, uc.reg_read(UC_ARM_REG_FPSCR)))
                    instructions.append(count[0])
                    executions += 1
                assert outputs[0] == outputs[1], (
                    hex(a), hex(b), hex(fpscr), eflags, outputs[0][1], outputs[1][1])
                relation = {0x4500: "unordered", 0x100: "less", 0x4000: "equal", 0: "greater"}[cc]
                key = ("flags_" if eflags else "plain_") + relation
                timings.setdefault(key, []).append(instructions)
    result = dict(executions=executions, pairs=len(pairs), fpscr_controls=16,
                  context_bytes=size, full_context_and_fpscr_match=True,
                  optimization=optimization, command=command,
                  scope="Synthetic ARM instructions, not cycles or hardware FPS",
                  timings={key: dict(cases=len(values),
                      original_mean=statistics.mean(v[0] for v in values),
                      candidate_mean=statistics.mean(v[1] for v in values))
                      for key, values in timings.items()})
    (out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS:", json.dumps(result))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--cases", type=int, default=1024)
    parser.add_argument("--optimization", choices=["O2", "O3"], default="O2")
    args = parser.parse_args()
    if not 0 <= args.cases <= 10000:
        parser.error("--cases must be between 0 and 10000")
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=False)
        check(args.output_dir, args.cases, "-" + args.optimization)
    else:
        with tempfile.TemporaryDirectory(prefix="xita-arm-compare-") as temporary:
            check(Path(temporary), args.cases, "-" + args.optimization)


if __name__ == "__main__":
    main()
