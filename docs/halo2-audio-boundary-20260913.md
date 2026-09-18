# Halo 2's next native boundary: Xbox audio hardware

Native attempt 40 executes the previously missing sound-object AddRef and
stops at actual MCPX APU access `FE801100` in function `00384A36`. The exact
instruction is `00384A60`, a byte TEST of the front-end control register.
Following code checks sequencer state at `FE802000`, polls voice-processor
FIFO space at `FE820010`, and submits commands. This cannot be implemented by
pretending one status read succeeded. No audio device/register is modeled.

The only added discovery roots are the observed four-slot sound-object vtable
`4170E4..4170F4` (exclusive end): destructor, AddRef, Release, delete helper.
All four targets are code in the DSOUND section. The following words are data.
The existing whole-XBE SHA-256 gate applies before these roots are read. No
broad data-root scan was enabled.

The diagnostic guest-address check now rejects the MCPX APU aperture
`FE800000..FE880000`, in addition to NV2A and OHCI. The owned executable supplies
the addresses; the pinned [xemu APU device](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/apu.c)
defines its 512 KiB aperture, and its
[register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/apu_regs.h)
identify the observed front-end and sequencer controls. Unsupported hardware
must not alias the runtime's unmapped-memory trash page.

Native input initialization and opening the built-in controller still pass.
There is still no Halo 2 menu, geometry, or sound. The only scanout is the
earlier black startup framebuffer. The next bounded task is an audited public
DirectSound creation/submission adapter with a real host audio backend, or a
separately labeled investigation of the original game's audio-error handling.
Neither exists in this milestone.

All 14 host executables and three existing bounded callback-root tests pass.
Native 40 was built with four jobs, guest O0/runtime O1, launched on the private
`:111` lab, captured, archived and stopped. The source changes affect only Halo
2's diagnostic target. Private evidence is `native-40-artifacts`,
`native-40-view`, `native-milestone-40.json`, `native40-apu-boundary.txt`, and
`apu-boundary-tests.log` beneath the Halo 2 private directory.

| Native 40 artifact | SHA-256 |
| --- | --- |
| ELF | `7ef1df84480f03ae92452ce54f707ae8e67203bc0e91141939e69a8744ca1acc` |
| EBOOT | `c89d0389d4a857548a0a220b9322a2b153a47ee718a321d8e55a2b16fb1bb1ef` |
| Boot trace | `fa0d33799b6f7005c6302970c89196f2ff970632eedfbc6cc1374f09241706e4` |

Game-derived bytes, captures and diagnostic packages remain private. The VPK
embeds owned game code/image and must not be uploaded as a distributable release.
