"""Halo-only emission rules, kept out of x86 instruction lowering.

Both the complete image and the existing local signatures guard these rules.
Native helpers retain the ordinary translated body as their fallback.
"""
import hashlib
import re

from recompiler.halo_flare_hooks import matches_image, ENTRY, ENTRY_HOOK, BARRIERS, barrier_line
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925 import clip_region, collision_solver, object_motion_profile, collision_vertices, segment_sphere, collision_traversal, scene_partition, model_route_profile, constant_pack, cluster_lifetime, model_fog, model_uv, marker_region


class HaloHooks(NoGameHooks):
    def discovery(self, *args):
        if not self.enabled:
            raise ValueError("CE discovery requires the audited executable")
        from .discovery import HaloDiscovery
        return HaloDiscovery(*args)

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
        self.owner_phase_enabled = self.enabled
        self.scene_partition_enabled = self.enabled
        self.scene_bucket0_detail_enabled = self.enabled
        self.flare_enabled = self.enabled
        self.clip_region_enabled = self.enabled and clip_region.matches_spans(image)
        self.object_basis_enabled = self.enabled and hashlib.sha256(
            image.bytes_at(0x8E166, 301) or b"").hexdigest() == "2dd205a3eef42000a6f5adf73582961a651e1ab9630a6669285a972085f61941"
        self.palette_enabled = self.enabled and all(
            hashlib.sha256(image.bytes_at(address, size) or b"").hexdigest() == digest
            for address, size, digest in (
                (0xA2781, 0x45, "a1460c149ae33b843578a5652fdd0dc09e5e9fe39ffb96fb5c19204041dd9fd3"),
                (0xB5B40, 339, "21273987e0276cda51a2d70b2b576abc42195b8efeff9ab4e8f552a15b323726")))
        self.object_scan_enabled = self.enabled and hashlib.sha256(
            image.bytes_at(0x900E0, 0x239) or b"").hexdigest() == "5bcdb3c78aa2f0b4ba4da986cfe59cb0d28c1004cbcc804abdafabd8200f520a"
        # Collision collection's object walk and the shape callback whose
        # early exits it reproduces, including the callback's jump tables.
        self.object_collect_enabled = self.enabled and all(
            hashlib.sha256(image.bytes_at(address, size) or b"").hexdigest() == digest
            for address, size, digest in (
                (0x171F10, 672, "39eb1cabc77888d1f879bc5980471d399fbec45bf5727078deaebd44d10d144e"),
                (0x1716F0, 605, "b3e09bd594aede77989dd62976f907d88bd10c69e30cb646ba79287b819c45e3")))
        self.hierarchy_enabled = self.enabled and all(
            hashlib.sha256(image.bytes_at(address, size) or b"").hexdigest() == digest
            for address, size, digest in (
                (0x8DDF0, 2218, "247190d1cd001f43646b9627fd2f538c64b15efefaad1f9510f5137f04e4dc63"),
                (0xB5B40, 339, "21273987e0276cda51a2d70b2b576abc42195b8efeff9ab4e8f552a15b323726"),
                (0xB5F60, 291, "9f10d4414ec5fb6b39f5f20f6100791e0aec209d77f6c60599402dff6bdb5d78")))
        self.pose_coalesce_enabled = self.hierarchy_enabled
        self.light_census_enabled = self.enabled and all(
            hashlib.sha256(image.bytes_at(a, n) or b"").hexdigest() == digest
            for a, n, digest in (
                (0x8D760, 218, "3d10c3ea21c7ed098485d57533f9b293e43e9096eaef7078f09399dee5f8fa2a"),
                (0x92330, 656, "78bf7b299611b6fce621ebbaa1406ee7537fa38cb1beb31f2ea7ac49442a0110"),
                (0x58CD0, 16, "a9f13e55c80e9521bdf84c9387d687d15c8435557200eb1988d4b0471ae55234"),
                (0x58440, 16, "3924ff434ffec9f8ce90fe82c10ad0c8d906da3ad177e495fd4e24e77df2a24a"),
                (0x91D10, 16, "b65838221f19be5267c558dd49e057246ac9fb23ba9f34c13db055b19aa4590d"),
                (0x92230, 16, "9e4ab43e59f6e617a0672f43b5b4f2d23e771aa95e66e5943c90578983b7779a")))

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

    def light_census_entry(self, address):
        if not self.light_census_enabled:
            return []
        if address in (0x8D760, 0x8D7A6):
            hook = "SCOPE" if address == 0x8D760 else "SUFFIX"
            return ["#ifdef XV_LIGHT_QUERY_CENSUS", f"    XV_LIGHT_CENSUS_{hook}(c);", "#endif"]
        lifetime = {0x58CD0: "BSP_SWITCH", 0x58440: "MAP_END", 0x91D10: "LIST_RESET", 0x92230: "LIGHT_DELETE"}
        if address in lifetime:
            return ["#ifdef XV_LIGHT_QUERY_CENSUS", f"    XV_LIGHT_CENSUS_CANCEL(c,XV_LC_{lifetime[address]});", "#endif"]
        return []

    def light_census_before(self, address):
        if self.light_census_enabled and address == 0x8D837:
            return ["#ifdef XV_LIGHT_QUERY_CENSUS", "    XV_LIGHT_CENSUS_END(c);", "#endif"]
        return []

    def light_census_body(self, address, body):
        # Call hooks run after the original return-address push, before callee
        # scope/guard entry. Preserve every emitted original instruction.
        if not self.light_census_enabled:
            return body
        if address == 0x92330:
            needle = "    X_PUSH32(0x925B0u);\n    f_00056670(c);"
            assert body.count(needle) == 1, "light query callsite drift"
            body = body.replace(needle, "    X_PUSH32(0x925B0u);\n#ifdef XV_LIGHT_QUERY_CENSUS\n"
                "    XV_LIGHT_CENSUS_QUERY(c);\n#endif\n    f_00056670(c);")
        if address in (0x8D760, 0x8D7A6):
            needle = "    X_PUSH32(0x8D7FFu);\n    f_000565E0(c);"
            assert body.count(needle) == 1, "light removal callsite drift"
            body = body.replace(needle, "    X_PUSH32(0x8D7FFu);\n#ifdef XV_LIGHT_QUERY_CENSUS\n"
                "    XV_LIGHT_CENSUS_REMOVE(c);\n#endif\n    f_000565E0(c);")
        return body

    def before_instruction(self, address):
        census = self.light_census_before(address)
        if census:
            return census
        if address == 0x8E0F0:
            out = []
            if self.pose_coalesce_enabled:
                out.extend(["#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_POSE_EXPERIMENT)",
                            "    XV_OBJECT_POSE_BEGIN(c);", "#endif"])
            if self.hierarchy_enabled:
                out.extend(["#ifdef XV_NATIVE_MODEL_HIERARCHY",
                            "    { extern int xv_math_model_hierarchy(xctx *); (void)xv_math_model_hierarchy(c); }",
                            "#endif"])
            return out
        if self.pose_coalesce_enabled and address == 0x8E5D0:
            return ["#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_POSE_EXPERIMENT)",
                    "    XV_OBJECT_POSE_FINISH();", "#endif"]
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
        if self.object_collect_enabled and address == 0x172034:
            return ["#ifdef XV_NATIVE_OBJECT_COLLECT",
                    "    { extern int xv_object_collect_refs(xctx *);",
                    "      if (xv_object_collect_refs(c)) goto L_00172163; }",
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
        if self.owner_phase_enabled and address in (0xFA920, 0xBCB30):
            phase = 0 if address == 0xFA920 else 1
            # Local declarations avoid changing the shared generated prototype
            # header and recompiling unrelated guest shards for this observer.
            out.extend(["#ifdef XV_OWNER_PHASE", "    /* XV_OWNER_PHASE_SCOPE: coarse primary entry only */",
                        "    extern int xv_owner_phase_enabled;",
                        "    extern void xv_owner_phase_begin(uint64_t *, void *, unsigned);",
                        "    extern void xv_owner_phase_end(uint64_t *);",
                        "    uint64_t xv_owner_phase_scope_ __attribute__((cleanup(xv_owner_phase_end))) = 0;",
                        f"    if (xv_owner_phase_enabled) xv_owner_phase_begin(&xv_owner_phase_scope_, c, {phase}u);", "#endif"])
        if address == 0x5B4A0 and self.scene_bucket0_detail_enabled and hashlib.sha256(
                self.image.bytes_at(address, 16) or b"").hexdigest() == "84e04c50371d0908bde1caf2ddc2c71a88d7d9f38fa3d66b1b12aad47785164b":
            out.extend(["#if XV_POSE_PIPELINE", "    /* XV_POSE_SCOPE: full salted model owner */",
                        "    extern unsigned xv_pose_scope_begin(void *);",
                        "    extern void xv_pose_scope_end(unsigned *);",
                        "    unsigned xv_pose_scope_ __attribute__((cleanup(xv_pose_scope_end))) = xv_pose_scope_begin(c);",
                        "#endif"])
        out.extend(cluster_lifetime.entry(self.image, address))
        out.extend(self.light_census_entry(address))
        out.extend(object_motion_profile.entry(self.image, address))
        if address == 0x170C10 and collision_solver.matches(self.image):
            out.extend(collision_solver.ENTRY)
        if address == segment_sphere.SPAN[0] and segment_sphere.matches(self.image):
            out.extend(segment_sphere.ENTRY)
        if self.pose_coalesce_enabled and address in (0x8DDF0, 0x8E087):
            out.extend(["#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_POSE_EXPERIMENT)",
                        "    XV_OBJECT_POSE_SCOPE();", "#endif"])
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
                if address == 0x56670:
                    if self.light_census_enabled:
                        out.extend(["#ifdef XV_LIGHT_QUERY_CENSUS",
                                    "    XV_QUERY_WORK_BEGIN(c, xv_object_math_locked_);", "#endif"])
                    out.extend(["#ifdef XV_WORKER_QUERY",
                                "    { extern int xv_worker_query(xctx *, int);",
                                "      if (xv_worker_query(c, xv_object_math_locked_)) goto L_000566DE; }",
                                "#endif"])
        if self.flare_enabled and address == ENTRY:
            out.append(ENTRY_HOOK)
        if address == 0xB77C0 and hashlib.sha256(
                self.image.bytes_at(address, 195) or b"").hexdigest() == "5ceeee6fc591265ef9a0e51d3cd0a0cac96b0a100d27b3e29a1e9a10be4aba73":
            out.extend(["#ifdef XV_NATIVE_POLYGON_EDGE",
                        "    { extern int xv_math_polygon_edge(xctx *); if (xv_math_polygon_edge(c)) return; }",
                        "#endif"])
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
        if self.enabled and address == 0xA1EC0 and marker_region.matches(self.image):
            body = marker_region.hook(body)
        if self.enabled and address == 0x7E530:
            body = constant_pack.hook(body)
        body = model_fog.hook(self.image, address, body)
        body = model_uv.hook(self.image, address, body)
        if self.scene_partition_enabled and address == 0x5D410:
            body = scene_partition.hook(body)
            if self.scene_bucket0_detail_enabled:
                body = scene_partition.detail_hook(body)
        if self.scene_bucket0_detail_enabled and address == 0x5B760:
            body = model_route_profile.hook(body)
        if self.scene_bucket0_detail_enabled and address == 0x5B4A0:
            body = model_route_profile.child_hook(body)
        body = self.light_census_body(address, body)
        if self.enabled and address == 0x86F50 and collision_vertices.matches(self.image):
            body = collision_vertices.hook(body)
        if self.enabled and address == segment_sphere.SPAN[0] and segment_sphere.matches(self.image):
            body = segment_sphere.hook(body)
        if self.enabled and address == 0x4C980:
            size, digest = self.object_shared[address]
            if hashlib.sha256(self.image.bytes_at(address, size) or b"").hexdigest() == digest:
                children = (0x11120,0x3A8B0,0x3D190,0x41B40,0x425D0,0x428F0,
                            0x43AF0,0x478D0,0x48090,0x48E10,0x49280,0x493E0,
                            0x4A9F0,0x4B000,0x4B170,0x4B3A0,0x4B410,0x4B580,
                            0x4B9D0,0xBDF10,0xBE050,0xBF870,0xD8B70)
                # Keep direct calls and every original stack/register effect.
                # Only an already sampled outer worker hold can time a child.
                def child_call(match):
                    child = int(match[1], 16)
                    assert child in children, "object callback child drift"
                    return ("#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_HOLD_PROFILE)\n"
                            "    { extern unsigned xv_object_hold_children_enabled;\n"
                            "      extern unsigned xv_object_hold_child_begin(int);\n"
                            "      extern void xv_object_hold_child_end(unsigned,unsigned);\n"
                            "      unsigned hold_child_ = xv_object_hold_children_enabled ?\n"
                            "          xv_object_hold_child_begin(xv_object_math_locked_) : 0;\n" +
                            match[0] + "\n"
                            f"      if (hold_child_) xv_object_hold_child_end(hold_child_,{children.index(child)});\n"
                            "    }\n#else\n" + match[0] + "\n#endif")
                body = re.sub(r"    f_([0-9A-F]{8})\(c\);", child_call, body)
        if address == 0x56670 and self.light_census_enabled:
            size, digest = self.object_shared[address]
            if hashlib.sha256(self.image.bytes_at(address, size) or b"").hexdigest() == digest:
                needle = "L_000566DE:\n"
                assert body.count(needle) == 1, "query work boundary drift"
                body = body.replace(needle, needle + "#ifdef XV_LIGHT_QUERY_CENSUS\n"
                                    "    XV_QUERY_WORK_END(c, xv_object_math_locked_);\n#endif\n")
        if self.clip_region_enabled and address == 0xB7F10:
            body = clip_region.hook(body)
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
        if self.enabled and address in collision_traversal.SPANS and collision_traversal.matches(self.image):
            body = collision_traversal.hook(address, body)
        return body

    def postprocess(self, directory):
        if not self.enabled:
            raise ValueError("Halo postprocessing requires the audited executable")
        from tools.patch_split_screen import patch
        patch(directory)
