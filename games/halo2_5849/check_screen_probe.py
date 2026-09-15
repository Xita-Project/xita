#!/usr/bin/env python3
"""Compare eight isolated GPU fixtures; requires NumPy and private inputs.

The independent reference uses normalized RGB565 endpoints, BC2 interpolation,
explicit four-stage equations, and destination blending. Allow one UNORM8 level
for sampling/output quantization. This is not a hardware NV2A comparison or
proof of displayed menu pixels.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import numpy as np


def decode(data):
    if len(data) != 64:
        raise ValueError("expected four BC2 blocks")
    image = np.zeros((8, 8, 4))
    for block in range(4):
        data_block = data[16 * block:16 * block + 16]
        alpha = int.from_bytes(data_block[:8], "little")
        endpoint0, endpoint1, indices = struct.unpack("<HHI", data_block[8:])
        colors = [np.array([value >> 11, (value >> 5) & 63, value & 31]) /
                  np.array([31, 63, 31]) for value in (endpoint0, endpoint1)]
        # BC2 always has four opaque RGB entries, irrespective of endpoint
        # ordering. Keep normalized precision until sampling/output conversion.
        colors += [(2 * colors[0] + colors[1]) / 3,
                   (colors[0] + 2 * colors[1]) / 3]
        for pixel in range(16):
            y, x = (block // 2) * 4 + pixel // 4, (block % 2) * 4 + pixel % 4
            image[y, x, :3] = colors[(indices >> (pixel * 2)) & 3]
            image[y, x, 3] = ((alpha >> (pixel * 4)) & 15) / 15
    return image


def sample(image, u, v):
    s, t = np.clip(u * 8 - .5, 0, 7), np.clip(v * 8 - .5, 0, 7)
    x, y = s.astype(int), t.astype(int)
    f, g = (s - x)[..., None], (t - y)[..., None]
    nx, ny = np.minimum(x + 1, 7), np.minimum(y + 1, 7)
    return ((image[y, x] * (1 - f) + image[y, nx] * f) * (1 - g) +
            (image[ny, x] * (1 - f) + image[ny, nx] * f) * g)


def synthetic_blocks():
    data = bytearray(64)
    colors = [0xF800, 0x07E0, 0x001F, 0xFFFF]
    for block in range(4):
        data[block * 16:block * 16 + 8] = bytes(i * 2 | ((15 - i * 2) << 4) for i in range(8))
        struct.pack_into("<HHI", data, block * 16 + 8,
                         colors[block], colors[3 - block], 0xE4E4E4E4)
    return data


def compare(prepared, results):
    c = np.fromfile(prepared / "screen.constants.bin", dtype="<f4").reshape(18, 4)
    vertices = np.fromfile(prepared / "screen.vertices.bin", dtype="<f4").reshape(4, 7, 4)
    if not np.all(np.isfinite(c)) or not np.all(np.isfinite(vertices)):
        raise ValueError("nonfinite fixture")
    blocks = (prepared / "screen.texture2.bin").read_bytes()
    y, x = np.indices((480, 640), dtype=np.uint32)
    rgba = np.stack([(x * 11 + y * 23) & 255, (x * 37 + y * 19) & 255,
                     (x * 13 + y * 17) & 255, (x * 7 + y * 3) & 255], axis=-1) / 255
    reports = []
    for test in range(8):
        t0 = rgba if test else np.broadcast_to(np.array([1, 1, 0, 1]), (480, 640, 4))
        image = decode(synthetic_blocks() if 3 <= test <= 6 else blocks)
        hi = (t0[..., 3] * 65280 + t0[..., 0] * 255) / 65535
        lo = (t0[..., 1] * 65280 + t0[..., 2] * 255) / 65535
        u = hi if test >= 2 else hi * vertices[0, 2, 0]
        v = lo if test >= 2 else hi * vertices[0, 3, 0]
        t2 = sample(image, u, v)
        # Stage 0 products and stage 1 sum read the old alpha values. Stage 1
        # alpha writes independently feed stage 2; stage 3 consumes that result.
        rgb = ((c[0, :3] * t2[..., :3]) * (1 - c[0, 3] * t2[..., 3, None]) +
               (c[8, :3] * t2[..., 3, None]) * (1 - c[8, 3] * t2[..., 2, None]))
        rgb = rgb * (1 - t0[..., 2, None]) + c[3, :3] * t0[..., 2, None]
        alpha = ((1 - t0[..., 2]) *
                 (1 - (1 - c[1, 3] * t2[..., 2]) * (1 - c[9, 3] * t2[..., 3])) +
                 t0[..., 2] * c[3, 3])
        destination = np.zeros((480, 640, 4))
        if test >= 4:
            destination = np.where(((x ^ y) & 1)[..., None],
                                   [0x50, 0x30, 0x10, 0x80], [0x20, 0x10, 0x60, 0xFF]) / 255
        expected = np.concatenate([
            np.clip(rgb + destination[..., :3] * (1 - alpha[..., None]), 0, 1),
            destination[..., 3, None] * (1 - alpha[..., None])], axis=-1) * 255
        if test >= 6:
            expected = image[y // 60, x // 80] * 255  # independent point-sampling check
        wanted = np.floor(expected + .5).astype(int)
        words = np.fromfile(results / f"screen-probe-{test}.bin", dtype="<u4").reshape(480, 640)
        actual = np.stack([(words >> shift) & 255 for shift in (16, 8, 0, 24)], axis=-1).astype(int)
        error = np.abs(wanted - actual)
        reports.append(dict(test=test, pixels=640 * 480,
                            max_error=error.max(axis=(0, 1)).tolist(),
                            outside_1=int(np.any(error > 1, axis=-1).sum()),
                            first_expected=wanted[0, 0].tolist(), first_actual=actual[0, 0].tolist(),
                            mean_error=error.mean(axis=(0, 1)).tolist()))
    same_winding = ((results / "screen-probe-4.bin").read_bytes() ==
                    (results / "screen-probe-5.bin").read_bytes())
    passed = (same_winding and all(r["outside_1"] == 0 for r in reports) and
              reports[0]["max_error"] == [0, 0, 0, 0])
    return dict(passed=passed, opposite_winding_identical=same_winding,
                pixels=sum(r["pixels"] for r in reports), fixtures=reports,
                inputs={name: hashlib.sha256((prepared / name).read_bytes()).hexdigest() for name in
                        ("screen.constants.bin", "screen.vertices.bin", "screen.texture2.bin")})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.prepared, args.results)
    print(json.dumps(result, indent=2))
    sys.exit(0 if result["passed"] else 1)
