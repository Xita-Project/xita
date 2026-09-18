# Halo 2 original movie combiner state

Native70 retains the original pending combiner state and reaches texture offset
method `1B00`. The narrow command whitelist adds four eight-stage alpha/color
input/output banks, the final combiner words/factors/control, shader clip mode,
and the low 12-bit dot-mapping field. Each assignment stores its original word
and validity. It does not evaluate a shader or approve a draw encoding.

The pinned [xemu method implementations](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
store these combiner words in registers, with a separate masked low shader-control
field for dot mapping. Its [method table](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/methods.h.inc)
defines the eight-word banks and two final factors. Unsupported neighboring
methods, unaligned/class-mismatched writes, invalid dot-mapping high bits, BEGIN,
END and vertex emission still reject without modifying state or guest memory.

Synthetic tests compare complete command/clear state and guest memory for all
39 new words, repeated writes and representative raw values. All 21 host
executables plus the timed case pass with the H2 context option. Command-state
tests also pass ASan/UBSan.

Actual Vita3K native70 passes the alpha/color configuration and original 21-slot
vertex-program upload. Independent parsing verifies 55 exact setup words and
all 84 uploaded program words in the stop snapshot. GET is `03B442B8`, PUT is
`03B44520`; the next strict stop is `1B00=01336000` through guest `3FAC58`.
The remaining submitted packets describe texture0 with linear X8R8G8B8 format,
pitch 2560, rectangle 640x480 and a four-vertex fullscreen quad. These packets
have not executed. Texture DMA resolution, sampling, shader/draw execution and
presentation still require implementation. The actual displayed output is
black, with no decoded movie frame or main menu.

| Private native70 artifact | SHA-256 |
| --- | --- |
| ELF | `d7572db328603412b78e632c593e38026b3c90ecea9b5806155037200ee12e8b` |
| EBOOT | `7044f2e2fdb1271be5fa2c5b0b47fa20b499006827a2b147958347e4df3a27d8` |
| VPK | `2d00e47f9f9c24f085d3ba083606a27b88aa3c7caa79096adf78ebfda7fe6c21` |
| Boot trace | `dd254089ccb32ac959d19d3d430c54f25a2aac7c655a6765acc260509695178d` |
| Channel snapshot | `1138d958ca59e7267e63f22c8b20833bf233be45549fb29ec8ab12b39762eda1` |
| Last presented buffer | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Replay: `python3 ../private/run_lab.py replay70 ../private/native-70-artifacts/halo2-boot.vpk`.
Frozen generated source remains `private/bink-pixel-callbacks/generated`.
Packages embed owned game code/image and must not be distributed or uploaded as
releases. All game-derived data, generated source and captures remain private
and outside Git; the unavailable-audio diagnostic remains explicit.
