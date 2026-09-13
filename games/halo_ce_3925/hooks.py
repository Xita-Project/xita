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
        # Edges verified against the 3925 main loop, tick dispatcher and scene
        # dispatcher. Address labels deliberately retain unresolved semantics;
        # a tick child must not be advertised as all AI/physics without evidence.
        return {
            0xBD420: "main_loop", 0xFA920: "tick_driver", 0x109760: "tick_dispatch",
            0x120160: "director_update", 0xBCB30: "scene_dispatch",
            0xBC260: "view_setup", 0x5DBC0: "scene_frame",
            0x5D990: "scene_5D990", 0x5D340: "scene_5D340",
            0x800E0: "render_begin", 0x531B0: "scene_531B0",
            0x581A0: "scene_581A0", 0x520B0: "scene_520B0",
            0x5D410: "scene_5D410", 0x108A10: "tick_108A10",
            0x108810: "tick_108810", 0xF73F0: "tick_F73F0",
            0x1138B0: "tick_1138B0", 0xF7C40: "tick_F7C40",
            0xD8BC0: "tick_D8BC0", 0x107CB0: "tick_107CB0",
            0xE7140: "tick_E7140", 0x119460: "tick_119460",
            0x900E0: "tick_900E0", 0xF5BF0: "tick_F5BF0",
            0xE2A00: "tick_E2A00", 0xBB5D0: "frame_BB5D0",
            0x9EAE0: "frame_9EAE0", 0xBC400: "frame_BC400",
            0xBC8E0: "frame_BC8E0", 0xBB060: "frame_BB060",
            0x7EDF0: "frame_present",
        }

    def __init__(self, image):
        self.image = image
        self.enabled = matches_image(image)
        self.flare_enabled = self.enabled

    def before_instruction(self, address):
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
