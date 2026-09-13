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


class Halo2GraphicsHooks(NoGameHooks):
    def __init__(self, image):
        # A profile with a different executable cannot opt into these rules.
        load_profile("halo2_5849").validate_image(image)
        self.image = image

    def lower_instruction(self, emitter, instruction, output):
        section = self.image.section_of(instruction.ip)
        return bool(section and section[4] == "D3D" and
                    lower_bus_mov(emitter, instruction, output))


HOST_BOUNDARIES = {
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
