# Xbox surface description — September 6, 2026

A viewport trace found Halo requesting `0 x 128` for a 128-pixel render target.
`D3DSurface_GetDesc` wrote the 32-byte PC layout with a Pool field. Xbox's layout
has seven words: Format, Type, Usage, Size, MultiSampleType, Width and Height.
Halo therefore read the old zero multisample word as Width, Width as Height,
and received an extra write beyond its 28-byte description.

The original executable confirms this directly: `D3DSurface_GetDesc` at
`185990` calls `Get2DSurfaceDesc` at `189940`; the latter writes Size at +0C,
MultiSampleType at +10, Width at +14, and Height at +18. Its ordinary no-MSAA
value is `0x11`. The independent [Cxbx Xbox type definition](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbD3D8Types.h)
also describes the seven-word structure.

The HLE now writes exactly 28 bytes through the page-safe guest writer. The
shared helper honors its requested mip level, reports pixel-container type and
base-level usage capabilities, and uses the actual row pitch for uncompressed
byte size. Both entry points retain their original argument cleanup. No global
viewport/depth mapping change is included.

Regression checks call the real HLE and verify the write boundary, 128x128
render-target dimensions, nondefault linear pitch, color/depth usage, `0x11`
multisampling, texture/cube mip dimensions, compressed levels smaller than a
block, and every output alignment across a noncontiguous guest page boundary.
The old implementation fails the overwrite guard. Host and ASan/UBSan checks,
visibility/resident-shader tests, and the native Vita build pass.

The temporary broad viewport trace is removed; requested frame traces now
include viewport dimensions and depth range. Vita3K now records the expected
128x128 targets followed by the 640x480 guest viewport, replacing the earlier
0x128 requests. Blood Gulch unzoom/2x/10x transitions retain the correct scope
mask. Ordinary Campaign resume returns to the cryo room with its observation
windows and technician visible. Traced multiplayer/campaign frames checked
137 and 527 draws with zero data mutations; no frame-storage drops were logged.
Hardware rendering and performance remain unverified. The close-wall weapon
depth report is still separate: observed viewport depth ranges remain 0 to 1.
