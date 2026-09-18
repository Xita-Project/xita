# Polygon-stipple state after the first screen effect

Native182 completes the original post-intro screen-effect quad, then stops at
source `03B7B63C`: an incrementing packet at `1480` contains 32 all-ones words.
The pinned [Cxbx Xbox method table](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/XbConvert.h)
and independent [envytools NV2x register definition](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/rnndb/graph/nv20_3d.xml)
identify `147C` as Boolean polygon-stipple enable and `1480..14FC` as its
32-word pattern. The pinned xemu header does not define these methods; no
behavior is inferred from an unimplemented xemu handler.

The command state now retains each complete pattern word and its validity bit,
while the existing Boolean enable path still accepts only 0 or 1. These assignments do not fetch resources,
map guest memory, emit geometry, write pixels or execute a stipple mask. The
existing movie and screen-effect contracts compare the entire setup/validity
banks, so newly supplied stipple state cannot silently pass an old draw
contract. Active draws continue to reject these assignments. Rasterization with
new stipple state needs a separately validated consumer policy.

Host tests cover every row, both enable values, mixed bit patterns, repeated
writes, exact bank/validity changes, neighboring/misaligned methods, wrong
object bindings and rejected enable values. State and guest/clear memory remain
unchanged except for the selected state word and bit; no mapping or resource
callbacks occur. All 46 host executables and command-state ASan/UBSan pass.
Private source/reference evidence is under `polygon-stipple/`.

Native183 consumes the full pattern packet and reaches the next original quad
BEGIN at `03B7BA18` (`17FC=7`, PUT `03B80158`). All 32 pattern words are
`FFFFFFFF`, their validity bits are set, and enable `147C` is zero. The first
screen effect still commits 307,200 nonblack pixels before this stop; frame135
remains the last presented frame and is black. The original Microsoft Game
Studios intro is visible. No original main menu is visible.

Private native183 ELF SHA256 is
`4ca267aed21a558a8390ad814fd9c374dc1336b8fc223fafd2ceffeeab64c9e8`;
EBOOT `793e1f2aa201d932bc83427fa80d17b464184528dc2a7bdd26efa709d5b64dcf`.
Channel snapshot SHA256 is
`6b4d7cbcdbc07b6cea821c3fb3b5b81b8857ce1790ac55b49325fbfbb0d97634`.
Native183 had a redundant early Boolean-enable branch; the final source keeps
only the pre-existing enable case and adds the pattern range. The native184
resource-capture replay validates that final source and records its own build
identity. Native183's source diff and unmodified binaries remain archived.
