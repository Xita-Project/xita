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
from games.halo2_5849.hooks import (reviewed_sparse_jump_roots, reviewed_widget_kind_roots,
                                  reviewed_widget_field_roots)


HOST_CALLBACK_WALK = (0x3FBA54, 135, "e0cc1649c0b744615b3de0f5b2446411bb408d0b3ce4d1d3da59980219abc70c")
STREAM_VTABLE = (0x417170, 28, "72cb68310880069f79f94d33bfd78c04aae9b0b48e2d4b623612bd88cd1a53c9")
GAME_SOUND_VTABLES = (
    (0x47F0D0, 0x45711C, 36, "3390e861a1a9ebe7c9da27fbef73da9982814ad5696894b63d9aed83c665c38b"),
    (0x47F088, 0x457140, 28, "cfe13e6e922ae34f6ec6ffdfae387fecb3c865fe164f75f1be70c9004d969120"),
    (0x47F0F0, 0x45715C, 36, "5176d30bf8e4cea56c2a70787eed63a1704718b78f13a40bcd04edd9074be25e"),
)


def game_sound_owner_roots(image):
    """Original stream-format/selected sound owners observed in initialization."""
    roots = set()
    for owner, address, length, digest in GAME_SOUND_VTABLES:
        if image.u32(owner) != address:
            raise ValueError("Halo 2 game sound owner binding mismatch")
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 game sound owner vtable fingerprint mismatch")
        for slot in range(address, address + length, 4):
            target = image.u32(slot)
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError("Halo 2 game sound owner target is not title code")
            roots.add(target)
    return roots


def audio_stream_roots(image):
    """Seven original stream interface slots; unknown APIs retain strict guards."""
    address, length, digest = STREAM_VTABLE
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 stream vtable fingerprint mismatch")
    roots = set()
    for slot in range(address, address + length, 4):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != "DSOUND":
            raise ValueError("Halo 2 stream vtable target is not DSOUND code")
        roots.add(target)
    return roots


XPP_CALLBACK_WALK = (0x408C72, 36, "9234a2afaedda5206ca55c2bf3f0269b70b1b0345091581c639ee86230cd3756")
GAME_INIT_WALK = (0x137C84, 19, "44c1c20adf4bb014714a0825d601e592e668699e31671c7676a674951946da02")
GAME_MAP_WALKS = (
    (0x137CC5, 34, "c8aab7f2f289627cdbf8915607639cd18538016fe0b7f4e3103dd4bbef21eeff"),
    (0x137D0D, 24, "9cb66f3dce939462fe6ed66f2a48cf29c9f7984c06084164cdcaef1866c90598"),
    (0x137D7B, 28, "a96f07c6fb9bebebfe3c9c11c3991c16c39ea99e73e03f0e281734cf1549cf06"),
    (0x137DA1, 24, "602be11a670b15871af4b680707d5daef495dfe3c96e1397204b8e5f580dd7d2"),
)
GAME_RESOURCE_WALKS = (
    (0xD48A1, 29, "0791d3646b316f6c184aabc27feed7cb52baae825f512b69619b288d7e6e06c2"),
    (0xD48D2, 28, "0590129ef3b973f39b577ebb22e78a02e28b830fd15d1810386c95aab9c7738d"),
    (0xD4DB7, 25, "1bfc7777b7aa974c521ed9f9a02992ddd431597af8c68e9dc9d1070988637ef8"),
)
GAME_DESCRIPTOR_WALK = (0x1088E0, 124, "c1bf2193fbf5a7f7a8d0de9fffaaf9ee5cbe12ced08b39059af29896540348ec")
GAME_DESCRIPTOR_MAP_WALKS = (
    (0xB69D8, 31, "4850979303eb86790589e6a32dd3bcce89e4d8f58d809678e55869c4f3e51d67"),
    (0xB6AB7, 31, "ea694745b9044ee364ab532d8e5b9ff513a30b50df1127666c9adab5beb432aa"),
)
GAME_DESCRIPTOR_CHILD_WALKS = (
    (0x1088A0, 54, "d6eb603c5f02b788e35ca7cf7c3d9deb074bf7de9ec5713dd13f713a0e296db3"),
    (0x108960, 101, "7ea8925cd8f074e3813192c97d502b93db4457e76596f1aca5d36acb5bd8439c"),
    (0x1089D0, 101, "49878cdd6bea74a1154ff2dc9294513ada0489a089e7cc09c892790dc6d03fa3"),
    (0x108A40, 66, "b80567cf17df1eb4cf294790c5ef58f37dec29e321ff31a4743b8287ae436101"),
    (0x108A90, 122, "afa08763dab7cffe0330b76bdf026aec29abfe8d8f9ac67117d07b7f13c4b3c3"),
)
GAME_PACKED_VECTOR_BINDINGS = (
    (0x279BA2, 45, "ae5b4f05401786d52eb8183057ed4ce7f7ed8b9c4b38b3d45510b1d9a291b6fa"),
    (0x279C6F, 70, "d9fe85669bb7394f774d95bbce234fa3950a02301b12e6b67ca0962456a6f2c5"),
)
GAME_MODE_WALK = (0x18EF00, 152, "c499facfbe49993ebd3e15bb55a4f65adafb4bfd53eb99474ba7bb96ad3f8102")
GAME_INTERFACE_REGISTRATION = (0x3769F0, 45, "46e548c6c8f362dc1ba57b6f7581a1b2c0bffb4b2cb9c2e812dcc7b9544b1611")
GAME_ONLINE_INTERFACE_DISPATCH = (0x59949, 41, "c68f2b75408f155325c537d83f024b3ff580f096399c7606ca83a739654555a1")
BINK_PIXEL_DISPATCH = (
    (0x3EDB70, 80, "67b6419123998c1cce93a7f29fb8630684b4f2813e8ab27d5cc6c1cc884535e0"),
    (0x3E97E0, 1013, "57ec72883b4d8abe0ef2170d0e79202c3a5b70a7da216051a2d25e1d3fa72db0"),
)
GAME_DISPATCH_CONSTRUCTORS = (
    (0x23546B, 27, "cbd17bebf8667c708be45cbe65a4fc6dfc672448edf8c83151e4173d16e69e71"),
    (0x234E43, 33, "84924bde2768f01fd262d3d0cd0916038e22c201d8200008e05c9cdff85bd95f"),
)
GAME_ALLOCATOR_CONSTRUCTORS = (
    (0x81EC2, 6, "12ddc689a0e651bd82e523e0305410591c34c0ecd1da8fda19d119363ba1520f"),
    (0x32A871, 6, "090c672f0e3b32b564eec33dbae02580b53d91bf1c9d4e1eb0b378b0d2faa881"),
)
GAME_STATE_CONSTRUCTORS = (
    (0x376A20, 10, "ea990a0b2671f169030c7ed2bac29c98c5c5e42fd4bafcb4d5fcf86da5ff161c"),
    (0x58E70, 105, "9195f198f6de23c02ee21bf68539b3898e5bad37deff50056be9c0b6b35b1e29"),
)
GAME_STARTUP_WIDGET_CONSTRUCTORS = (
    (0x2B7269, 32, "80197ecb7c788fcdef80d420ace1c23bb3b5b063704d5a2de1b3f0a9a663e90c"),
    (0x2B7388, 6, "9a1381c2d5cae2abe6b261ed8da05f4b4ffb50cd3f83001353654c83aab7ad4c"),
)
GAME_TEXT_WIDGET_CONSTRUCTORS = (
    (0x22F561, 30, "f3dbfe2397a2ad7ad4d86f0fbba196e80b76143b455fc7890bb58ee932689ecd"),
    (0x22F583, 30, "0af4f3515f9b6b34f00727c0be7369e6e47f14a998372d1a5495982d9ce851e0"),
    (0x22F4FA, 19, "a27a5c5dad3bab9e240ea5d61bb3be35633de873c4e7f8c9de1251d55fd80775"),
    (0x22F532, 19, "2ec44c870c40e778e72b6f3d4a7a2b37fa8ab37ae974256d86e49fe9920be02e"),
)
GAME_WIDGET_PROPERTY_DISPATCH = (0x2373BE, 56, "863a8fd2961e9fd5ceab6711205222d5cf403957a5512768286bceb656ca4c61")


def game_mode_callback_roots(image):
    """Native47: eight paired callbacks, two stride-eight walks of 40h bytes."""
    address, length, digest = GAME_MODE_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 mode callback walk fingerprint mismatch")
    roots = set()
    for slot in range(0x453C00, 0x453C40, 4):
        target = image.u32(slot)
        if target == 0:
            continue
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 mode callback slot {slot:#x} has invalid target")
        roots.add(target)
    return roots


def _constructor_vtable_roots(image, constructors, start, end):
    for address, length, digest in constructors:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 dispatch constructor fingerprint mismatch")
    return _code_vtable_roots(image, start, end)


def _code_vtable_roots(image, start, end):
    roots = set()
    for slot in range(start, end, 4):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 dispatch vtable slot {slot:#x} has invalid target")
        roots.add(target)
    return roots


def game_registered_interface_roots(image):
    """Native51: four static interface objects registered by the same initializer."""
    address, length, digest = GAME_INTERFACE_REGISTRATION
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 interface registration fingerprint mismatch")
    roots = set()
    for slot, instance, table in ((0x417370, 0x462E00, 0x417378),
                                  (0x4173E4, 0x462E10, 0x4173E8),
                                  (0x41745C, 0x462E28, 0x417460),
                                  (0x4174D0, 0x462F30, 0x4174D8)):
        if image.u32(slot) != instance or image.u32(instance) != table:
            raise ValueError(f"Halo 2 interface binding {slot:#x} mismatch")
        roots.update(_code_vtable_roots(image, table, table + 27 * 4))
    return roots


def game_dispatch_vtable_roots(image):
    """Native46 object vtable, bounded by two original constructor assignments."""
    return _constructor_vtable_roots(image, GAME_DISPATCH_CONSTRUCTORS, 0x4599A8, 0x4599DC)


def game_allocator_vtable_roots(image):
    """Native48: allocator at 454970, followed by the table assigned at 32A871."""
    return _constructor_vtable_roots(image, GAME_ALLOCATOR_CONSTRUCTORS, 0x454970, 0x454980)


def game_state_vtable_roots(image):
    """Native58: one constructor assigns eleven adjacent four-method state tables."""
    return _constructor_vtable_roots(image, GAME_STATE_CONSTRUCTORS, 0x450990, 0x450A40)


def game_startup_widget_vtable_roots(image):
    """Native146: constructor 2B7269 assigns the table used at 234E2C.

    The adjacent table is independently assigned at 2B7388. Include only the
    intervening 28 executable slots, including observed slot48h -> 2B7289.
    """
    return _constructor_vtable_roots(image, GAME_STARTUP_WIDGET_CONSTRUCTORS, 0x45BC60, 0x45BCD0)


def game_text_widget_vtable_roots(image):
    """Native147: outer getter returns member+74h; caller invokes member slot4.

    The two original factory branches construct the same outer interface and
    one of two three-method text members. Root only their executable prefixes;
    the following zero words and adjacent object tables remain untouched.
    """
    roots = _constructor_vtable_roots(image, GAME_TEXT_WIDGET_CONSTRUCTORS, 0x458940, 0x458984)
    for start, end in ((0x4588B0, 0x4588BC), (0x458930, 0x45893C)):
        roots.update(_code_vtable_roots(image, start, end))
    if any(image.u32(end) != 0 for end in (0x458984, 0x4588BC, 0x45893C)):
        raise ValueError("Halo 2 text interface boundary mismatch")
    return roots


def game_widget_property_roots(image):
    """Native148: original caller checks 0 <= index < 70h and skips nulls.

    Each nonnull entry supplies the original property kind/offset/scale. Keep
    the table's explicit holes and execute the callbacks without replacement.
    """
    address, length, digest = GAME_WIDGET_PROPERTY_DISPATCH
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 widget property dispatch fingerprint mismatch")
    roots = set()
    for slot in range(0x470828, 0x4709E8, 4):
        target = image.u32(slot)
        if target == 0:
            continue
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 widget property slot {slot:#x} has invalid target")
        roots.add(target)
    return roots


def game_online_interface_roots(image):
    """Native59: the same original update calls two statically bound interfaces."""
    address, length, digest = GAME_ONLINE_INTERFACE_DISPATCH
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 online interface dispatch fingerprint mismatch")
    for instance, table in ((0x47708C, 0x450B68), (0x47712C, 0x450B44)):
        if image.u32(instance) != table:
            raise ValueError(f"Halo 2 online interface binding {instance:#x} mismatch")
    return _code_vtable_roots(image, 0x450B44, 0x450B88)


def bink_pixel_callback_roots(image):
    """Native68: the original wrapper passes this mixed converter descriptor.

    3E97E0 copies selected function fields into the converter dispatch globals.
    Six scalar/function pairs and sixteen later function words are distinct
    from the intervening mutable selection counters; those are never roots.
    """
    for address, length, digest in BINK_PIXEL_DISPATCH:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 Bink pixel dispatch fingerprint mismatch")
    base = 0x57A080
    for offset, value in ((0, 4), (4, 2), (12, 2), (20, 3), (28, 2), (36, 2), (44, 3)):
        if image.u32(base + offset) != value:
            raise ValueError("Halo 2 Bink pixel descriptor shape mismatch")
    roots = set()
    for offset in (*range(8, 0x34, 8), *range(0x74, 0xB4, 4)):
        target = image.u32(base + offset)
        section = image.section_of(target) if target else None
        if (not target or not image.is_code(target) or not section or
                section[4] not in {"BINK32", "BINK32M", "BINK32X2", "BINK32MX"}):
            raise ValueError(f"Halo 2 Bink pixel callback {base + offset:#x} has invalid target")
        roots.add(target)
    return roots


def game_descriptor_initialization_chain(image):
    """Native45: reproduce the bounded original descriptor linking algorithm.

    Children are not recursively traversed. A shared/self child is appended
    only while its current next pointer is zero; later parent writes can
    replace that pointer. Do not substitute ordinary graph deduplication.
    This extraction supports the pinned image's initially unlinked records.
    It never changes the image or the original guest list construction.
    """
    address, length, digest = GAME_DESCRIPTOR_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 descriptor walk fingerprint mismatch")
    nodes = {}

    def validate_node(node):
        if node in nodes:
            return
        section = image.section_of(node) if node else None
        if (not node or node & 3 or not section or section[4] != ".data" or
                node + 0xC8 > section[0] + section[2] or
                len(image.bytes_at(node, 0xC8)) != 0xC8):
            raise ValueError(f"Halo 2 descriptor {node!r} has invalid data span")
        if any(node < other + 0xC8 and other < node + 0xC8 for other in nodes):
            raise ValueError("Halo 2 descriptor records overlap")
        if image.u32(node + 0xC4) != 0:
            raise ValueError("Halo 2 descriptor has unsupported initial link")
        target = image.u32(node + 0x10)
        section = image.section_of(target) if target else None
        if target is None or (target and (not image.is_code(target) or not section or section[4] != ".text")):
            raise ValueError(f"Halo 2 descriptor {node:#x} has invalid initializer")
        nodes[node] = target

    parents = []
    children = {}
    for slot in range(0x468630, 0x468664, 4):
        parent = image.u32(slot)
        validate_node(parent)
        parents.append(parent)
        children[parent] = []
        for index in range(16):
            child = image.u32(parent + 0x84 + 4 * index)
            if child == 0:
                break
            validate_node(child)
            children[parent].append(child)
    links = {node: 0 for node in nodes}
    links[0] = 0  # synthetic head slot, corresponding to guest [4E0330]
    tail = 0
    for parent in parents:
        links[tail] = parent
        tail = parent
        for child in children[parent]:
            if links[child] == 0:
                links[tail] = child
                tail = child
    links[tail] = 0
    chain, seen = [], set()
    node = links[0]
    while node:
        if node in seen or node not in nodes:
            raise ValueError("Halo 2 descriptor chain is cyclic or invalid")
        seen.add(node)
        chain.append((node, nodes[node]))
        node = links[node]
    return chain


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


def game_descriptor_map_roots(image):
    """Native85: map setup/cleanup follow the existing chain via node+C4.

    Reuse the original linking algorithm, including shared/self children.
    Only callback fields18/1C are read; the original loops skip nulls.
    """
    for address, length, digest in GAME_DESCRIPTOR_MAP_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 descriptor map walk fingerprint mismatch")
    roots = set()
    for node, _ in game_descriptor_initialization_chain(image):
        for offset in (0x18, 0x1C):
            target = image.u32(node + offset)
            if target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 descriptor map callback {node + offset:#x} has invalid target")
            roots.add(target)
    return roots


def game_descriptor_child_roots(image):
    """Native153: four callbacks walk each catalog parent's direct children.

    Validate the existing descriptor spans/link shape, but do not filter by
    the linked initialization chain: these callers use the child arrays.
    Require their null terminator before the mutable link field at C4h.
    """
    for address, length, digest in GAME_DESCRIPTOR_CHILD_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 descriptor child walk fingerprint mismatch")
    game_descriptor_initialization_chain(image)
    roots = set()
    for slot in range(0x468630, 0x468664, 4):
        parent = image.u32(slot)
        for index in range(16):
            child = image.u32(parent + 0x84 + index * 4)
            if child == 0:
                break
            for offset in (0x20, 0x24, 0x28, 0x2C):
                target = image.u32(child + offset)
                if target == 0:
                    continue
                section = image.section_of(target) if target else None
                if not target or not image.is_code(target) or not section or section[4] != ".text":
                    raise ValueError(f"Halo 2 descriptor child callback {child + offset:#x} has invalid target")
                roots.add(target)
        else:
            raise ValueError("Halo 2 descriptor child array is not terminated before its link")
    return roots


def game_packed_vector_roots(image):
    """Native154 reaches format 1 of the 40-byte decoder record table.

    Original binders select either triplet at +0/+12; both are proven for
    this one row. Do not infer other format rows or treat metadata as code.
    The whole-image revision gate also protects the observed row contents.
    """
    for address, length, digest in GAME_PACKED_VECTOR_BINDINGS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 packed vector binding fingerprint mismatch")
    roots = set()
    for slot in range(0x47FB4C, 0x47FB64, 4):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 packed vector callback has invalid target")
        roots.add(target)
    return roots


def game_map_callback_roots(image):
    """Native83: four per-map lifecycle fields in the same 68-record table.

    Two forward walks cover ESI=0..0x990, stride0x24; their paired reverse
    walks cover the same records. Each original walk skips null callbacks.
    Other descriptor fields are not interpreted as code, and the original
    order, calls and state writes remain in generated code.
    """
    for address, length, digest in GAME_MAP_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 map callback walk fingerprint mismatch")
    roots = set()
    for base in (0x440DE0, 0x440DE4, 0x440DE8, 0x440DEC):
        for slot in range(base, base + 0x990, 0x24):
            target = image.u32(slot)
            if target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 map callback slot {slot:#x} has invalid target")
            roots.add(target)
    return roots


def game_resource_callback_roots(image):
    """Native84: three original three-record walks, each at stride0x38.

    Only the called fields at 4674A4, 4674A8 and 4674B8 are followed. The
    metadata, other lifecycle fields and following records are not scanned.
    """
    for address, length, digest in GAME_RESOURCE_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 resource callback walk fingerprint mismatch")
    roots = set()
    for base in (0x4674A4, 0x4674A8, 0x4674B8):
        for index in range(3):
            slot = base + index * 0x38
            target = image.u32(slot)
            if target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 resource callback slot {slot:#x} has invalid target")
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
    audio = parser.add_mutually_exclusive_group()
    audio.add_argument("--audio-unavailable", action="store_true", help="diagnostic only: DirectSoundCreate returns DSERR_NODRIVER (requires --host-channel)")
    audio.add_argument("--audio-host", action="store_true", help="bounded real-output audio device adapter (requires --host-channel and AUDIO_HOST=1)")
    args = parser.parse_args()
    if args.audio_unavailable and not args.host_channel:
        parser.error("--audio-unavailable requires --host-channel")
    if args.audio_host and not args.host_channel:
        parser.error("--audio-host requires --host-channel")
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
        descriptor_chain = game_descriptor_initialization_chain(image)
        roots.update(target for _, target in descriptor_chain if target)
        roots.update(game_dispatch_vtable_roots(image))
        roots.update(game_mode_callback_roots(image))
        roots.update(game_allocator_vtable_roots(image))
        roots.update(game_registered_interface_roots(image))
        roots.update(game_state_vtable_roots(image))
        roots.update(game_startup_widget_vtable_roots(image))
        roots.update(game_text_widget_vtable_roots(image))
        roots.update(game_widget_property_roots(image))
        roots.update(game_online_interface_roots(image))
        roots.update(bink_pixel_callback_roots(image))
        roots.update(reviewed_sparse_jump_roots(image))
        roots.update(reviewed_widget_kind_roots(image))
        roots.update(reviewed_widget_field_roots(image))
        # Native49: 1A474C passes the global arena object 47D924 to 18E1F0.
        # Its stored vtable is 4508FC: allocate/free, followed by string data.
        if image.u32(0x47D924) != 0x4508FC:
            raise ValueError("Halo 2 global arena vtable binding mismatch")
        for slot in (0x4508FC, 0x450900):
            target = image.u32(slot)
            if not target or not image.is_code(target):
                raise ValueError(f"Halo 2 global arena slot {slot:#x} is not code")
            roots.add(target)
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
        roots.update(game_map_callback_roots(image))
        roots.update(game_resource_callback_roots(image))
        roots.update(game_descriptor_map_roots(image))
        roots.update(game_descriptor_child_roots(image))
        roots.update(game_packed_vector_roots(image))
        # Native42: 0x66305 calls [ [0x477058] + 0x10 ]; the pinned record
        # is 0x467140, whose callback is 0x662E0 (ten-byte original body).
        roots.add(image.u32(image.u32(0x477058) + 0x10))
        # Native43: arena allocator call 0x14B4C2 uses the two-slot table
        # at 0x453498 (allocate/free); the following word begins string data.
        roots.update(image.u32(slot) for slot in (0x453498, 0x45349C))
        # Native44: D486D..D4885 selects three records at 4674A0, stride38h.
        # The loop calls only each record's first field and skips nulls.
        for slot in (0x4674A0, 0x4674D8, 0x467510):
            target = image.u32(slot)
            if target:
                if not image.is_code(target):
                    raise ValueError(f"Halo 2 resource initializer {slot:#x} is not code")
                roots.add(target)
    output = args.out.resolve()
    generated = output / "generated"
    profile = str(Path(__file__).with_name("graphics-profile.json")) if args.graphics else "halo2_5849"
    if args.host_channel:
        profile = str(Path(__file__).with_name("host-channel-profile.json"))
    if args.audio_unavailable:
        profile = str(Path(__file__).with_name("audio-unavailable-profile.json"))
    if args.audio_host:
        profile = str(Path(__file__).with_name("audio-host-profile.json"))
        # Header AddRef/Release are reached through the exact original vtable.
        roots.update((0x37A14F, 0x37C70F))
        roots.update(audio_stream_roots(image))
        roots.update(game_sound_owner_roots(image))
    subprocess.run([sys.executable, "-m", "recompiler", str(args.xbe.resolve()),
                    "--profile", profile, "--no-data-roots", "--trace-calls", "--trace-funcs",
                    "--files", "128", "-o", str(generated),
                    "--roots", f"{image.entry:X}", *(f"{root:X}" for root in sorted(roots - {image.entry}))], cwd=ROOT, check=True)
    # xv_game_main is the startup entry in this entry-only diagnostic target;
    # boot.c only uses xv_entry_point. No game-main boundary is asserted here.
    (output / "startup-roots.json").write_text(json.dumps(sorted(roots), indent=2) + "\n")
    if args.host_channel:
        (output / "descriptor-initializers.json").write_text(json.dumps(descriptor_chain, indent=2) + "\n")
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
