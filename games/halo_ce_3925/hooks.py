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
        # The September 15 physical valley capture puts selected self time in
        # A26B0 (model setup), 532E0 (visibility) and 92330 (light updates).
        # Separate their matrix, material, query and shared-list children before
        # choosing native/worker boundaries. Inclusive rows can overlap and
        # include native waits; all capture arms retain serial object callbacks.
        return {
            0x51E90: "render_51E90",
            0x52240: "render_52240",
            0x524B0: "render_524B0",
            0x528D0: "render_528D0",
            0x52E10: "render_52E10",
            0x53280: "render_53280",
            0x532E0: "render_532E0",
            0x53540: "render_53540",
            0x539C0: "render_539C0",
            0x54010: "render_54010",
            0x56670: "render_56670",
            0x5B190: "render_5B190",
            0x5B4A0: "render_5B4A0",
            0x5B760: "render_5B760",
            0x5C300: "render_5C300",
            0x5D410: "scene_5D410",
            0x5E270: "render_5E270",
            0x60560: "render_60560",
            0x637A0: "render_637A0",
            0x63C00: "render_63C00",
            0x66510: "render_66510",
            0x6B060: "render_6B060",
            0x6EFC0: "render_6EFC0",
            0x6F730: "render_6F730",
            0x70110: "render_70110",
            0x7E2F0: "render_7E2F0",
            0x7E530: "render_7E530",
            0x7EDF0: "frame_present",
            0x7F210: "render_7F210",
            0x8B0F0: "render_8B0F0",
            0x8B220: "render_8B220",
            0x8B910: "render_8B910",
            0x8D650: "render_8D650",
            0x8DDF0: "object_pose",
            0x900E0: "object_update",
            0x92330: "render_92330",
            0x92890: "render_92890",
            0x96430: "object_collision",
            0xA2380: "render_A2380",
            0xA26B0: "render_A26B0",
            0xA9330: "render_A9330",
            0xB1260: "render_B1260",
            0xB5B40: "render_B5B40",
            0xB5DF0: "render_B5DF0",
            0xB7F10: "render_B7F10",
            0xBCB30: "scene_dispatch",
            0xBD420: "main_loop",
            0xFA920: "tick_driver",
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
        self.hierarchy_enabled = self.enabled and all(
            hashlib.sha256(image.bytes_at(address, size) or b"").hexdigest() == digest
            for address, size, digest in (
                (0x8DDF0, 2218, "247190d1cd001f43646b9627fd2f538c64b15efefaad1f9510f5137f04e4dc63"),
                (0xB5B40, 339, "21273987e0276cda51a2d70b2b576abc42195b8efeff9ab4e8f552a15b323726"),
                (0xB5F60, 291, "9f10d4414ec5fb6b39f5f20f6100791e0aec209d77f6c60599402dff6bdb5d78")))

    # Shared cluster lists and datum allocation/free. The first concurrent
    # campaign test cycled at 56643 in removal after unguarded list mutation.
    # Use the existing recursive shared-helper mutex to avoid a lock-order pair.
    object_shared = {
        0x325C0: (268, "b9c3ec368939628f063f09a3e5540714b8cc40b48ab6933e0783e7ec0f441513"),  # bitmap-cache transaction
        0x32B00: (142, "d003de5fabd8c9399c4e893f88b150ac128b2f250d2d2b98a3716a9f476dfcc0"),  # cache/queue transaction
        0x33A20: (121, "9709750dd0b4fcbb05e409f5fefddac60b2286bd6360402625698d86e25d35a0"),  # cache/queue transaction
        0x4C980: (976, "d566b124ef1a74ef77392465cd04524c704426497c4a0f2835890bc1124fb106"),  # indirect object callback, including its jump table
        0x96430: (1025, "99502556c4292e6e48fef7c8e51a27402edce60c2b67490dbddf019e07b69bde"),  # collision-producing object callback
        0x565E0: (139, "a545d5f623d8b3e606350417a39ad062380c8d92a1df0aadbb8074cfc18d03ef"),
        0x56670: (273, "54d374355fdeb944c482141e466359117060ab5f6db257a02a382d1ea9008154"),
        0xA92C0: (108, "91de09c33f100a0543ffaabea4a5472f146b763cefc409831186d16e412fc1e6"),
        0xA9330: (131, "2563d84b6197cd72ccfce86a6bb43ab8a83ae9068476a89432dfb8dd5eecce54"),
        0x114D30: (6661, "f69b2229fb307116fcaef1542e2d2f1099d67f1705edea176c2f6df3e0b9373e"),  # impact geometry allocation/write/publication
    }
    # Independently emitted entry points share backward branches into the same
    # original function. Verify its whole body, not just the suffix at the alias.
    object_shared_aliases = {0x115423: 0x114D30, 0x115FDF: 0x114D30}

    def before_instruction(self, address):
        if self.hierarchy_enabled and address == 0x8E0F0:
            return ["#ifdef XV_NATIVE_MODEL_HIERARCHY",
                    "    { extern int xv_math_model_hierarchy(xctx *); (void)xv_math_model_hierarchy(c); }",
                    "#endif"]
        if self.object_scan_enabled and address in (0x900E0, 0x902A9, 0x90314):
            line = {0x900E0: "(void)xv_object_jobs_begin(c);",
                    0x902A9: "xv_object_jobs_join();",
                    0x90314: "xv_object_jobs_finish(c);"}[address]
            return ["#ifdef XV_EXPERIMENTAL_OBJECT_JOBS", "    " + line, "#endif"]
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
        if address == 0x1D130 and hashlib.sha256(
                self.image.bytes_at(address, 16) or b"").hexdigest() == "810ef7f224dd7cd8feb82821c31ca0997daab985ffc11449ffa287d879ada54e":
            out.extend(["#ifdef XV_EXPERIMENTAL_OBJECT_JOBS",
                        "    xv_object_job_stack_probe(c);", "#endif"])
        if address == 0x8FB70 and self.enabled and hashlib.sha256(
                self.image.bytes_at(address, 0x111) or b"").hexdigest() == "8003e134015d9a4df2a0e3bd501610aafacb7b7693bba77a40094b542c964ae0":
            out.extend(["#ifdef XV_EXPERIMENTAL_OBJECT_JOBS",
                        "    if (xv_object_jobs_queue(c)) return;", "#endif"])
        shared_address = self.object_shared_aliases.get(address, address)
        if shared_address in self.object_shared:
            size, digest = self.object_shared[shared_address]
            if hashlib.sha256(self.image.bytes_at(shared_address, size) or b"").hexdigest() == digest:
                out.extend(["#ifdef XV_EXPERIMENTAL_OBJECT_JOBS",
                            "    XV_OBJECT_MATH_GUARD(); /* shared guest transaction */", "#endif"])
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
        if self.enabled and address == 0x87EA0 and hashlib.sha256(
                self.image.bytes_at(0x87ECC, 0x1A) or b"").hexdigest() == "68ec0f334940aa871ae2fede180af8adf67f37fd58a0f094857397e9445970b0":
            def sphere_distance(match):
                return ("#ifdef XV_NATIVE_BSP_SPHERE\n"
                        "    { extern int xv_bsp_sphere_plane_distance(xctx *);\n"
                        "      if (!xv_bsp_sphere_plane_distance(c)) {\n"
                        "#endif\n" + match[0] +
                        "#ifdef XV_NATIVE_BSP_SPHERE\n    } }\n#endif\n")
            # The emitted first-node and loop-body copies both need the same
            # hook. Original arithmetic remains the exceptional-input fallback.
            body = re.sub(r"    /\* 00087ECC .*?(?=    /\* 00087EE6 )",
                          sphere_distance, body, flags=re.S)
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
