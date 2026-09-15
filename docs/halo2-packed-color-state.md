# Halo 2 packed-color state and read-only BC2 capture

Native195 completed the original luminance strip, then stopped at array-format
method `1768 = 00000040`, GET `03B7F010`, PUT `03B80158`. The following inline
quad has not executed. Its first two retained attributes are float2; the third
format describes four unsigned bytes in D3D color order.

The pinned [NV2A definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h#L1138)
assign type 0 to UB_D3D, bits 4–7 to component count, and bits 8–31 to stride.
The corresponding [vertex binding](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/vertex.c#L102)
uses normalized unsigned bytes with BGRA channel order. This change retains
that exact four-component format and its full stride, independently for every
attribute. It does not fetch, unpack, normalize or emit any vertex. Other new
types/counts and unsupported geometry methods still reject.

The stop-only texture diagnostic now captures each enabled one-level BC2
texture independently, including the observed `03A10E29` 1024×8 allocation.
Logical powers of two from 1 through 4096 per axis are permitted for capture;
each physical axis occupies at least one four-texel block. The
[pinned compressed-size calculation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/texture.c#L166)
and [upload path](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/texture.c#L484)
establish rows of 16-byte BC2 blocks. Thus 1024×8 needs 8192 compressed bytes.
The read-only helper validates every required state word, exact format flags,
DMA read permission, inclusive DMA limit, complete physical span and returned
host span before exposing the view. Cube, depth, multiple mips and larger
dimensions reject. Neither sampler semantics nor GPU decoding is provided by
this diagnostic. Existing executable BC1/BC2 readers still require exactly 8×8.

All 53 host executables and command-state/texture-view ASan/UBSan checks pass.
Tests cover every bounded pair of logarithmic dimensions, both DMA selectors,
all four texture units, sub-block dimensions, exact/short limits, returned
pointer overflow, invalid flags and unchanged state/output on failure. Large
test spans deliberately fail the real small host mapper; they are not backed
by invented storage. State tests verify only the selected register and validity
bit change, with no mapping or guest/resource mutation. All nine existing draw
and dispatcher objects are byte-identical to native195 after rebuilding.

The native package remains a private diagnostic containing owned game code
and image; it must not be uploaded as a distributable release. All generated
code, compressed textures, packages and traces remain outside Git. Only H2's
isolated `:111` lab is used. These state/capture changes do not establish a
visible original main menu.

Native196, built with all 179 dependency targets verified, accepts the exact
state and stops at the following original `BEGIN_END = 8` (quads): header
`03B7F048`, GET `03B7F04C`, PUT `03B80158`, caller `003FAC58`. No quad was
accepted. The original luminance strip still completes first. The new capture
contains all 8192 bytes at physical `002C7000`, 1024×8, block pitch 4096,
format `03A10E29`; its file SHA256 is
`d4d48eaf9037a98054ff42ea3542cbcb6139ef477b016927dd8cfa8862c2684b`.
Original Microsoft Game Studios intro and terminal screenshots were viewed;
last presented frame 135 remains black, SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
The normal startup driver waited for the complete 59,670,016-byte original map
copy and sent the normal Start input. It archived and stopped owned PID 3472257.

Private evidence is `native-196-artifacts`, `native-196-view` and
`packed-color-state`. ELF SHA256:
`771d3139b362b6c2c83f4807d3edd13d5e8943d206ea1cd3142e8136cb7d41fb`;
EBOOT:
`78cc73392c8291fae3df26d68a01c30096ca158d0825cf79375869fe0dc47315`;
trace:
`a365599f155b381a87fcb1d2d9d5e1bfe6b5a6c7bccb71c61a36ea02eb8550a9`;
channel:
`0f4b86d50e00eca9206a05c73231125fc2736679582a835fc75560ffb9dc5e52`;
ring:
`1d647e868e676552b5e4c0539d4d527cf2abce9f1cd65ded98d197027b1b051f`.

Replay from the private directory with its existing isolated lab helpers:

```sh
python3 preserve_fresh_cache.py native196-replay
python3 capture_run.py 196-replay native-196-artifacts
python3 drive_startup.py 196-replay native-196-artifacts
```

Next: validate the actual 21-slot program, four packed-color inline vertices,
BC2 sampling and alpha blending for the observed quad. Raw constants and all
dual-issued operations must remain intact; no replacement interface or menu
image is part of this work.
