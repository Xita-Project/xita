# Native182 original screen-effect draw

The opt-in `SCREEN_RENDER=1` consumer executes the original first post-intro
quad with the [validated screen-effect shaders](halo2-screen-effect-probe.md).
Native182 consumes its BEGIN, all 80 immediate vertex words and END, then
commits the completed RGBA output to the original color attachment. It stops
at the next unsupported method before presentation. **The main menu is still
not visible; the last presented image remains black frame 135.**

The version-2 private contract adds the captured vertex sequence to the exact
program, setup and validity banks. These reference inputs are acceptance
checks; actual vertices come from consumed original commands. Unexpected
values/order, other topology, a different active subchannel or unknown methods
reject without changing guest memory or consumer state. An active consumer
owns dispatch: rejection cannot fall through to the other renderer or generic
state assignment. The existing movie contract is preserved.

BEGIN and END validate the complete color/depth/texture spans through checked
DMA objects and mappings. Texture0 must resolve to the logical depth attachment;
the destination and both input views must be disjoint in physical and returned
host address space. Attachment metadata, alignment, overflow, permissions and
mapping failure are checked. END rechecks state/resources and the four complete
vertices. Only an independent, aligned, completed GPU staging result can be
copied to the guest destination; failures preserve all guest bytes. Shader
factors are read from actual command-state colors, including alpha.

The screen backend shares the existing H2 GXM context, patcher, target and
private depth/mask surface. It copies the actual destination through the GPU,
converts the four BC2 blocks to GXM order and executes the original shader and
blend operations. It binds its own viewport, programs, samplers, culling and
depth/stencil state. Guest floating-point state is restored by the existing
geometry boundary. No separate GXM initialization or CE renderer is used.
`SCREEN_RENDER` defaults off and requires `QUAD_RENDER=1`.

All 46 host executables pass, along with the screen-consumer ASan/UBSan tests
and eight preparation tests. Tests exercise every observed vertex component,
wrong ordering/values/subchannels, unknown active methods, exact state banks,
full RGBA commit, source/destination aliases, failed attachment/mapping/render
operations, and revalidation at END. Runtime source addresses and the real
27 critical setup words were independently checked against the frozen capture.

## Actual replay

The build passes all 173 dependency-target checks and reuses unchanged guest
output/image. Shader, contract and EBOOT installation bytes match the package.
The original intro is visible again. The full original 59,670,016-byte map copy
precedes normal Start; the transition recording lasts 73.2 seconds.

The original draw completes at END source `03B7B5BC`, color `038E8000`, with
texture0 `037BC000` and texture2 `018FA680`. GXM reports 307,200 nonblack staged
pixels, first pixel `00726150`, before the consumer commits RGBA. This uses the
live `00232323` clear destination. The earlier isolated fixture used the
captured program/vertex/texture inputs with a synthetic zero destination;
it was not a full capture of this pending framebuffer.

The next stop is GET/source `03B7B63C`, method `1480`, first input `FFFFFFFF`,
remaining count 32, PUT `03B80158`. Guest submission is at EIP `003FAC58`.
The pending packet contains 32 all-ones words. These are not consumed in this
checkpoint. There is no newly presented frame or main-menu screenshot.
The emulator and capture processes have stopped; the owned `:111` X server
remains available. Source before this run is frozen privately under
`screen-consumer/source-native182/`.

| Private artifact | SHA-256 |
| --- | --- |
| ELF | `49e92712ab470223b5c77bd476a5e15624c758f3991bfb9f185b24a010a1ccc6` |
| EBOOT | `21503174955c4dd29e1e9710fbf2268cb45a94349ee3a8a19a2e41e03f559c2b` |
| Guest trace | `d994ba3491f03f3e53a0fe59b038f3ea48a453e794730b38dfe46e4f50949249` |
| Channel snapshot | `58de94b44b571e4f3fee75d7c3d248f9aaa93abd71ae80e8847ab5388deeb8d3` |
| Raw push | `7d3f83350dc8457274570b190f0e998e88a4d62125ec06d46069c08867f0404a` |
| Last presented image | `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893` |
| Intro capture | `309529716321e28c4db9b6b11982db42b07c5a3f0ed7a2783a22e1a3278cc0f0` |

Replay from the private directory with its emulator stopped:
`python3 preserve_fresh_cache.py native182-replay`, then
`python3 capture_run.py 182-replay native-182-artifacts`, then
`python3 drive_startup.py 182-replay native-182-artifacts`.
The isolated installer must copy all four `screen.*` package files as well as
the existing game/image, movie-shader and DSP files. Rebuilding also requires
`SCREEN_RENDER=1 SCREEN_SHADERS=<private version-2 prepared directory>` alongside
the previously documented host-channel/movie/audio build flags.

The diagnostic package embeds owned game code/data and must not be uploaded
as a distributable release. Generated code, assets, packages and captures stay
private. The next task is to audit and retain the bounded non-executing state
at `1480`, then continue the original submission toward a presentable menu.
