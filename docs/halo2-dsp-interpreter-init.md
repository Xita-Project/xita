# Halo 2 original DSP initialization probe

The owned GP monitor and all 15 effect programs now execute in an isolated
portable interpreter on host and ARM in Vita3K. The real monitor transfers code
and state, writes its own command acknowledgment, executes effect work and halts
at its frame boundary. This checkpoint is a private DSP probe, not a new game API
success, menu screen or audio-effects playback implementation. The game remains
at [native106's visible intro and blocked menu sound setup](halo2-visible-intro.md).

## Original loader and DSP evidence

Original XDK function `37E1B4` copies 1,484 monitor bytes from `386B38` to GP scratch
zero through `383D79`. The monitor SHA-256 is
`c2527379062a138b53dbaadcb3f9ac5762409ff0e38d2f320aea47f8a3ffd8e8`.
The effect image's zero prefix is therefore not its bootstrap program.
Original `383EF2` and `383F1F` decode the key table and 15 effect-code ranges in a
private Unicorn address space. Every call must reach its checked return address
with the expected stack cleanup. All decoded program words are 24 bits. The
original four-byte unencrypted code trailer is retained. The prepared image
SHA-256 is `106f71e7bc4917afc931c7bc0ac1ef6ae991e31206de295e7078aca6a671cda7`.
No keys or original instruction bytes appear in source or tests.

The monitor loads 14,208 code bytes from scratch `818`, then 11,880 state bytes
from `3F98`, and acknowledges command three through a completed DSP-to-scratch
transfer. It executes the uploaded entry at P:`171`. State is placed at X:`80`;
code/state pointers in original descriptor relocation must account for these
DSP word addresses versus x86 byte addresses. Queries for effects 4–7, byte
offset `20`, return the eight actual interpreted state bytes corresponding to
`0,30`. Effects 11–14 change another state field from zero to `4000`, demonstrating
why accepting unexecuted file metadata would not establish initialized effects.

The C core is pinned to the [xemu interpreter](https://github.com/xemu-project/xemu/tree/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/dsp/interp).
The new bounded transfer engine follows the pinned
[DMA reference](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/dsp/dsp_dma.c)
for observed non-interleaved unit-step scratch controls `59E0`, `59E2`, `59D2`,
`49E2`, `4BD2`, `4BD0`. Unknown controls, unreviewed register accesses, FIFO and
unmapped memory remain faults. A fault poisons the candidate; no acknowledged
image or usable state is returned. Completed earlier transfers are not rolled
back after a later fault. Image validation bounds code/state/maps/scratch before
execution; node validation bounds the complete source and destination before its
first transfer. Instruction and descriptor-chain limits prevent infinite loops.

## Counter and timing limits

The original monitor writes `FFFFB2=FFFFFF`, `FFFFB0=1`, `FFFFB1=1`; four reads of
`FFFFB3` feed profiling words X:`7C..7F`. In this engine the value is explicitly
interpreter cycles modulo 24 bits, not an assertion about MCPX timer hardware.
Reads are permitted only at reviewed monitor PCs `2D`, `33`, `3A`, `4D` and setup
only at `23`, `25`, `27`. The decoded monitor's timer-dependent branches rejoin
before subsequent effect work. Private comparison with constant zero, constant
maximum, descending, wrapping and scrambled counters found differences only in
those four profiling words; effect RAM, program/Y RAM and transfer count matched.
The scrambled case executes two extra profiling instructions, as expected.

Transfers are synchronous. The reference's three-poll RUNNING observation is
retained because the original monitor observes start before awaiting completion;
EOL is set only after the actual checked transfer. No Xbox cycle-accurate DMA,
APU voice engine, 48 kHz scheduling, full precision mixing or audible effects
claim follows. Format-2/bit-9 representation follows the pinned emulator and
needs hardware-independent waveform validation before broader routing support.

## Validation and native probe

Synthetic tests cover X/Y/P transfers, circular wrapping/writeback, 24-bit input
masking, unsupported encodings, X holes, Y/P endpoints, scratch bounds/overflow,
cyclic chains, unknown registers, unreviewed profiling readers, invalid code,
truncated/malformed images, no invented acknowledgment and bounded effect reads.
31,744 sign-extension cases cover widths 1–31; the unsigned-shift correction fixes
an error observed by UBSan in the unchanged reference. Synthetic and private owned
execution both pass ASan/UBSan. All 33 H2 host executables and the extra channel
modes pass. Source does not modify shared Halo CE runtime or its default target.

The new engine's X/Y/P RAM after three frames compares byte-for-byte with the
private pinned-reference harness. The separate Vita3K app `XH2D00001` runs the same
monitor/effects and produces the same instruction, transfer and query results:

- Three halted frames: bootstrap plus two effect frames.
- 62,974 instructions, 140,442 logical cycles, 207 completed transfers.
- Final PC `002B`, command zero, 15 effects, 823,296 scratch bytes.
- Effects 4–7 at offset `20`: `00000000,0000001E`.
- Effects 11–14 at offset `20`: `003E8FA0,00004000`.

The native report includes a canonical state fingerprint over DSP registers,
stack, X/Y/P RAM, mix buffers and scratch, excluding host pointers. This provides
an additional host/ARM comparison, not cryptographic authentication. Both report
`33F95FA8B7503F58`. The final probe completed in 21,237 microseconds in Vita3K;
this is one initialization measurement, not sustained processing or Vita hardware
performance.

| Final private probe artifact | SHA-256 |
|---|---|
| ELF | `d971bd9c7eac8f250bde65c913d36d93ca66ddc4154235dcf92170e5ebd6cb69` |
| EBOOT | `c24e452f1cf1843cbcef66820e52933e073ab88b200c7ef40a42796caa37ba84` |
| VPK | `e0f7039c1f86f125b456f920eff9d0d8dd3176f96497fd0419d39ffacbb85a20` |
| Result | `52a170aedbbd4adf088f93ceeffdfa81eb22b1ab88d540a32a4fc7addf7c6098` |

## Private reproduction

Prepare and run only with a matching owned executable; `prepare_dsp.py` validates
the profile's full XBE SHA-256 before executing its decoder. It requires the same
Python environment plus Unicorn. Choose a new private output path:

```sh
python games/halo2_5849/prepare_dsp.py /private/default.xbe --out /private/dsp/halo2-dsp.bin
make -C games/halo2_5849 -j4 dsp-probe BUILD=/private/dsp/build DSP_ASSET=/private/dsp/halo2-dsp.bin
make -C games/halo2_5849 test-host BUILD=/private/h2-tests
```

The probe package contains owned DSP code and must not be uploaded or distributed.
Install only in the isolated Vita3K lab and launch `XH2D00001`. It writes
`ux0:data/xita-halo2/dsp-probe.txt` and exits. In this session the private evidence
is `../private/dsp-bringup/`, including `native-probe/`,
`native-probe-result.txt`, `engine-check.log`, `counter-independence.json`,
`dsp-engine-san.log`, `owned-engine-san.log` and `all-host.log`.
The emulator is stopped after the probe. The opt-in game download/query integration
is recorded in [the native107 checkpoint](halo2-dsp-game-init.md); unsupported
playback routes still stop pending DSP mixer/output integration.
