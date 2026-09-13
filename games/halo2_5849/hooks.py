"""Pinned Halo 2 diagnostic bus translation; no CE or constructor replacements."""
from iced_x86 import Mnemonic, OpKind, Register
import hashlib
from recompiler.core.hooks import NoGameHooks
from recompiler.core.profile import load_profile


_GPRS = {getattr(Register, name) for name in (
    "EAX ECX EDX EBX ESP EBP ESI EDI AX CX DX BX SP BP SI DI AL CL DL BL AH CH DH BH".split())}


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

    def function_entry(self, address):
        boundary = HOST_BOUNDARIES.get(address)
        if boundary is None:
            return []
        name = boundary[2]
        return [f"    {{ extern void {name}(xctx *); {name}(c); return; }}"]
