# Halo 2 face culling

Both main-menu rendering paths ignored captured culling state. GXM set NONE
when opening a scene; the software rasterizer accepted either triangle winding.
They now honor NV097 methods 0x308 (enable), 0x39C (front/back/both), and 0x3A0
(front winding). This removes unwanted back-face fragments without changing
geometry, texture data, or guest game logic.

The generated vertex shader converts Xbox window coordinates to clip space.
Its Y reversal and the GXM viewport's negative Y scale cancel, preserving
screen-space winding. GXM receives the winding to discard; front and back
selection is resolved against the game's front-face setting. Both-face culling
uses region CLIP_ALL, retaining submission while suppressing fragments, depth
writes, and visibility increments. Every draw resets both culling and clipping,
so a later two-sided UI draw cannot inherit that suppression.

The software fallback rejects culled triangles before rasterization. Positive
signed area is clockwise in its top-left-origin window coordinates.

## Validation

`tools/test_halo2_culling.py` exercises the production GXM state setup with all
enable/face/winding combinations, verifies the window-to-clip-to-window mapping,
and checks a following two-sided UI draw. These GXM calls are host doubles.
The expanded `menu_raster_test.c` renders both triangle windings and verifies
color, depth, stencil preservation, and visibility counts for each combination.
Both tests pass normally and with ASan/UBSan.

The Vita build at revision `d212da0` succeeded; only `eboot.bin` differs from
the preceding color-mask package. An isolated Vita3K run reached the real main
menu through normal controller input, with no guest guard stop before the
operator ended the run. Background artifacts remain visible. Different camera
phases prevent a pixel-for-pixel visual comparison with the earlier run.

Physical hardware validation remains necessary. This does not establish an FPS
improvement, fix the complete menu background, or diagnose the hardware black
startup. The physical Vita was unreachable; no update was deployed. Private
receipts and screenshots are under `halo2-culling/visual` in the September 18
unified-games experiment directory.

## References

The enum semantics were checked against local VitaSDK `psp2/gxm.h` and the
[Vita3K GXM state implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/gl/sync_state.cpp).
The Xbox front-face/Y-coordinate convention was cross-checked with
[xemu's NV2A draw state](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/gl/draw.c).
No source code from those implementations was copied.
