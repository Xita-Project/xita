#!/usr/bin/env python3
"""xbe_image.py - flatten an XBE's sections into a guest-memory image (raw bytes at
base_address .. base_address+size_of_image, zero-filled gaps) for the recompiled
runtime / host harness.  Usage: xbe_image.py GAME.xbe manifest.json out.bin"""
import json, sys
xbe, manifest, out = sys.argv[1:4]
# Let the JSON decoder detect UTF-8/UTF-16 (including Windows BOMs) instead
# of decoding with the host's default text encoding first.
with open(manifest, "rb") as f:
    m = json.load(f)
data = open(xbe, "rb").read()
base = m["base_address"]; size = m["size_of_image"]
img = bytearray(size)
# The XBE header itself is mapped at the base address on a real Xbox and XAPI reads it at run time
# (SizeOfStackCommit for the main thread, TlsDirectory, section headers for XLoadSection, the
# certificate).  Without it the main thread got a 64 KB stack and the checkpoint save overflowed it.
hdr = m.get("size_of_headers", 0)
if hdr and hdr <= len(data):
    img[0:hdr] = data[0:hdr]
for s in m["sections"]:
    va, ro, rs = s["virtual_address"], s["raw_address"], s["raw_size"]
    if rs and ro + rs <= len(data):
        img[va - base: va - base + rs] = data[ro: ro + rs]
with open(out, "wb") as f:
    f.write(base.to_bytes(4, "little")); f.write(size.to_bytes(4, "little")); f.write(img)
print(f"wrote {out}: base 0x{base:08X}, {size >> 10} KB")
