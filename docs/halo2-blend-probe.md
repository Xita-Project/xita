# Original single-texture screen blend

Native193 completes the four averaging passes and reaches original BEGIN7 at
`03BB6400`. One 160×120 linear ARGB image at `02B1B000`, pitch640, is sampled
bilinearly with clamp addressing. The target is `038E8000`, 640×480/pitch2560.
Its original seven-slot vertex program remains unchanged. Each vertex writes
v5 color, v1 projected texture coordinates, then position: 12 words in that order.
The unused vertex outputs do not feed the captured fragment shader.

The original one-stage combiner computes `S.rgb = clamp(2 * T.rgb * C0.rgb)`;
final alpha is zero. `C0.rgb` is 96/255 in the captured state. RGB blending is
`S * (1-D) + D`; alpha blending preserves the actual destination alpha.
The pinned [xemu register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h#L959)
identify source factor 0307, destination factor 1 and equation 8006. Its
[combiner generation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/psh.c#L514)
provides the bounded output scale/clamp reference. No depth, stencil, alpha-test,
fog or other sampler fetch is enabled by this route.

Preparation pins the owned XBE and native193 channel, complete ring and image.
The image contains exactly 76,800 zero bytes, the real preceding filter output;
it is a probe input, never a substitute for later live guest data. The private
version1 contract is 9016 bytes, containing complete state/program/vertices.
SceShaccCg compiles 596-byte vertex, 496-byte fragment and 344-byte copy programs,
with zero failures and two unused-variable warnings. The copy shader initializes
GPU staging from the actual destination and is separate from the original draw.

Ten fixtures cover 3,072,000 output pixels; ten separate unblended fragment
captures cover another 3,072,000. Tests cover the captured zero source, gradients,
blocks, opposite winding, fractional UVs, varied gain, saturation, zero gain,
unused source-alpha changes and a copy-only destination check. The initial
strict one-level end-to-end comparison fails on 13 pixels in the strong-gradient
fixture: maximum RGB error 2, while its unblended source differs by at most 1.
That failed report is preserved. Blending from the independently measured
fragment result matches every output byte exactly, identifying the accumulated
source/blend rounding. The final checker requires source error≤1, exact measured
source blending, full error≤1 except the documented gradient≤2, and exact
alpha preservation. Zero-source/zero-gain/copy-only destinations are byte-exact;
opposite winding and unused source-alpha changes are also byte-identical.
These are measured GXM/Vita3K bounds, not NV2A bit-exactness claims.

Both probes finish all captures and then hit the known utility-exit Vita3K
SIGSEGV at `0x480400008`; the completed draw/readback evidence precedes that
separate fault. Shared Vita3K was not modified. Five preparation/math tests and
21 existing filter/composition tests pass. Private evidence: `blend-pass/compile01`,
`probe01`, `probe02-source`; the initial failure is `comparison-strict1.json`.
Final comparison SHA256:
`0c4cafea82cc2bbe4a8de9e6065ea3ad323a2518893fad4230ad6fccd6165395`.
Normal ELF `d60e37e953210e759f22cc5a2a8de6d19df42bdf74ee16e555f26ddedae3bcc7`,
EBOOT `1df3cb89c17336dce30aa6631be9eda2261360e852c68c7ad09a9bbfe882460e`;
source ELF `e9d304b582484510f707a20d5fd185c000e8bf90134ffdf3ec9a11efe93c6082`,
EBOOT `bda13c73b78a2a45d9fd5e521a2a4f435e12b6616262105058b548894c71e10c`.

`BLEND_RENDER=1` requires the existing blur path and defaults off. The consumer
checks the exact original 12-word ordering, complete pipeline/program reference,
framebuffer dimensions, sampler state, DMA permissions and complete physical/host
spans. Input and destination cannot overlap. Retained inactive textures and
unbound depth cannot trigger reads. END revalidates all inputs, renders into
independent staging, and commits RGBA only after completion. The staging copy
uses separate full-destination UVs; a completed scene precedes restoration of
the actual original vertex bits and original fragment draw. This changes no
presentation call or guest control flow.

All 52 host executables, new consumer/dispatcher ASan/UBSan checks and 26 focused
Python tests pass. The seven-consumer dispatch oracle checks exclusive success
and rejection ownership plus floating-point controls. With the new option off,
renderer and dispatcher objects are byte-identical to native193.

The diagnostic packages embed owned game code/image/shaders and must not be
uploaded as distributable releases. Owned inputs, generated code and captures
remain private and outside Git. CE, shared Vita3K and physical Vita are unchanged.
The original main menu has not yet appeared; native replay follows the GPU proof.

Native194 completes the original blend END at `03B7E1E0`, writing all 307,200
nonblack pixels into `038E8000`; first pixel is `00786959`. It then strictly
rejects the next original BEGIN6 at GET `03B7E62C`, PUT `03B80158`,
EIP `003FAC58`. That triangle-fan route has a newly uploaded 12-slot vertex
program and remains unsupported. The original game has not presented this
composition result yet. The Microsoft intro and terminal black game window
were visually checked; raw last-presented frame135 has zero nonblack RGB
pixels, SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
**The original main menu has not appeared.** Normal Start followed the complete
original 59,670,016-byte map copy. The driver completed without interruption;
the owned game process was stopped after archiving.

All 178 native dependencies were checked. ELF SHA256:
`5949612c0aa3b89e4b3639b33572e3af3f8515f22afc0ec4ef651c4dd9dd3638`;
EBOOT `5b0f4f9e79cfdd8aae8e78211eeaa8d0461ef8d4ae958a86e058f77837ac735d`;
trace `f6f7c1126f344e4ff1e71b824105890071fada7b354a3b58278a01f53205943e`;
channel `712dfd93152ba17f11278bcb363c865b645de562e8edd0b6802c4262ff02662c`;
push `40e807a80d890ca403ccdab003306480b9a1d164da11819ae9dcde00aa4a7d49`.
Private build/tests: `blend-consumer/`; exact captures: `native-194-artifacts/`
and `native-194-view/`. From the private handoff directory, frozen replay is:

```sh
python3 preserve_fresh_cache.py native194-replay
python3 capture_run.py 194-replay native-194-artifacts
python3 drive_startup.py 194-replay native-194-artifacts
```

Next is the captured triangle-fan vertex program, original inputs, texture and
combiner/blend state; no replacement image or premature presentation is added.
