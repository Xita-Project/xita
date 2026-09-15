# Halo 2 first post-intro screen-effect inputs

Native180 reaches an unconsumed quad begin (`17FC = 7`, GET/source `03B7B424`,
PUT `03B80158`). Its seven-slot vertex program passes through position and
attributes; its texture stages and four combiners differ from the supported
movie pipeline. This is a new rendering boundary, not a rendered menu.

The read-only texture resolver now also admits one-level linear A8R8G8B8,
preserving every storage byte. A second bounded resolver admits only the
observed 8×8, one-level, 2D DXT23 encoding. It resolves four row-ordered 4×4
blocks of 16 bytes through the existing checked DMA object and complete-span
mapper. Both DMA selectors are checked; missing state, unsupported layouts,
wrong permissions, address/host overflow and unmapped spans reject without
changing the result or guest memory. There is no decompression or draw in
these helpers. The movie consumer continues to require X8R8G8B8 explicitly.

Layout references are the pinned xemu
[format definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h),
[block-size calculation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/texture.c)
and [compressed texture upload](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/texture.c).

Stop-only private captures have eight little-endian header words: version,
physical address, width, height, pitch, payload bytes, original format and
layout. Layout zero is a linear byte image; layout one is row-ordered DXT23
blocks. Captures occur after rejection and cannot accept a pending command.

All 44 host executables pass, as do the texture-resolver and movie-consumer
ASan/UBSan tests. Tests include exact input/output preservation, every format
bit change, all units/selectors, invalid DMA/mapping spans and rejection of
ARGB by the movie renderer even if a supplied reference requests it.

## Native181

The native build reused the unchanged scalar-comparison guest output and
passed all 171 dependency-target checks. The replay consumes the full original
59,670,016-byte map copy and normal Start, then rejects the same quad begin.
The decoded channel is byte-identical to Native180. Both resource captures
report completion: texture0 is the 640×480, pitch-2560 depth attachment at
`037BC000`, viewed as ARGB, with every word `FFFFFF00`; texture2 is the original
64-byte DXT23 image at `018FA680`. These are logical host storage views, not
validation of NV2A hardware compression tags or texture shader execution.

The original Microsoft Game Studios intro is visible in
`native-181-view/early-movie-middle.png`; the transition recording lasts
80.714 seconds. The final displayed image remains black frame 135. **The
original main menu is not visible.** The emulator and capture processes have
stopped. The next task is an explicitly validated screen-effect consumer,
including HILO texture mapping, compressed sampling, original vertex inputs,
combiner order and destination blending. None is implied by these captures.

| Private artifact | SHA-256 |
| --- | --- |
| ELF | `4c1756479354f0d938bef0395dffee3c340ca57d0b1bdf810eebee01e30b21a3` |
| EBOOT | `3712fbee5e73b6a299b9227c192bc4ab586df29876153fe693cb55658bed8f5e` |
| Guest trace | `6959aa94a5d1debdcb52ff179b5d178e30e0f993f92f8b3a698f17edb2e7f4a2` |
| Decoded channel | `8ef2cb740df793e5de1e8c02345f82c7c76e21913c4fbbfa3d5c8ab856406d6f` |
| Linear texture capture | `713492e8e1a38a716643aec5ffb482dbb2ba0f1d84fd5b62ca75a6d10b9da0df` |
| DXT23 capture | `28b94c7f7595c9a970886a071d9a20ca9f87c6bb24e82f4ecf03a226ca0afab2` |
| Last frame | `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893` |

Replay from the private directory with its emulator stopped:
`python3 preserve_fresh_cache.py native181-replay`, then
`python3 capture_run.py 181-replay native-181-artifacts`, then
`python3 drive_startup.py 181-replay native-181-artifacts`.
The diagnostic package embeds owned game image/code and must not be uploaded
as a distributable release. Game data, captures and generated files stay private.
