# Halo 2 read-only movie texture view

Native73 still stops at the original quad BEGIN (`17FC=7`, GET `03B4438C`,
PUT `03B44520`). A stop-only diagnostic now resolves texture0 through the
selected existing DMA object and captures its actual guest bytes. It does not
advance the command stream, change resources, draw or present anything.

The reusable read-only view supports only the observed one-level, two-dimensional
linear X8R8G8B8 format with border-color addressing. It requires all five resource
fields to be valid and the texture enabled; selects DMA A/B from the original
method encoding; validates the DMA class/read permission, inclusive limit and
physical extent; then calls the existing whole-span mapper. The mapper retains
its guest-page and tile checks. The returned host span is checked for pointer
wrap. Output remains unchanged on rejection. Dimensions above4096, short or
partially encoded pitches, unsupported formats/cubes/mips/selectors, absent DMA,
unmapped spans and overflowing ranges reject. These are explicit view limits,
not a claim of general texture or sampler support. Sampling modes and shader
state require separate validation before future draw execution.

The pinned [xemu texture address resolver](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/texture.c#L81)
uses the selected DMA base plus the texture offset; its format table identifies
linear X8R8G8B8 as four bytes per pixel. This implementation reuses the project's
stricter bounded DMA loader rather than wrapping addresses into RAM.

All 22 host executables plus the timed case pass. The view tests cover four units,
both DMA selectors, read-only and read/write classes, full padded spans, missing
fields, invalid format/dimensions/pitch, permissions, limits, failed mapping,
pointer wrap and preservation of inputs/output/guest bytes. They pass ASan/UBSan.
The native capture validates `01336000`, 640x480, pitch2560, 1,228,800 bytes;
`texture0-at-stop.bin` contains an eight-u32 header (version,physical,width,height,
pitch,bytes,original-format,reserved), followed by unchanged guest bytes.
No capture is added to the normal frame loop.

The actual source texture contains 307,200 zero pixels. Independent FFmpeg decode
of the owned `intro_60.bik` produces an entirely black first RGB frame too
(SHA256 `0b150fd32588b1daca5569992ebe559c0102c837306b1af4c44d35128ec58366`).
This is consistent with the first movie frame; it does not prove correctness of
later decoding or demonstrate a working renderer. The displayed buffer remains
black and identical to native72. No movie image or main menu is visible.

| Private native73 artifact | SHA-256 |
| --- | --- |
| ELF | `f3ee23729e6f029d0ee6784a565548268e8ac5ee3d82a48ceff837aeebb21add` |
| EBOOT | `dcc4693f0d9dcf6f1ba18a27bc583f6dda5b0fdf16f7a52fed85a209495facff` |
| VPK | `49cb6707f4b4809aa5e2b2d52e6ecea0ff886fb0ccbf06cf80ec1f54c8cbabfb` |
| Boot trace | `4fa577abb0aa5ac0e50399da3f066efea7c1a9fc0c497b48f2a73da56873115b` |
| Channel snapshot | `a008f536b05e8e3aebcea56d148da630b0cd302f8d74797417b76d673a4bf659` |
| Read-only texture | `eb8d3660e7c39fd2d9d665c21b006c4162f1b0d223501d555cd391ed1a33bf47` |
| Last presented buffer | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Replay: `python3 ../private/run_lab.py replay73 ../private/native-73-artifacts/halo2-boot.vpk`.
Frozen generated source remains `private/bink-pixel-callbacks/generated`.
The next task is a tested backend for the original quad's shader, combiner and
sampling state, followed by the original timed presentation path. Packages embed
owned game code/image and must not be distributed or uploaded as releases.
All textures, decoded reference frames, private assets, generated source and
captures remain outside Git. The unavailable-audio diagnostic remains explicit.
