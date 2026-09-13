# Halo 2 graphics bus bootstrap — 2026-09-13

The original guest device constructor now passes the earlier `0xFD001804`
stop in native Vita3K. It enables PCI bus mastering, disables display/timer
interrupts, reads device identity and memory size, and configures its PCI
latency/ROM fields. **No successful CreateDevice return, rendered frame or
title/menu has been observed.** The next strict stop is a master-control read
at `0xFD000200`, guest instruction `0x401C43`.

## Selected translation

`prepare_boot.py --graphics` selects the separate hash-pinned
`graphics-profile.json`. The existing `halo2_5849` profile remains discovery
only. The graphics adapter changes only scalar 32-bit `MOV` memory operands
inside this image's `D3D` section into explicit bus reads/writes. The original
constructor and its control flow still run. Other instructions retain the
checked guest-pointer path and stop if they access GPU MMIO.

The core gains an optional instruction-lowering hook whose default returns
false. CE inherits that default; its selected hooks and normal lowering remain
unchanged. Selecting the Halo 2 adapter for another executable is rejected by
full-image/profile validation before generation.

Ordinary memory accesses through the new bus helpers still read/write the
actual guest pages, including copies crossing separately mapped pages. NV2A
accesses go to a small stateful register model. Invalid alignment, unknown
registers, unsupported writes and accesses after PCI memory decode is disabled
stop with the instruction, address, direction and reason. They do not read the
runtime's unmapped-memory trash page.

## Modeled register subset

Offsets below are relative to NV2A's `0xFD000000` BAR.

| Offset | Behavior |
| --- | --- |
| `0x1800` | NV2A PCI device/vendor identity, read only |
| `0x1804` | PCI command state; only I/O, memory and bus-master bits accepted |
| `0x1808` | VGA class/revision identity, read only |
| `0x180C` | PCI cache-line size and latency timer; unsupported header/BIST writes rejected |
| `0x1830` | Expansion-ROM disable; mapping a ROM is unsupported |
| `0x10020C` | Framebuffer memory size, matching this target's 64 MiB guest physical memory |
| `0x600140` | Display interrupt disable/readback; enabling unsupported without an event source |
| `0x9140` | Timer interrupt disable/readback; enabling unsupported without a timer/alarm source |

The virtual boot configuration enables PCI memory decoding, with bus mastering
disabled until requested by the guest. This is an explicit model configuration,
not a captured retail power-on register snapshot. Disabling memory decode also
blocks the MMIO PCI mirror; re-enabling then needs a PCI-configuration path,
which this limited bus implementation does not provide yet.

Register identities and meanings were checked against primary xemu sources at
commit `75650bd8cd91945f7b79774e2cee0b200ca373ff`:
[device identity](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a.c),
[PCI mirror](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pbus.c),
[memory size](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pfb.c),
[display interrupt control](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pcrtc.c)
and [timer control](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/ptimer.c).
The implementation is deliberately smaller: unmodeled addresses are fatal,
including addresses for which another emulator currently returns a default.

## Native evidence and validation

Native attempt 09 observed these accesses in the original guest code:

| Guest instruction | Access | Value |
| --- | --- | --- |
| `0x3FE16B` | Read PCI command `0xFD001804` | `2` |
| `0x3FE173` | Write PCI command | `6` |
| `0x3FE17A` / `0x3FE184` | Disable display/timer interrupts | `0` |
| `0x3FE192` | Read device/vendor | `0x02A010DE` |
| `0x3FE1A7` | Read class/revision | `0x030000A1` |
| `0x3FE1B9` | Read framebuffer memory size | `0x04000000` |
| `0x3FE1D9` | Disable expansion ROM | `0` |
| `0x3FE1E3` | Set cache-line/latency fields | `0xF800` |
| `0x401C43` | Read master control `0xFD000200` | Rejected; no value supplied |

Synthetic tests execute generated x86 code through the real bus helpers. They
cover register/data aliasing, arithmetic flags around a PCI command update,
absolute and indirect accesses, unchanged guest memory across rejected writes,
normal copies across guest pages, strict stop diagnostics, and wrong-image
adapter rejection. Register tests cover reset/readback, unsupported write
isolation, invalid sizes/addresses and PCI memory decode disable. The five
Python modules below pass 14 tests; the shared host suite and title-local cache
and register tests also pass.

```sh
python games/halo2_5849/prepare_boot.py /path/to/owned/default.xbe --graphics --out /private/graphics-bus
make -C games/halo2_5849 -j4 GUEST_OPT=-O0 \
  GENERATED=/private/graphics-bus/generated BUILD=/private/graphics-bus/build \
  IMAGE=/private/graphics-bus/halo2_image.bin
make -C games/halo2_5849 test-host
make -C recomp/host test
python -m unittest tools.test_halo2_gpu_bus tools.test_guest_address_check tools.test_game_profiles tools.test_loop_branches tools.test_sse_half_moves
```

Use the [isolated launch procedure](halo2-native-boot-20260912.md) and its own
title `XH2B00001`, game/save paths and display `:111`. The diagnostic package
embeds owned executable code/image data and must not be uploaded as a
distributable release. All assets, generated C, packages and complete traces
remain private and outside Git.

A repeated native run, attempt 10, follows the same register values and strict
stop after the PCI memory-decode disable behavior was covered by tests. Its
private artifact manifest is `private/native-milestone-10.json`. SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `c0937e50883ebdbe77e881d039b6662741af11dcbcf40dcf853bf483369f83ab` |
| EBOOT | `0bb2f97f5feb1bd3f9abf5c8264ab1208da3332c8cba4866f2804c515cf88787` |
| VPK | `5c8bab6bde5a72fd62fefa7b5563c08854fb45596b6a0366a481c8fa5e44761d` |
| Native trace | `5d7b1083236c963035bbc0789332f4e562079cc8e5b918736dcf4bfc7d555c5e` |

This change adds the title-local adapter, register model, bus helpers and
synthetic tests, plus an inert default instruction hook in the shared emitter.
The original Halo 2 discovery profile and Halo CE adapter retain their existing
lowering behavior.

The next bounded task is master-control engine enable/reset and clock-register
semantics, then another native trace. DMA objects, command processing, GPU
interrupt delivery and actual rendering remain unimplemented. Modeled register
progress must not be described as graphics output.
