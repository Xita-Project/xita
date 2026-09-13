"""Pinned Halo 2 diagnostic bus translation; no CE or constructor replacements."""
from iced_x86 import Mnemonic, OpKind, Register
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
