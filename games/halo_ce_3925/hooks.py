"""Halo-only emission rules, kept out of x86 instruction lowering.

Both the complete image and the existing local signatures guard these rules.
Native helpers retain the ordinary translated body as their fallback.
"""
import hashlib
import re

from recompiler.halo_flare_hooks import matches_image, ENTRY, ENTRY_HOOK, BARRIERS, barrier_line
from recompiler.core.hooks import NoGameHooks


class HaloHooks(NoGameHooks):
    def phase_targets(self):
        if not self.enabled:
            raise ValueError("Halo phase timing requires the audited executable")
        # September 12 physical timings selected the object-update region and
        # scene dispatcher for a deeper pass. Preserve the parent scopes, then
        # measure their immediate children; selected self still includes all
        # unselected descendants. These are address labels, not AI/physics claims.
        return {
            0xBD420: "main_loop",
            0xFA920: "tick_driver",
            0x109760: "tick_dispatch",
            0xBCB30: "scene_dispatch",
            0x5D410: "scene_5D410",
            0x900E0: "tick_900E0",
            0x7EDF0: "frame_present",
            0x8B2C0: "object_8B2C0",
            0x8B2F0: "object_8B2F0",
            0x8DC60: "object_8DC60",
            0x8E830: "object_8E830",
            0x8ECA0: "object_8ECA0",
            0x8FB70: "object_8FB70",
            0x2C920: "render_2C920",
            0x2CE30: "render_2CE30",
            0x539C0: "render_539C0",
            0x54010: "render_54010",
            0x542F0: "render_542F0",
            0x54740: "render_54740",
            0x54C10: "render_54C10",
            0x590F0: "render_590F0",
            0x59D80: "render_59D80",
            0x5B710: "render_5B710",
            0x5B760: "render_5B760",
            0x5E270: "render_5E270",
            0x60560: "render_60560",
            0x606B0: "render_606B0",
            0x6BF30: "render_6BF30",
            0x73A80: "render_73A80",
            0x73FD0: "render_73FD0",
            0x74D10: "render_74D10",
            0x75780: "render_75780",
            0x758E0: "render_758E0",
            0x769D0: "render_769D0",
            0x7BFE0: "render_7BFE0",
            0x7C200: "render_7C200",
            0x7F210: "render_7F210",
            0x92890: "render_92890",
            0x93C00: "render_93C00",
            0x93DD0: "render_93DD0",
            0xD35A0: "render_D35A0",
            0xD6B00: "render_D6B00",
            0xD80C0: "render_D80C0",
            0x10C300: "render_10C300",
            0x10C7E0: "render_10C7E0",
            0x110C80: "render_110C80",
            0x17F350: "render_17F350",
            0x17FCC0: "render_17FCC0",
        }

    def __init__(self, image):
        self.image = image
        self.enabled = matches_image(image)
        self.flare_enabled = self.enabled
        self.object_basis_enabled = self.enabled and hashlib.sha256(
            image.bytes_at(0x8E166, 301) or b"").hexdigest() == "2dd205a3eef42000a6f5adf73582961a651e1ab9630a6669285a972085f61941"
        self.palette_enabled = self.enabled and all(
            hashlib.sha256(image.bytes_at(address, size) or b"").hexdigest() == digest
            for address, size, digest in (
                (0xA2781, 0x45, "a1460c149ae33b843578a5652fdd0dc09e5e9fe39ffb96fb5c19204041dd9fd3"),
                (0xB5B40, 339, "21273987e0276cda51a2d70b2b576abc42195b8efeff9ab4e8f552a15b323726")))
        self.object_scan_enabled = self.enabled and hashlib.sha256(
            image.bytes_at(0x900E0, 0x239) or b"").hexdigest() == "5bcdb3c78aa2f0b4ba4da986cfe59cb0d28c1004cbcc804abdafabd8200f520a"

    def before_instruction(self, address):
        if self.object_scan_enabled and address in (0x90190, 0x90240, 0x902B6):
            index = (0x90190, 0x90240, 0x902B6).index(address)
            return ["#ifdef XV_NATIVE_OBJECT_SCAN",
                    "    { extern int xv_object_scan_active; "
                    "extern unsigned xv_object_scan_empty(xctx *, unsigned); "
                    f"if (xv_object_scan_active) xv_object_scan_empty(c, {index}u); }}",
                    "#endif"]
        if self.object_basis_enabled and address == 0x8E166:
            return ["#ifdef XV_NATIVE_OBJECT_BASIS",
                    "    { extern int xv_math_object_basis(xctx *); if (xv_math_object_basis(c)) goto L_0008E293; }",
                    "#endif"]
        if self.palette_enabled and address == 0xA2781:
            return ["#ifdef XV_NATIVE_MODEL_PALETTE",
                    "    { extern int xv_math_model_palette(xctx *); if (xv_math_model_palette(c)) goto L_000A27F2; }",
                    "#endif"]
        if self.flare_enabled and address in BARRIERS:
            return [barrier_line(address)]
        return []

    def function_entry(self, address):
        out = []
        if not self.enabled:
            return out
        if self.flare_enabled and address == ENTRY:
            out.append(ENTRY_HOOK)
        native_math = {
            0xB5EA0: (105, "da339a7eda273186b22d469e8d3fa75d0ec291bee26bc91c8534de058e01e0e1", "xv_math_point_transform"),
            0x5C300: (733, "5e463d77ea6ed255323f310d40cf3f7e847c08e7a6a71841b12545b1f937e1ab", "xv_math_bounds"),
            0xB5B40: (339, "21273987e0276cda51a2d70b2b576abc42195b8efeff9ab4e8f552a15b323726", "xv_math_matrix_multiply"),
            0xB5F60: (291, "9f10d4414ec5fb6b39f5f20f6100791e0aec209d77f6c60599402dff6bdb5d78", "xv_math_quaternion_matrix"),
            0xB71C0: (874, "34bf76203325f8157fcc9595b80b4be8851d829baabc556d45941533aa6c22e3", "xv_math_polygon_clip"),
        }
        if address in native_math:
            size, digest, helper = native_math[address]
            if hashlib.sha256(self.image.bytes_at(address, size)).hexdigest() == digest:
                out.append(f"    {{ extern int {helper}(xctx *); if ({helper}(c)) return; }}")
        if address == 0x114C50 and self.image.bytes_at(address, 8) == bytes.fromhex("A134AB2F008A4824"):
            out.append("    { extern void xk_quality_decal_budget(void); xk_quality_decal_budget(); }")
        return out

    def transform_body(self, address, body):
        if self.enabled and address == 0x88B80 and hashlib.sha256(
                self.image.bytes_at(0x88BA5, 0x54) or b"").hexdigest() == "8cff0b4dd7978f4e88a2872a32b2d9a8df712111a33aed2f8ef29208df959b3f":
            body = re.sub(r"    /\* 00088BA5 .*?(?=    /\* 00088BF9 )",
                "    /* 00088BA5..00088BF8: native interval arithmetic; original spill precision. */\n    { extern void xv_bsp_plane_interval(xctx *); xv_bsp_plane_interval(c); }\n",
                body, count=1, flags=re.S)
        return body

    def postprocess(self, directory):
        if not self.enabled:
            raise ValueError("Halo postprocessing requires the audited executable")
        from tools.patch_split_screen import patch
        patch(directory)
