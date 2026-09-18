# Original luminance/tint strip

Native194 stops at BEGIN6, `03B7E62C`. The earlier fan label was incorrect:
the pinned [NV2A definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h#L1161)
define 6 as triangle strip and 7 as fan. Actual corner order is top-left,
bottom-left, top-right, bottom-right. The packet writes v1/v2/v3/v4/v0 through
methods 1A10/1A20/1A30/1A40/1A00, 20 words per vertex. The same 640×480 linear
ARGB surface `038E8000` is both sampled input and writable color output.

The newly uploaded 12-slot vertex program copies position, performs eight DPH
operations for texture XY, copies two colors and writes fog. Texture0 uses
hardware constants 18/19; its observed transform is identity, with the original
negative-zero bit preserved. The other texture/color/fog outputs are unused by
this fragment route. All original slots remain translated. The private binding
uploads hardware constants 18..25 into the generated eight-vector array whose
D3D base is −78 (bias 96); it does not use CE constants or viewport removal.

The shader only writes texture XY. The pinned
[xemu output initialization](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/vsh.c#L192)
sets texture Z/W to 0/1. The shared Cg generator starts non-position outputs at
zero; leaving that unchanged here would make PROJECT2D divide by zero. This
preparation corrects only the four texture-output initializers in this H2
shader. Shared generator and CE behavior are unchanged. Position remains the
original window coordinates adapted to the 640×480 GXM target.

The original pixel shader computes a clamped RGB dot product with weights
(128,179,51)/255, then multiplies by final color (26,35,64)/255. Output alpha is 0;
blending, depth/stencil, fog and other sampler fetches are disabled. The global
mux selector and final-sum clamp are unused by these operands; actual mux
operations and unproduced inputs remain rejected.

The pinned [xemu texture binding](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/texture.c#L378)
copies a rendered surface into a separate texture before the new draw. This
bounds the source-snapshot model used here. Admission requires exactly identical,
complete physical and host input/output views. Partial overlaps and different
physical ranges mapped to the same host span are rejected. The GXM adapter
copies the complete original image into independent sampled storage before
rendering; guest memory is untouched until the private output completes and
END commits it. This is a supported model for the observed draw, not proof of
arbitrary NV2A feedback behavior.

Preparation pins the owned XBE, native194 channel/ring/image and 12-slot program.
Its private contract includes all setup/program data, the eight raw constants
and four 128-byte expanded vertices. Only unused color/fog input components are
zero-filled; no live input is substituted. SceShaccCg compiles 868-byte vertex
and 528-byte fragment programs with zero failures, one scalar-varying warning
and three unused-variable warnings.

The first GPU probe stopped before drawing because its harness requested an
incorrectly renamed `IN.lumaweight` parameter. That failed artifact/log remains
in `luma-pass/probe01`. Correcting the harness to the compiled
`IN.blendweight` name allows all ten fixtures to complete. No shader or original
instruction was bypassed. The corrected probe covers 3,072,000 pixels using the
actual captured image, gradients, block patterns, opposite winding, translated
and cross-axis DPH transforms, changed gain, zero gain, saturation and unused
source-alpha variation. Every channel is within one UNORM8 level of an
independent DPH/bilinear/dot/clamp/tint reference; alpha is exactly zero.
Opposite winding and unused source-alpha variations are byte-identical. Zero
gain and saturation fixtures are exact. These are measured GXM/Vita3K bounds.
The corrected utility returns to idle after successful completion.

All 53 host executables, consumer/dispatcher ASan/UBSan checks and 32 focused
Python tests pass. Tests enforce raw constant/vertex bits, complete in-place
ownership, failed-render isolation and no guest mutation before commit. The
eight-consumer dispatch oracle preserves exclusive active ownership and FPSCR.
`LUMA_RENDER=1` requires the prior blend path and defaults off.

Owned inputs, generated code/shaders/contracts, diagnostic packages and captures
stay private and outside Git. Packages embed owned game code/image and must not
be uploaded as distributable releases. Only the isolated H2 `:111` lab is used;
CE, shared Vita3K and physical Vita are unchanged. Native194 remains the last
completed game run pending this integration: intro verified, displayed frame135
black, original main menu absent.

The compiler finishes both shaders and then hits a separate utility-exit
SIGSEGV at `0x2004C0`; its successful compiled outputs are preserved. The first
failed harness also enters the emulator's exit fault path; the corrected probe
completes and returns to idle. Final probe comparison SHA256:
`6f3ce93543d0de06d64aa7e4d4b66cb62a638afd31c5d0a3cd3a055ef7b57bc0`;
ELF `46b0956035c72b3b564214a74a9c03e5feee45dc724a4fa3b50f5143f0a29372`,
EBOOT `012b0bf5615093f80681e2efd17ee18d7f75257c5a26bcdfc26e1d2172b76968`.
Private probe inputs/results are `luma-pass/prepared`, `compile01`, preserved
failed `probe01`, successful `probe02`; the complete 9288-byte contract SHA256 is
`4093cb9fd8e8135f257886d5d7235e91a3e717db845c6c93ba811e5d226123fa`.
With LUMA_RENDER disabled, both renderer and dispatcher objects are byte-identical
to native194. Native integration uses 179 independently checked dependency targets.

Native195 completes the original strip END at `03B7E7C4`. Its real in-place
source snapshot produces 307,200 nonblack pixels, first `00101527`, then commits
them to `038E8000`. The next strict stop is method `1768`, value `00000040`,
GET `03B7F010`, PUT `03B80158`, EIP `003FAC58`: a packed-color vertex format
for the following inline quads. No quad was accepted through that stop.

The Microsoft Game Studios intro and terminal black window were visually
checked again. The last presented game frame remains 135, with zero nonblack
RGB pixels and SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
**The original main menu has not appeared.** Normal Start followed the full
original 59,670,016-byte map copy; the replay completed and its owned emulator
process was stopped after archiving. Next is the actual packed vertex-format
state and subsequent inline draw/texture snapshot, retaining strict draw rejection.

Native ELF SHA256:
`a1ffc1b29a8eeb25437eff5aca81e993b6b3ef810adea7188b68674d8b538006`;
EBOOT `d91ced09855d47b8f92c7de6c97d9fdcfda01076d82d4fd37620224f963c4dd0`;
trace `d56e559b0afa7b0bea34f8b0f4078e346145de4cd3c4117ce3d8e6569f8272b9`;
channel `a321b6d38b3d78c8df01cb613066af2f179c8cd188b88cb421e846e4280b1433`;
push `2e48b0d9cc489f5bf9a8c4793d6ac18026c63a1cf302a9bd7801cab482b2fc7e`.
Private build/tests: `luma-consumer/`; frozen evidence: `native-195-artifacts/`
and `native-195-view/`. Frozen normal replay from the private handoff directory:

```sh
python3 preserve_fresh_cache.py native195-replay
python3 capture_run.py 195-replay native-195-artifacts
python3 drive_startup.py 195-replay native-195-artifacts
```
