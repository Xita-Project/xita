"""Compatibility boundary for the existing audited CE generated inputs.

Retire this layout only with fresh CE helper and full-image qualification.
Halo 2 uses the general discovery algorithm and checked write lowering.
"""
from recompiler import xita_recomp as r


class HaloDiscovery(r.Discovery):
    """Preserve the block shape used by the qualified CE native helpers.

    The general lifter deduplicates overlapping tails for Halo 2. Changing this
    helper's graph changes compiler scheduling of NaN operands, so retain its
    independently audited graph until that replacement passes ARM equivalence.
    Other profiles retain the general lifter's deduplicated graph.
    """
    rewrite_memory_stores = False

    def retained_dead_flags(self, insns, dead):
        # Existing CE generated callers were qualified with instruction-value
        # equality. Preserve their source contract during integration; migrate
        # these bodies to IP-based liveness in a separate full-image rebuild.
        values = {ins for ins in insns if ins.ip in dead}
        return {ins.ip for ins in insns if ins in values}

    def split_blocks(self, fn):
        targets = {target for block in fn.blocks.values() for target in block.succ}
        for start in sorted(fn.blocks):
            block = fn.blocks[start]
            for index, ins in enumerate(block.insns):
                if index and ins.ip in targets and ins.ip not in fn.blocks:
                    tail = r.Block(ins.ip)
                    tail.insns, tail.end, tail.succ = block.insns[index:], block.end, block.succ
                    block.insns, block.end, block.succ = block.insns[:index], ins.ip, [ins.ip]
                    fn.blocks[ins.ip] = tail
                    break

