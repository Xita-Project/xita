# Halo 2: first original movie draws through GXM

Native77 executes two original intro-movie quads through GXM. The original game
supplies the command vertices, decoded texture, vertex program and constants;
the [validated shader translation](halo2-quad-gxm-probe.md) supplies the native
programs. The first movie framebuffer is presented through the existing original
interval-one flip/mode-transition path. It is entirely black. The next recurring
active-scanout flip stops explicitly. **No visible movie or main menu is claimed.**

Native74 first established this path; native75 tightened callback/error handling,
and native76 additionally preserves native FPSCR around loading the private
pipeline contract. A subsequent audit found that dependency files copied from
an earlier build still named the old output directory. That left `boot.o` and
`command_snapshot.o` with the prior channel structure layout. Native74–76's
channel JSON GET/PUT/bootstrap values are invalid and superseded by native77;
their original draw/flip logs and framebuffer captures remain preserved.
Retargeting the dependencies and rebuilding both readers restores exact JSON
agreement with the terminal parser log. Native77 is the reproducible checkpoint.
The explicit audio-unavailable diagnostic still returns a genuine audio-driver
error and creates no audio device.

## Scope and preservation

`QUAD_RENDER=1` is a separate opt-in build selection requiring `HOST_CHANNEL=1`.
The default build does not link GXM or the geometry consumer. Shared Halo CE
runtime/compiler paths remain unchanged.

The geometry consumer accepts only Kelvin `BEGIN QUADS`, exactly four observed
immediate vertices, and `END`. Each vertex must contain opaque white packed v9,
the original DATA2F v3 texture coordinates, and matching DATA2F v0 position.
The accepted rectangle is `(0,0),(640,0),(640,480),(0,480)`. These input values
are copied into the host vertex stream; no replacement vertices or screen are
drawn. The xemu
[`SET_VERTEX_DATA2F_M`/`SET_VERTEX_DATA4UB` implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
documents the immediate register selection and v0-last-component emission.

Before BEGIN and again before END, the consumer compares all retained setup
words/validity bits, all 21 vertex-program slots and hardware constants 10–187
against the private reviewed contract. Only texture0's storage offset may vary;
its DMA object and complete memory span are resolved afresh. The contract is
generated only from the pinned native73 snapshot SHA-256
`a008f536b05e8e3aebcea56d148da630b0cd302f8d74797417b76d673a4bf659`,
which also verifies the program against the owned XBE. It is a trusted prepared
build input, not a user-editable pipeline configuration. A different state,
shader, sampler, geometry or ordering rejects instead of using this specialization.

The source must be one-level linear X8R8G8B8, 640×480 with pitch 2560. The target
must be linear ARGB8 with the same dimensions/pitch. DMA permissions, bounds,
whole-span mappings and tile metadata are checked before any guest write.
Physical source/target overlap and overlapping host spans both reject. Output
staging must also be aligned and separate from all guest resource spans.

GXM renders into its own memory and waits for an explicit fragment completion
notification. Only then are its RGB bytes committed to the guest framebuffer.
The pinned source alpha is one, blending is ONE/ONE_MINUS_SRC_ALPHA, and the
color mask excludes alpha. This permits copying the opaque RGB result while
preserving every existing guest alpha byte. The depth test and stencil test are
disabled; the depth surface is untouched. This does not implement arbitrary
blending or rely on Vita3K loading CPU-written destination pixels into a surface.

Rejection preserves guest bytes and accepted command/geometry state. A rejected
END remains pending; incomplete/unsupported draws are never silently discarded.
Native GXM failure returns to the strict command stop. The geometry callback
does not receive or change the interrupted x86 context, and preserves native
FPSCR around both rendering and diagnostic logging. Per-frame texture or
framebuffer dumps are not added; completion logs cover the first four draws and
then every sixtieth draw.

## Native evidence

Both original draws complete at command sources `03B44414` and `03B44AE8`,
writing guest color buffers `038E8000` and `03A14000`, respectively, from texture
`01336000`. The first buffer then passes through the original queued flip,
original retirement/callbacks, real Vita vblank waits and mode presentation.

The terminal command is method `0100`, value `74280021`, GET `03B44C00`,
PUT `03B44C44`; the submitter remains original guest `3FAC58`. It requests the
next active-display interval-one swap. The existing initialization-only flip
guard rejects it. No arbitrary port write or false swap completion is added.

Native77 private files are under `native-77-artifacts/` and `native-77-view/`:

| Artifact | SHA-256 |
| --- | --- |
| ELF, 75,180,028 bytes | `de5d1900d0d76df590c8bc642828554a823e8317f7d4065f81e3ca26fda98c88` |
| EBOOT, 91,889,554 bytes | `686e3e730e5480045c0543e8e688b2a12150aefd007eb70c4ad853156b2d394b` |
| VPK, 25,614,838 bytes | `571ae8160a308dd3c5ec3aeb8b87955635c65571e0bd35cf43dba65ca4f116b5` |
| Boot trace | `a720d5ffe7e86a687a8564e52bfb666c7b9fa49b36a0b7278aee2f2892f93aba` |
| Decoded channel | `e2af71b83f36e02f84262819f61ca1273dbdb6fe41ddfa01551571c16252eeaf` |
| Last presented frame | `d5ac913ac8137311738933954f21221544586ff6ba9cc9b044cd2afa7bb42a62` |

The last presented capture has header `{960,544,3840,2}`. Every pixel is
`FF000000`; the actual emulator window is also black. This is consistent with
the independently inspected black first intro frame, but does not establish
correct later video decoding or a visible rendered menu.

Validation: all 23 host test executables plus the existing timed case pass.
The new geometry tests cover every retained setup/validity word, every program
word and referenced constant word; incomplete/bad vertices and commands;
read-only DMA and failed/aliased/overflowing mappings; backend failure and bad
staging pointers; exact RGB writes; preservation of all alpha and surrounding
bytes. The geometry test also passes ASan/UBSan. The isolated native shader
probe's exact pixel/culling checks are documented separately.

## Private build/replay

Run `prepare_quad_shaders.py` as documented for the probe, adding `--contract`
and supplying the pinned native73 JSON. Compile its generated shaders with the
isolated shader compiler. Use a fresh, distinct build directory for this selection;
do not copy object/dependency files from another output directory:

```sh
make -C games/halo2_5849 -j4 HOST_CHANNEL=1 QUAD_RENDER=1 GUEST_OPT=-O0 \
  GENERATED=/private/host-channel/generated BUILD=/private/quad-render/build \
  IMAGE=/private/host-channel/halo2_image.bin QUAD_SHADERS=/private/quad-shaders/prepared
```

The VPK embeds owned game image/code, shader code and pipeline data. **Keep it
private; do not upload it as a distributable release.** The isolated Vita3K lab
uses synchronous shader compilation. Its launcher must install the three
`quad.*` data files as well as the original image and EBOOT.

Exact archived replay in this workspace:

```sh
python3 ../private/run_lab.py replay77 ../private/native-77-artifacts/halo2-boot.vpk
```

The next bounded task is the original active-display software flip: audit its
queue fields and device writes, retire it only after real Vita vblank, execute
the original guest callback, and present the resulting original framebuffer.
Further unsupported state remains fatal.
