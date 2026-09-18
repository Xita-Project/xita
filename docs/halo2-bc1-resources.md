# BC1 input capture at the second post-intro quad

Native183 reaches the original `17FC=7` at `03B7BA18` after the first screen
quad and the polygon-stipple assignment. The next target is 320×240, color
offset `02B48000`, pitch1280, with depth/stencil/blend disabled. All four
texture controls are enabled (`4003FFC0`); each format is `03310C29`, each
offset is `018F1A80`, and addressing/filter words are `00010101`/`02022000`.
The texture shader selects four PROJECT2D stages. The seven-slot vertex
program is unchanged, but this pass has a different three-stage combiner.
These observations do not establish accepted draw support.

The pinned [xemu method definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h)
identify color format `0C` as DXT1. Its
[texture layout calculation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/texture.c)
and the [S3TC format specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt)
identify 8-byte 4×4 blocks. The observed one-level 8×8 image therefore occupies
32 bytes. This change resolves that complete read-only span through the
original selected DMA, validates permissions and bounds, and preserves every
compressed byte. It does not decode texels or accept any draw.

The existing BC2 reader retains its 64-byte format contract. Both wrappers use
a common checked block-view helper with separate exact format/extent values.
Tests cover all four units and both DMA selectors, every format-bit change,
missing state, absent callbacks, disabled units, incorrect object permissions,
DMA/physical end bounds, wrapping guest and host addresses, and mapping
failures. Failed calls leave the output structure and all input/guest memory
unchanged. All 46 host executables and both texture/command-state ASan/UBSan
checks pass.

At a strict stop, `boot.c` writes separate private files
`texture0-dxt1-at-stop.bin` through `texture3-dxt1-at-stop.bin` when each input
resolves. Header version1 contains physical address, dimensions, block-row
pitch, byte count, original format, and layout2 (row-ordered 8-byte blocks).
No capture runs in the normal draw loop. Captures and packages include owned
game data and must remain private; diagnostic VPKs must not be uploaded as
releases.

Native184 verifies all 173 dependency targets, repeats the original intro and
first screen-effect commit, and stops at the same BEGIN. Its decoded channel
is byte-identical to native183. Each of the four BC1 files is complete and
byte-identical (same original allocation), SHA256
`fbfbec57446a47e2fb7312b3e4b0f24d73ba094be9ab73e39f954e4c36a4ac61`.
The last presented frame135 remains black, SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
No menu is visible. The next task is an isolated GPU/CPU comparison for this
BC1 repeat/linear sampling and combiner route, before original command
execution is enabled.

Native184 ELF SHA256:
`99b7de2cef804090d5adf4d8a95c123871676ffde375170c5a215414ab74f261`;
EBOOT `f99f1a63b93e47cac554563662b4efa35ff447d6a95fd31c1eb5e1025cca2ade`;
trace `dcae2bbd5d737c4347cfb707f145e6f0495e0db14a5b2d511290dfb88c812b5d`;
channel `6b4d7cbcdbc07b6cea821c3fb3b5b81b8857ce1790ac55b49325fbfbb0d97634`;
push `f59b53c00623691ad83bc71c7c21767cc542918c8c858f2f65e2f8be9f3b71be`.
Private artifacts are `native-184-artifacts/`, with visible-intro evidence in
`native-184-view/early-movie-middle.png`, and build/source records in
`bc1-capture/`. All are excluded from Git. The shared emulator and CE/hardware
paths remain unchanged.
