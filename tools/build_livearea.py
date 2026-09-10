#!/usr/bin/env python3
"""Render the editable LiveArea SVGs to Vita's indexed PNG assets.

Requires rsvg-convert (librsvg), Pillow and the DejaVu Sans font.
Use the VitaSDK sample's 128-color PNG-8 and CRLF XML conventions.
"""
import io
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1] / "sce_sys/livearea"
for name, size in [("bg", (840, 500)), ("startup", (280, 158))]:
    png = subprocess.check_output(["rsvg-convert", str(ROOT / "source" / (name + ".svg"))])
    with Image.open(io.BytesIO(png)) as image:
        assert image.size == size
        indexed = image.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT)
        destination = ROOT / "contents" / (name + ".png")
        indexed.save(destination, optimize=True)
        print(f"{destination.relative_to(ROOT.parent.parent)}: {size[0]}x{size[1]}, indexed PNG")

template = ROOT / "contents/template.xml"
text = template.read_text(encoding="utf-8")
template.write_bytes(text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
