# Halo 2: initialize the private GXM staging mask

The movie staging renderer supplied a color surface and a null depth surface
to `sceGxmBeginScene`. Native148 rendered a nonblack movie input black;
native149 replayed the same ELF/EBOOT/configuration and rendered it correctly.
Native150 again rendered black. Its generated vertex and fragment SPIR-V
files were byte-identical to native149, as was the first movie input.

The pinned Vita3K implementation provides a specific explanation to test:

* With a null depth surface, `handle_set_context` clears only the stored depth
  and stencil pointers; it does not set the background-mask bit. See
  [scene.cpp at 496939b6](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/scene.cpp).
* OpenGL context setup calls `sync_mask`, which clears the mask texture from
  that stored bit. See [gl/sync_state.cpp](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/gl/sync_state.cpp).
* The generated fragment shader reads the mask image and discards masked
  pixels. This mechanism exists in both GLSL and SPIR-V; changing shader
  language alone does not initialize the missing state.
* The ordinary `sceGxmDepthStencilSurfaceInit` API initializes the background
  mask to one. See [SceGxm.cpp](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/modules/SceGxm/SceGxm.cpp).

The renderer now allocates one private 640×480 S8D24 tiled staging surface and
initializes it through that API. Every staging scene supplies this surface.
It is not the guest depth attachment and is never copied into guest memory.
The supported movie contract still disables depth/stencil writes and retains
the existing shader, constants, geometry, texture checks and RGB-only copy.
Allocation or API failure remains fatal to the supported draw.

An independent native probe first tested this change with four rounds of
the existing quadrant, texture-hash and reversed-winding pixel fixtures.
Three SPIR-V replays and three corrected GLSL replays pass all 72 checks with zero pixel
mismatches. These are synthetic rendering checks, not Halo 2 frames. Both
shader paths generated their corresponding shader form with preload disabled.
Private evidence is `quad-shaders/mask-control/validation.json` and the six
`mask-control-spv-*` and `mask-control-glsl-synced-*` directories. Earlier
GLSL-labelled controls actually generated SPIR-V because the secondary XDG
configuration was not synchronized. They are retained as invalid mode
comparisons, and are excluded from the GLSL coverage claim. The corrected
runs assert the shader form in the emulator log. The owned lab configuration was then
restored to SPIR-V. Neither the shared emulator binary nor CE was modified.

The tracked probe has the same explicit surface and four-round coverage;
its separately built package passes another 12 exact checks in each shader
mode, with shader form asserted from the logs (`mask-tracked-{glsl,spv}`).
The native152 game build changes only `quad_gxm.o` from native151; guest code,
owned image, audio objects and shaders are unchanged. All 171 dependency
targets are verified. Native152 visibly renders the original Microsoft Game
Studios intro; first movie input and output are byte-identical, SHA-256
`fa48be270c689ce661bf3bbc89ca47c6a531420d0a5750a1d88aaf5a0d073491`.
Normal Start reaches the same strict `216A54` stop as native151, with the same
channel snapshot. Its direct intro capture has SHA-256
`94fbbf082ea7c99348d8b72bd7de6bd82a54712a87dc2e8cd2a4da7b68ffb9ae`.
The final presented frame is black; no main menu is visible.

Native152 ELF SHA-256 is
`baa69260df1bcb7a9e010cd495037e758d7e357807cd75a18a4fe7d07991ec8e`,
EBOOT `f5ca978635b00b04388b358c117293436820243baa4aaab3093cfa2d1375195a`,
trace `0eb934d5b7dd02968738bc73fcdce3fffe221ff4c303abadd59eb9ceec28e6e8`.
All 44 host executables pass. Owned shaders, generated code, game data and packages
remain private; these diagnostic packages must not be distributed.
