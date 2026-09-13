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
    if instruction.ip != SPARSE_JUMP_IP:
        return False
    if (instruction.mnemonic != Mnemonic.JMP or instruction.op0_kind != OpKind.MEMORY or
            instruction.memory_base != Register.NONE or instruction.memory_index != Register.EAX or
            instruction.memory_index_scale != 4 or instruction.memory_displacement != SPARSE_JUMP_TABLE):
        raise ValueError("Halo 2 sparse jump instruction shape mismatch")
    # Use the existing generic indirect-tail-jump form. It reads the actual
    # table word, does not push a return address, and faults null/unknown targets.
    output.append(f"    xv_call(c, {emitter.operand(instruction, 0, 4)}); return;")
    return True


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

    def lower_instruction(self, emitter, instruction, output):
        return (lower_sparse_jump(emitter, instruction, output) or
                lower_fp_environment(emitter, instruction, output) or
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
