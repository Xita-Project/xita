"""Pinned Halo 2 diagnostic bus translation; no CE or constructor replacements."""
from iced_x86 import Mnemonic, OpKind, Register
import hashlib
from recompiler.core.hooks import NoGameHooks
from recompiler.core.profile import load_profile


_GPRS = {getattr(Register, name) for name in (
    "EAX ECX EDX EBX ESP EBP ESI EDI AX CX DX BX SP BP SI DI AL CL DL BL AH CH DH BH".split())}

SPARSE_JUMP_IP = 0x18EBDD
SPARSE_JUMP_TABLE = 0x18EC08
SPARSE_JUMP_GUARDS = (
    (0x18EB80, 100, "85bfb44438536da9c07ee5e19acdea5a49c29502b71c96b3214736fd4b29902a"),
    (0x18EC08, 24, "93324ac5bd2d4025f18ded90bf69be6bdce3f496bd293bc14b2948db69676743"),
)
WIDGET_KIND_JUMP_IP = 0x216B01
WIDGET_KIND_JUMP_TABLE = 0x216B30
WIDGET_KIND_JUMP_GUARDS = (
    (0x216B00, 48, "0717fb224fbb378fd62e0c4c59f813e1be584356d8821d70e7d5391a84e012b1"),
    (0x216B30, 36, "5f702a0b28f1e274ee1cbf2018a6ca465050a4bfcf0d44f715d748aa0a6b770e"),
)
WIDGET_FIELD_JUMP_IP = 0x216A54
WIDGET_FIELD_JUMP_TABLE = 0x216A88
WIDGET_FIELD_JUMP_GUARDS = (
    (0x216A50, 56, "e59cb4e3aba68854c97772c9fbd695217fb19d4e4caa305730d804b5d7c7b835"),
    (0x216A88, 36, "3a12572e353c32577d20ce9d33cf70adfc65dd9b8f3227b14b40c0209baf7005"),
)
INLINE_QUEUE_STATUS_GUARD = (
    0x12D0CF, 26, "76248680a6285ea26e18db9d8eddf97bdf349746e8281507c8876dfd43090850")
INLINE_QUEUE_STATUS = {
    0x12D0D5: (Mnemonic.MOV, Register.ECX, 0x3240),
    0x12D0DB: (Mnemonic.CMP, Register.ECX, 0x3244),
    0x12D0E3: (Mnemonic.MOV, Register.EAX, 0x400700),
}


def reviewed_sparse_jump_roots(image):
    """Native52: a six-entry table with null holes, not a contiguous prefix."""
    for address, length, digest in SPARSE_JUMP_GUARDS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 sparse jump fingerprint mismatch")
    roots = set()
    for index in range(6):
        target = image.u32(SPARSE_JUMP_TABLE + index * 4)
        if target == 0:
            continue
        if not image.is_code(target):
            raise ValueError("Halo 2 sparse jump target is not executable")
        roots.add(target)
    return roots


def lower_sparse_jump(emitter, instruction, output):
    index = Register.EAX
    if instruction.ip == SPARSE_JUMP_IP:
        table = SPARSE_JUMP_TABLE
    elif instruction.ip == WIDGET_KIND_JUMP_IP:
        table = WIDGET_KIND_JUMP_TABLE
    elif instruction.ip == WIDGET_FIELD_JUMP_IP:
        table, index = WIDGET_FIELD_JUMP_TABLE, Register.ECX
    else:
        return False
    if (instruction.mnemonic != Mnemonic.JMP or instruction.op0_kind != OpKind.MEMORY or
            instruction.memory_base != Register.NONE or instruction.memory_index != index or
            instruction.memory_index_scale != 4 or instruction.memory_displacement != table):
        raise ValueError("Halo 2 sparse jump instruction shape mismatch")
    # Use the existing generic indirect-tail-jump form. It reads the actual
    # table word, does not push a return address, and faults null/unknown targets.
    output.append(f"    xv_call(c, {emitter.operand(instruction, 0, 4)}); return;")
    return True


def reviewed_widget_kind_roots(image):
    """Native150: nine original kind mappings; slots 2 and 5 are null holes."""
    for address, length, digest in WIDGET_KIND_JUMP_GUARDS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 widget kind jump fingerprint mismatch")
    roots = set()
    for index in range(9):
        target = image.u32(WIDGET_KIND_JUMP_TABLE + index * 4)
        if target == 0:
            continue
        if not image.is_code(target):
            raise ValueError("Halo 2 widget kind jump target is not executable")
        roots.add(target)
    return roots


def reviewed_widget_field_roots(image):
    """Native151: field44h uses an ECX-indexed table with null holes 4 and 5."""
    for address, length, digest in WIDGET_FIELD_JUMP_GUARDS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 widget field jump fingerprint mismatch")
    roots = set()
    for index in range(9):
        target = image.u32(WIDGET_FIELD_JUMP_TABLE + index * 4)
        if target == 0:
            continue
        if not image.is_code(target):
            raise ValueError("Halo 2 widget field jump target is not executable")
        roots.add(target)
    return roots


def lower_fp_environment(emitter, instruction, output):
    if instruction.mnemonic not in (Mnemonic.STMXCSR, Mnemonic.LDMXCSR):
        return False
    registers = {Register.NONE, Register.EAX, Register.ECX, Register.EDX, Register.EBX,
                 Register.ESP, Register.EBP, Register.ESI, Register.EDI}
    if (instruction.op0_kind != OpKind.MEMORY or instruction.op_count != 1 or
            instruction.memory_base not in registers or instruction.memory_index not in registers or
            instruction.memory_displ_size == 2 or instruction.segment_prefix == Register.GS):
        raise ValueError("Halo 2 MXCSR instruction shape mismatch")
    name = "h2_stmxcsr" if instruction.mnemonic == Mnemonic.STMXCSR else "h2_ldmxcsr"
    output.append(f"    {{ extern void {name}(xctx *, uint32_t, uint32_t); "
                  f"{name}(c, 0x{instruction.ip:08X}u, {emitter.addr(instruction)}); }}")
    return True


def lower_bus_mov(emitter, instruction, output):
    """Preserve scalar MOV semantics while making reads/writes explicit to HLE.

    Other instructions retain the checked guest-pointer path and stop if they
    touch MMIO. This is deliberately not a general instruction/device emulator.
    """
    ins = instruction
    if (ins.mnemonic == Mnemonic.MOVZX and emitter.op_size(ins, 0) == 4 and
            ins.op0_kind == OpKind.REGISTER and ins.op1_kind == OpKind.MEMORY):
        width = emitter.op_size(ins, 1)
        if width not in (1, 2):
            return False
        output.append(f"    {{ extern uint32_t h2_bus_read{width * 8}(xctx *, uint32_t, uint32_t); "
                      f"{emitter.operand(ins, 0, 4)} = h2_bus_read{width * 8}(c, 0x{ins.ip:X}u, {emitter.addr(ins)}); }}")
        return True
    if ins.mnemonic != Mnemonic.MOV:
        return False
    width = emitter.op_size(ins, 0)
    if width not in (1, 2, 4):
        return False
    if ins.op0_kind == OpKind.MEMORY:
        source = emitter.operand(ins, 1, width)
        if ins.op1_kind == OpKind.REGISTER and ins.op1_register not in _GPRS:
            return False
        output.append(f"    {{ extern void h2_bus_write{width * 8}(xctx *, uint32_t, uint32_t, uint32_t); "
                      f"h2_bus_write{width * 8}(c, 0x{ins.ip:X}u, {emitter.addr(ins)}, {source}); }}")
        return True
    if (ins.op0_kind == OpKind.REGISTER and ins.op1_kind == OpKind.MEMORY and
            ins.op0_register in _GPRS):
        output.append(f"    {{ extern uint32_t h2_bus_read{width * 8}(xctx *, uint32_t, uint32_t); "
                      f"{emitter.operand(ins, 0, width)} = h2_bus_read{width * 8}(c, 0x{ins.ip:X}u, {emitter.addr(ins)}); }}")
        return True
    return False


def lower_bus_compare(emitter, instruction, output):
    """Read a scalar CMP memory operand through the same checked bus as MOV."""
    ins = instruction
    if ins.mnemonic != Mnemonic.CMP:
        return False
    memory = [i for i in range(2) if ins.op_kind(i) == OpKind.MEMORY]
    width = emitter.op_size(ins, 0)
    if len(memory) != 1 or width not in (1, 2, 4):
        return False
    other = 1 - memory[0]
    if ins.op_kind(other) == OpKind.REGISTER and ins.op_register(other) not in _GPRS:
        return False
    operands = [emitter.operand(ins, i, width) for i in range(2)]
    operands[memory[0]] = f"h2_bus_read{width * 8}(c, 0x{ins.ip:X}u, {emitter.addr(ins)})"
    ctype = f"uint{width * 8}_t"
    output.append(f"    {{ extern uint32_t h2_bus_read{width * 8}(xctx *, uint32_t, uint32_t); "
                  f"{ctype} a_ = {operands[0]}, b_ = {operands[1]}; "
                  f"X_FLAGS(XK_SUB, a_, b_, ({ctype})(a_-b_), {width * 8}); }}")
    return True


def lower_inline_queue_status(emitter, instruction, output):
    """Native78: three inline reads after the original channel flush.

    These use the same live PUT/GET/busy values as the D3D section. All other
    instructions in the game function keep their original checked lowering.
    """
    expected = INLINE_QUEUE_STATUS.get(instruction.ip)
    if expected is None:
        return False
    mnemonic, register, displacement = expected
    if (instruction.mnemonic != mnemonic or instruction.op_count != 2 or
            instruction.op0_kind != OpKind.REGISTER or instruction.op0_register != register or
            instruction.op1_kind != OpKind.MEMORY or instruction.memory_base != Register.EAX or
            instruction.memory_index != Register.NONE or
            instruction.memory_displacement != displacement or instruction.segment_prefix != Register.NONE or
            emitter.op_size(instruction, 0) != 4 or emitter.op_size(instruction, 1) != 4):
        raise ValueError("Halo 2 inline queue-status instruction shape mismatch")
    return lower_bus_mov(emitter, instruction, output) or lower_bus_compare(emitter, instruction, output)


class Halo2GraphicsHooks(NoGameHooks):
    def __init__(self, image):
        # A profile with a different executable cannot opt into these rules.
        load_profile("halo2_5849").validate_image(image)
        self.image = image

    def lower_instruction(self, emitter, instruction, output):
        section = self.image.section_of(instruction.ip)
        return bool(section and section[4] == "D3D" and
                    (lower_bus_mov(emitter, instruction, output) or
                     lower_bus_compare(emitter, instruction, output)))


HOST_BOUNDARIES = {
    0x4098C0: (5, "9561ed51279ef9f1745a95cdc2983dffdffddc77f72490c0a5633693bfdcbd1f", "h2_input_init"),
    0x409932: (86, "bfb4e066121c5f7f7a06d73dbbb718944f287af76bfecea43cd50054d5dbc226", "h2_input_open"),
    0x409988: (12, "57f31931e7501e76294d43919c37ad52bb9ed9a16078c6a9058b32fa53f09203", "h2_input_close"),
    0x409994: (472, "66d193ca4aaf3159e9e9e2e789c2505f451bace610b47f186ac702b977658f67", "h2_input_capabilities"),
    0x409B6C: (115, "1d0e98d392433bce9ad7ca82f285a16ce3355d3ac8f2179299eb55827cd293a2", "h2_input_state"),
    0x409BDF: (51, "2bbc1fbb891fbf9c9acb1941fa38d5a9cf75545f6917f0653429da4d3d89dab2", "h2_input_feedback"),
    0x3FE4CB: (436, "2d6efe5e6fab632c7e291a17ec657f774d20f7952bcbf759278e62df7995c214", "h2_host_miniport_shutdown"),
    0x3FE005: (352, "3c7fccae26a9a87e47c60a69737aa6cde7340fee827b8e4b0338e96236947b4e", "h2_host_miniport_init"),
    0x4026CE: (410, "8d205a7f9f747353088695cf6df386bff659143e376c211663e9d74713fb56a0", "h2_host_channel_configure"),
    0x3FADE0: (45, "97d24aa2909c3ed0a59eb665c9759882cc01544230ea133ac77d1c1c532afc81", "h2_host_memory_barrier"),
    0x3FE86A: (189, "82181aef2b426ef189ef1f62ef5bc57dd2442bc42e7d53ce891d8845f974add0", "h2_host_tile_remove"),
    0x3FE67F: (491, "a8fca6c67a2271b28909086e5b1e4632cb26bcee87b0cc0e1e80b5e7b038565c", "h2_host_tile_configure"),
}


class Halo2HostChannelHooks(Halo2GraphicsHooks):
    def __init__(self, image):
        super().__init__(image)
        for address, (length, digest, _) in HOST_BOUNDARIES.items():
            if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
                raise ValueError(f"Halo 2 host channel boundary mismatch at {address:#x}")
        reviewed_sparse_jump_roots(image)
        reviewed_widget_kind_roots(image)
        reviewed_widget_field_roots(image)
        address, length, digest = INLINE_QUEUE_STATUS_GUARD
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 inline queue-status fingerprint mismatch")

    def lower_instruction(self, emitter, instruction, output):
        return (lower_sparse_jump(emitter, instruction, output) or
                lower_fp_environment(emitter, instruction, output) or
                lower_inline_queue_status(emitter, instruction, output) or
                super().lower_instruction(emitter, instruction, output))

    def function_entry(self, address):
        boundary = HOST_BOUNDARIES.get(address)
        if boundary is None:
            return []
        name = boundary[2]
        return [f"    {{ extern void {name}(xctx *); {name}(c); return; }}"]


class Halo2AudioUnavailableHooks(Halo2HostChannelHooks):
    """Explicit diagnostic error-path probe; supplies no audio device."""
    def __init__(self, image):
        super().__init__(image)
        if hashlib.sha256(image.bytes_at(0x37D797, 71)).hexdigest() != "937701608e296f3edcb1b3b77221d14015ee71e5b094256ba8fe1f50b76d7a0c":
            raise ValueError("Halo 2 DirectSoundCreate fingerprint mismatch")

    def function_entry(self, address):
        if address == 0x37D797:
            return ["    { extern void h2_audio_unavailable(xctx *); h2_audio_unavailable(c); return; }"]
        return super().function_entry(address)


AUDIO_HOST_BOUNDARIES = {
    0x37BA6F: (511, "7fbe183d305c3622314ae67063fd432e11afcce608562ddc2d364b6ffed91cca"),
    0x37B60D: (42, "44c132bd6ab2aea37ba22efdbf45a52e6553badae5d956d0dcc057e6bb55c2bc"),
    0x37AD25: (119, "7a182f2613dd476b6f5c86e101220a58cc389291b4acf5f4fce67aa9775d3bc4"),
    0x37D835: (87, "51775a4ae6d24d39db3ab4f2b8b66a190acba4bfffeed8f358aa4738dde0fd2e"),
    0x37D7DE: (87, "fba5b0697871b05f1c503ba189363fbceb37cc38b63e1a4dd0b427a660cb35be"),
    0x37B68B: (28, "5c0809339e8b04993352a3357f95cfda7dc11ead13d354d701ad997f272f52cd"),
    0x37C6E5: (32, "ebc647fb92ed1dad3a4e54a336bf325642e44338cb6767ef2e6f6be852b8a2ca"),
    0x37C620: (36, "b111ff5f50a8c9b0639aa305ad9d836e2e8e6a23affc4e58fb2513292e9b1f3b"),
    0x37C644: (36, "52fa9699a6894b34de89a5d68109697505d96e7976e4a1f3db4c0e055d1dbd5b"),
    0x37C6C1: (36, "257239c4361dd2e4334aaf63cf93a08d17cecb6e6f82ea01998a252d0dea9d15"),
    0x37C69D: (36, "1d6e1295dec2de1711b56a7486e50bf6bc9a52c519f1a88025c3d2606dfebedb"),
    0x37C600: (32, "034d262e6d09469e990f2d7a7e764a67681daeb0277ffdfc43a1b16183b032d6"),
    0x37D4E2: (36, "b01d17ea2962e3cd6a2247e8177ae43569b526a389444c2243215b22b43f54b0"),
    0x37AB40: (71, "0a41ff8dec44754af8b35f93ad01fa6ba2b62ff1e89b726f649fbd66da5a61fa"),
    0x37AB87: (78, "ff6b794e19a3386dfa4fef7560271460d5c5383918792393ea3cba256e2fbbac"),
    0x37B818: (5, "e5a903bca1b1a62664a083b4ba5ea58bce072bc2009bc02214f9d068981b9493"),
    0x37D598: (53, "11f3ebc6cebe38a0556eab50c525ab2bff3db2c43b13f48203bc6fa5fcfa8a88"),
    0x37D54E: (74, "c6c983ec615fe09776f47623d30b9c89230ce38602e521e8ec6a6976d4335afd"),
    0x37B5E6: (39, "6eccd3ff1e5974f6b4713514dc26c5f30caea37efa8bc8c9a4702b994f602cb3"),
    0x37B7B3: (48, "fc44b79ccde4fce54139e0db9a2d3b34c0af666e38d9363c0f935d480d1bde1b"),
    0x379F40: (5, "e123f60e9fc6e974d1381f2f15fb19e7960628cc8925d65e344c2f2bdc64f424"),
    0x37C5E4: (28, "7288d510a713749cb047a24899eff5e8c6f2535cbb01f923ada5764d605501c5"),
    0x37B6C3: (28, "73830059a6d6dbf64dbb0994e2515c1af2c9b482e8bdd44f82cd206d1191e614"),
    0x37B6DF: (36, "a17acc34f95c42184adf9b8358685c5786d376096e3e001960cd3ea673e84cdf"),
    0x37B703: (24, "98c7d0ea4384e7c62b0da83d228fc5451f7490839016b9c0a0aee8dfdec6d785"),
    0x37B75B: (28, "680f71631d4b31fb3c63170a4718a9da26670c5ae6e032ffcd88e0dd949c3e35"),
    0x37B797: (28, "a62c7816ddd7eca894ee66b02240910e0bcc9fa1103626071bf1c13e3bc63e76"),
    0x37B777: (32, "480268179bb41c9501b344e0d150b1615af698de767221586e6e6aa73c1207ad"),
    0x37D4BE: (36, "1e031345f578d8e9a006024dd3965019ce536af7c4010febb53397685aa543f5"),
    0x37CC4A: (32, "78a7f3aac8962233ca024758d5b866b7c58da0fa6099fcfe8e1420bbef2f033e"),
    0x379F45: (22, "d3270be9d4962cdcdb62ef4c3242b41b960c1f0cb7621e035393e8b7607de92c"),
    0x37A795: (74, "5b0d7aeb517875dbc44378585de0558a3933bffe42d178a357ab4f274f29c94d"),
    0x37C5C8: (28, "093366e133157c00dfbd587c7c41fa96b6bec546a6428a9b32117a91eb0e107c"),
    0x37B66F: (28, "c6aebe191b2b4cdc717d348e89ee35298b240fa29289607795f2c2de1f21dd53"),
    0x37B6A7: (28, "0a794b8ec207fdb882d1baabfe6327f112952d73364ebe86a3d7f7cee761b458"),
    0x37B86D: (514, "152b5769f3630d38073a25543bbce4d35f387ad3af712ad1b96f5427794f2b67"),
    0x37D52A: (36, "ae868f50868faf0f465131b3bbd5e9535658ab3b345614c5c4274ce28e5d8166"),
    0x37B637: (32, "9cd8e91683d0bd423aefc6a5ec381a25191ffc3ea7cd0a7f3c3a91933b2e3dcf"),
    0x37D797: (71, "937701608e296f3edcb1b3b77221d14015ee71e5b094256ba8fe1f50b76d7a0c"),
    0x37B5AE: (28, "a1220eefc06488fc180381e054af0cc9236b398f28a89b131070c3295b0cb14e"),
    0x37D506: (36, "34794564c8e0c6e58dd39ddfd60f7f14fef847e45956a12f290d01aed8fbf134"),
    0x37D5CD: (36, "45d4369e05e88c3204372c8367ead01af52e9eb7bc690972f082c4169eb68c6b"),
    0x37B5CA: (28, "26b77b362863d3f2dbe17b5d83ddcd88b467e09b9e132e48a340aa1c30014a44"),
    0x37A14F: (71, "b22c0d65d399f848fc77cbc2eff4a9fb22fef16b59503eb903309af5536c35d9"),
    0x37C70F: (201, "f37d2bbd41311fc416c2b8903348f7277202a97ec15b333b4549be1359a89484"),
}

# These execute original generated code after a read-only runtime guard. They
# configure algorithm pointers or invoke the audited common Release header;
# none of the selected HRTF processing bodies is HLE'd.
AUDIO_REVERB_ORIGINAL_BOUNDARIES = {
    0x379D4B: (9, "ae4a39f1f86ea5a10dc0f01ad19c2ede2c9ddf1aed58b048c690bc9f8fdebf83"),
    0x383167: (78, "d35c455c71b156256dee1f51d47eba2a54645641cbfda856e5f0e3ef06ee1704"),
    0x3831B5: (142, "2dc7f1b6e32fac60a4b5d4fef08286800f0d1ff51ba1099cf5693cd72766028e"),
    0x383243: (57, "f81572723928d72033f1c93530a77a7ab1d4d43f0ab1a85260b90cd4554634b5"),
    0x38327C: (30, "10f033e83f520551f87b3ed1889a6fec501bfe343b0a8df225d9847192b870ae"),
    0x38329A: (56, "866714e8d1adba1ec0cac3cf84436beed2afda0259cfe7a2314d92c3946dd963"),
    0x3832D2: (154, "11eac0dd3b81f8239248e530bd67dfee7d1f3d1286f4da45bc17a3b252156292"),
    0x38336C: (125, "20d2fdb36658d474603f988ea4aca7e486691435e99f8d21d21dcfbdde0a649d"),
    0x3833E9: (77, "5d23b9cb18e208196c59d122638b199e8bbbd84c83f3a5f08d1b9d5d9f106350"),
    0x383436: (189, "bd95df87f5b45e9df520438842e0e9ef21f25d7276615b4943bad4d566804a82"),
    0x38357F: (97, "32c364673a64610131a7e10c00c2a06ba6d09a95a3a1c3425f0cd933fc2441e9"),
    0x3835E0: (171, "c7376af8264943a26dbe2b354d502e7b134203e31f530fcfbc108ab656ac46cb"),
    0x3837BA: (234, "c54581e440dd7f2945f857f5fbc5ab78f7eb77ab8932c41e0ef158cc924aa91e"),
    0x3838A4: (904, "1ce2988d1ca62104653fabb0fdd306ef276f84c7402fe1c2ff94eb69fad4642f"),
}


AUDIO_ORIGINAL_BOUNDARIES = {
    0x37B844: (41, "edd97faddb280cb5fe23b85740d14540772206c0cc5af4f08e0cdb1b9b7931b7"),
    0x379F2A: (22, "0a72b625b8e9e1a301ddf3c84ec304eded77fc8886c86f41f03efd6fa1fac2b5"),
    0x379F5B: (31, "7f397c78fa9ddaa3fc01f85707ae08b470294586a72dacef5c6dec704dea2f73"),
    0x379E9E: (30, "c66d5fef481a4aa92bb4e6df72e3f1e2cfb1fc0a9f0bc0ac8ff03aeaf34c597c"),
    0x37E126: (111, "18c533b0df59f16b85abb24c514759e0eb47802cf4dd639bc51389c711aec77c"),
}


# Original callback bodies execute unchanged; validate their exact revision.
AUDIO_CALLBACK_BOUNDARIES = {
    0x335D82: (23, "db7607cb0403e7efdfad4a9b00c8d7efe11fc3455c3e28f42a1f88062a2fa730"),
    0x335D38: (74, "0c58e0d92dfe7751dc98c874bba3122866519fc5ae8dfa8413118e92c731609c"),
    0x33586D: (56, "d0fb8c3a3c75ca6c1ea906433a99919888b0b43d6bf441b174cf774d3ca14f2e"),
}


class Halo2AudioHostHooks(Halo2HostChannelHooks):
    """Opt-in, bounded real-output device adapter; unknown methods stop."""
    def __init__(self, image):
        super().__init__(image)
        self.image = image
        for address, (length, digest) in (AUDIO_HOST_BOUNDARIES | AUDIO_ORIGINAL_BOUNDARIES | AUDIO_CALLBACK_BOUNDARIES | AUDIO_REVERB_ORIGINAL_BOUNDARIES).items():
            if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
                raise ValueError(f"Halo 2 audio host boundary mismatch at {address:#x}")
        if (image.u32(0x417124) != 0x37A14F or image.u32(0x417128) != 0x37C70F or
                image.u32(0x417154) != 0x37A14F or image.u32(0x417158) != 0x37A795):
            raise ValueError("Halo 2 sound reference vtable mismatch")

    def function_entry(self, address):
        if address in AUDIO_HOST_BOUNDARIES:
            return [f"    {{ extern void h2_audio_host_call(xctx *, uint32_t); h2_audio_host_call(c, 0x{address:X}u); return; }}"]
        section = self.image.section_of(address)
        if section and section[4] == "DSOUND":
            return [f"    {{ extern void h2_audio_guest_entry(xctx *, uint32_t); h2_audio_guest_entry(c, 0x{address:X}u); }}"]
        return super().function_entry(address)
