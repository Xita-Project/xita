# Resident vertex shader selection — September 6, 2026

The flashlight disappearance has a confirmed CPU-side cause. Halo preloads three
model shaders, then switches between them with `SelectVertexShader(0, address)`.
The old HLE ignored the resident address and assigned the zero handle to the
current shader. The renderer consequently returned before submitting those
models to GXM. A paired cryo-room trace records 20 dropped draws immediately
after this call, including the 1,022-index pod body and the NPC's model parts.
The glass uses a later shader and remains visible, making the pod look hollow.

Xbox 3925's original `SelectVertexShader` at `0x183BC0` preserves declaration
state when the handle is zero and always updates the program start address.
This agrees with the independently implemented
[Cxbx shader selection](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbVertexShader.cpp).

The correction tracks declaration and resident program separately. Loads record
programs in the 136-instruction shader memory; a load overlapping another
program invalidates its compiled mapping. Repeated loads of the same immutable
program take a fast path. `SetVertexShader` also uploads at address zero.
The GXM bridge uses the selected program's hash when choosing compiled code.
Halo's resident VS 10, 9 and 27 all share declaration `0x1E1400`, so the existing
compiled model layouts apply to these switches.

`GetVertexShaderSize` also returned the wrong units. The original creation code
at `0x18399C–0x1839A8` reads the 16-bit instruction count from the function header;
`0x183A72–0x183A76` stores it at object offset `0x10`, and the getter at `0x183C30`
returns that field. The old HLE returned the byte length including the four-byte
header. Halo therefore requested load addresses 0, 1060 and 1784 instead of
0, 66 and 111. The three programs contain 66, 45 and 24 instructions.

Validation:

- `make -C recomp/host test-vertex-shaders` exercises the actual HLE: 10,000
  three-program switch sequences, preserved declaration state, independent
  nonzero declaration selection, instruction counts, Set/Load behavior,
  partial overlaps, shorter replacements and address bounds. Sanitizers pass.
- Restoring the old size result fails the instruction-count assertion.
- Existing runtime, immediate drawing, file I/O, save reservation, callback,
  profiler and read-retry tests pass after the state change.
- `make RECOMP=1 -j6` produces the native Vita build.

- The native Vita3K build keeps pod bodies visible through repeated flashlight
  toggles at multiple cryo-room angles. A trace at frame 9377 executes
  `SelectVertexShader(0, 0)` after VS 9: it retains the declaration and selects
  VS 10, submitting the following 1,343-index model draw to GXM. The original
  implementation drops every draw after the same null-handle API operation.

Hardware flashlight, NPC coverage and plasma-charge/Ghost retesting remain
pending; this does not claim to fix missing projectile effects, black glass,
or all forms of geometry pop-in.
