#!/usr/bin/env python3
"""Prepare private, revision-checked Halo 2 startup artifacts from an owned XBE."""
from pathlib import Path
import argparse
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from recompiler.core.profile import load_profile
from recompiler.xita_recomp import Image, KERNEL_DATA_EXPORTS, KERNEL_EXPORTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "local/halo2_5849/boot")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--graphics", action="store_true", help="enable the strict diagnostic NV2A bus adapter")
    mode.add_argument("--host-channel", action="store_true", help="enable the experimental synchronous command consumer (requires HOST_CHANNEL=1)")
    args = parser.parse_args()
    image = Image(str(args.xbe))
    load_profile("halo2_5849").validate_image(image)
    # The observed XAPI initializer at 0x2D1D15 calls the first table, and its
    # following initializer at 0x2D1CBD calls the other two. These are bounded
    # direct table walks in this hash-pinned image, not a general data scan.
    roots = {image.entry}
    for start, end in ((0x461180, 0x46118C), (0x461814, 0x46182C), (0x461190, 0x461810), (0x461FA8, 0x461FAC)):
        for slot in range(start, end, 4):
            target = image.u32(slot)
            if target in (0, 0xFFFFFFFF):
                continue
            if target is None or not image.is_code(target):
                raise ValueError(f"Initializer slot {slot:#x} has invalid target {target!r}")
            roots.add(target)
    output = args.out.resolve()
    generated = output / "generated"
    profile = str(Path(__file__).with_name("graphics-profile.json")) if args.graphics else "halo2_5849"
    if args.host_channel:
        profile = str(Path(__file__).with_name("host-channel-profile.json"))
    subprocess.run([sys.executable, "-m", "recompiler", str(args.xbe.resolve()),
                    "--profile", profile, "--no-data-roots", "--trace-calls", "--trace-funcs",
                    "--files", "128", "-o", str(generated),
                    "--roots", f"{image.entry:X}", *(f"{root:X}" for root in sorted(roots - {image.entry}))], cwd=ROOT, check=True)
    # xv_game_main is the startup entry in this entry-only diagnostic target;
    # boot.c only uses xv_entry_point. No game-main boundary is asserted here.
    (output / "startup-roots.json").write_text(json.dumps(sorted(roots), indent=2) + "\n")
    # Generated compatibility stubs return success. This diagnostic target
    # deliberately does not link them: absent kernel implementations halt.
    lines = ['#include "xv_x86rt.h"', 'void xv_boot_missing_kernel(xctx *c, const char *name);']
    for ordinal in sorted(set(image.kernel_imports().values()) - KERNEL_DATA_EXPORTS):
        name = KERNEL_EXPORTS[ordinal]
        lines += [f'void xk_{name}(xctx *c) __attribute__((weak));',
                  f'void xk_{name}(xctx *c) {{ xv_boot_missing_kernel(c, "{name}"); }}']
    (generated / "strict-kernel.c").write_text("\n".join(lines) + "\n")
    manifest = output / "manifest.json"
    manifest.write_text(json.dumps(image.m, indent=2) + "\n")
    subprocess.run([sys.executable, str(ROOT / "recompiler/xbe_image.py"), str(args.xbe.resolve()),
                    str(manifest), str(output / "halo2_image.bin")], check=True)


if __name__ == "__main__":
    main()
