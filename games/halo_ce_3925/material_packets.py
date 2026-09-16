"""Exact, independently guarded state-only islands in Halo 3925's 70110.

The adapter also requires the complete supported image. Resume labels leave
all ordinary instructions present and keep the four texture callbacks ordered.
"""
import hashlib

SPANS = (
    (0x7036D, 0x70433, "939b8e88a116b117b6cf1e99daeaea6464406edff073697d1f20be2c43676ba2"),
    (0x70440, 0x70486, "28e1cc0ebd611c168d4ca5dabf39ad8f9b08c81decf4744ef37bb9fec60dd8a0"),
    (0x70498, 0x704ED, "e3b18d2a392eaf69e1fe935addc01107b2183962470835ad0bd88e0df442fb56"),
    (0x704FF, 0x70554, "50bb2244cf970671ff843fb2fc03480382a33b531e9937705fb17cd2d445d879"),
    (0x70566, 0x705CC, "5690d65e37606fa317f4447078964c68647c8e5545e7815a29d34838c4d230b6"),
)


def verified_spans(image):
    return {start: (index, end) for index, (start, end, digest) in enumerate(SPANS)
            if hashlib.sha256(image.bytes_at(start, end - start) or b"").hexdigest() == digest}


def before_instruction(spans, address):
    if address in spans:
        index, end = spans[address]
        return ["#ifdef XV_MATERIAL_PACKET",
                "    { extern int xv_material_packet(xctx *, unsigned); "
                f"if (xv_material_packet(c, {index}u)) goto XV_MATERIAL_{end:08X}; }}",
                "#endif"]
    if any(end == address for _, end in spans.values()):
        return ["#ifdef XV_MATERIAL_PACKET", f"XV_MATERIAL_{address:08X}:", "#endif"]
    return []
