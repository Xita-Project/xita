# Halo 2 depth-clear notification

The Kelvin clear consumer notified the GPU depth backend for either depth or
stencil clear flags, before validating the surface, rectangle, DMA mappings, or
attachment overlap. The callback schedules a full depth reset. Consequently a
stencil-only command could erase accumulated GPU depth, even though the CPU
clear correctly preserved the Z24 bits. Rejected commands could also queue a
depth reset despite returning failure.

The callback now runs only for the depth flag, after all existing clear
validation has succeeded and before the accepted clear writes its pixels.
Stencil-only, color-only, no-op, and rejected clears leave GPU depth untouched.

The existing parser-to-pixels clear test now observes this callback. It fails
against the old consumer on a stencil/partial-color command, and passes against
the correction both normally and with ASan/UBSan. It also checks callback
preservation on failed DMA mappings, host and physical aliases, and other
rejections. These tests do not emulate GXM or establish hardware performance.

## Remaining depth work

This fixes notification timing and selection, not all depth semantics. The GPU
backend still has one shared depth surface, and its clear callback still has no
attachment identity or rectangle. A partial depth clear is therefore currently
treated as a whole-surface reset, and switching guest zeta attachments is not
fully represented. Stencil testing is also disabled in the GPU path. These
require their own implementation and validation; the overall rendering goal
remains open.

The menu shader generator and software rasterizer were also inspected: both
undo the Xbox Z24 viewport range, while the GXM generator applies an additional
0.9999 factor for far-plane clipping. No shader conversion change was made in
this patch. The hardware black startup still requires its saved boot log.
