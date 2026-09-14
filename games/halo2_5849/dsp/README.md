# Halo 2 DSP interpreter provenance

The `interp/` files and `debug.h` are adapted from xemu commit
[`75650bd8cd91945f7b79774e2cee0b200ca373ff`](https://github.com/xemu-project/xemu/tree/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/dsp).
Original copyright and GPL-2.0-or-later notices are retained. Xita's repository
license is GPL-3.0. No Xbox code, monitor, effect program, key or game image is
included in this directory.

Only the portable C instruction interpreter and disassembler are reused. The
QEMU device, JIT, DMA implementation, scheduling and audio output are not linked.
`compat/` supplies standard C includes, explicit little-endian word access and
no-op trace instrumentation. Interpreter assertions stay enabled and transfer
control to the owning engine's terminal fault handler; they cannot return a
successful guest result. Engine calls are serialized because the upstream opcode
cache is process-global.

Two semantic interpreter changes from the pinned source (an extra trailing
blank line is also removed):

- Out-of-range X-memory reads fault instead of returning upstream's tentative
  `FFFFFF` value.
- Sign extension shifts unsigned before converting to signed, fixing an observed
  UBSan signed-left-shift error without changing the intended bit result.

The separate `../dsp_engine.c` implements only the observed monitor's scratch DMA
encodings and frame/interrupt protocol, with explicit range/control checks and
instruction/chain budgets. Unsupported registers, FIFO, interleaving and formats
are terminal. Format-2/bit-9 scratch representation and the three-poll DMA status
observation follow the pinned reference; neither establishes exact MCPX timing.
The counter at `FFFFB3` reports interpreter cycles only at the four reviewed
monitor profiling reads. It does not claim to reproduce the Xbox hardware timer.
See [the initialization audit](../../../docs/halo2-dsp-interpreter-init.md) for
scope, counter noninterference checks and native evidence.
