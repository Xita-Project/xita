#!/usr/bin/env python3
"""Prepare private, revision-checked Halo 2 startup artifacts from an owned XBE."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from recompiler.core.profile import load_profile
from recompiler.xita_recomp import Image, KERNEL_DATA_EXPORTS, KERNEL_EXPORTS


HOST_CALLBACK_WALK = (0x3FBA54, 135, "e0cc1649c0b744615b3de0f5b2446411bb408d0b3ce4d1d3da59980219abc70c")
XPP_CALLBACK_WALK = (0x408C72, 36, "9234a2afaedda5206ca55c2bf3f0269b70b1b0345091581c639ee86230cd3756")
GAME_INIT_WALK = (0x137C84, 19, "44c1c20adf4bb014714a0825d601e592e668699e31671c7676a674951946da02")


def game_initialization_roots(image):
    """Native41: 68 record callbacks, ESI=0..0x990 in steps of 0x24."""
    address, length, digest = GAME_INIT_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 game initialization walk fingerprint mismatch")
    roots = set()
    for slot in range(0x440DD8, 0x440DD8 + 0x990, 0x24):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 game initialization slot {slot:#x} has invalid target {target!r}")
        roots.add(target)
    return roots


def host_device_callback_roots(image):
    """Native37 XPP dispatch: six descriptor slots, initialization at +4."""
    address, length, digest = XPP_CALLBACK_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 XPP callback walk fingerprint mismatch")
    roots = set()
    for slot in range(0x4086D4, 0x4086EC, 4):
        descriptor = image.u32(slot)
        if descriptor == 0:
            continue
        section = image.section_of(descriptor) if descriptor else None
        if not descriptor or descriptor & 3 or not section or section[4] != "XPP":
            raise ValueError(f"Halo 2 XPP descriptor {slot:#x} is invalid")
        target = image.u32(descriptor + 4)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != "XPP":
            raise ValueError(f"Halo 2 XPP callback {descriptor:#x} is invalid")
        roots.add(target)
    return roots


def host_channel_callback_roots(image):
    """Exact default-state callback walk observed at 3FBACA in native attempt 25.

    EBX traverses E4..294 and ESI=EBX-170. The indirect call runs only for
    ESI>=B0 and EBX!=268, with EBP=403A48. This excludes slot403B40.
    The enclosing caller already has the whole-image revision gate in main().
    """
    address, length, digest = HOST_CALLBACK_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 default-state callback walk fingerprint mismatch")
    roots = set()
    for slot in range(0x403AF8, 0x403B70, 4):
        if slot == 0x403B40:
            continue
        target = image.u32(slot)
        section = image.section_of(target) if target is not None else None
        if not target or not image.is_code(target) or not section or section[4] != "D3D":
            raise ValueError(f"Halo 2 default-state callback {slot:#x} has invalid target {target!r}")
        roots.add(target)
    return roots


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "local/halo2_5849/boot")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--graphics", action="store_true", help="enable the strict diagnostic NV2A bus adapter")
    mode.add_argument("--host-channel", action="store_true", help="enable the experimental synchronous command consumer (requires HOST_CHANNEL=1)")
    parser.add_argument("--audio-unavailable", action="store_true", help="diagnostic only: DirectSoundCreate returns DSERR_NODRIVER (requires --host-channel)")
    args = parser.parse_args()
    if args.audio_unavailable and not args.host_channel:
        parser.error("--audio-unavailable requires --host-channel")
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
    if args.host_channel:
        roots.update(host_channel_callback_roots(image))
        # Native attempt 35: application creator 0x120A90 pushes 0x120C30
        # at 0x120B0E and calls XAPI thread creation at 0x120B3D. The native
        # worker dispatch reaches that exact entry. Whole-image gate above.
        roots.add(0x120C30)
        # Attempt 36 dispatches the allocator's slot zero at 0x453308.
        # The two-slot vtable contains allocate/free; the following bytes are
        # string data. Keep the whole-image revision guard and exact bounds.
        roots.update(image.u32(slot) for slot in (0x453308, 0x45330C))
        roots.update(host_device_callback_roots(image))
        # Attempt 39 reaches the sound object's four-slot vtable at 0x4170E4:
        # destructor, AddRef, Release, delete helper. Slot one is the observed
        # indirect dispatch from 0x37B17B. The following words are data.
        roots.update(image.u32(slot) for slot in range(0x4170E4, 0x4170F4, 4))
        roots.update(game_initialization_roots(image))
        # Native42: 0x66305 calls [ [0x477058] + 0x10 ]; the pinned record
        # is 0x467140, whose callback is 0x662E0 (ten-byte original body).
        roots.add(image.u32(image.u32(0x477058) + 0x10))
    output = args.out.resolve()
    generated = output / "generated"
    profile = str(Path(__file__).with_name("graphics-profile.json")) if args.graphics else "halo2_5849"
    if args.host_channel:
        profile = str(Path(__file__).with_name("host-channel-profile.json"))
    if args.audio_unavailable:
        profile = str(Path(__file__).with_name("audio-unavailable-profile.json"))
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
