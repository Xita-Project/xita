# Generated input at the next composition pass

Native185 completes the original 320×240 BC1 pass, then reaches another
640×480 BEGIN7 at `03B7BF34`. Native186 adds only stop-time read-only linear
texture capture for units1..3, using the already tested four-unit resolver.
Unit0 keeps its existing filename and header. Compressed formats still use
their separate readers; unsupported inputs remain uncaptured and no draw is
accepted by this diagnostic change.

Native186 repeats the original Microsoft intro and both original screen
passes. The second pass stages 76,485 nonblack pixels, then commits to
`02B48000` at END `03B7BBB0`. The next pass's texture3 resolves that same
allocation as a 320×240 linear ARGB image, pitch1280, bytes307200. Its first
word `00001A2A` and nonblack count both match the just-completed GPU output.
The complete private texture3 capture SHA256 is
`66a31947712af4ad13173d1b37f8570dd08c439b6ce532b1b92021d44e1c09a7`.

The new pass selects PROJECT2D, DOTPRODUCT, DOT_ST, PROJECT2D, with unsigned
HILO mapping4 for its dot stages. Unit0 is the earlier 640×480 linear ARGB
input, unit2 the 8×8 BC2 lookup, and unit3 this generated 320×240 image. Unit1
still has an enabled BC1 descriptor, but the DOTPRODUCT stage does not fetch
that resource: it dots coordinates against the earlier texture result, as
shown in the pinned [xemu texture-shader implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/psh.c).
The observed combiner does not read its unused color result. Six combiner
stages overwrite the v0/v1 color components before their later reads; a future
wrapper must check that component flow rather than invent initial vertex
colors. The original immediate coordinates are preserved in the push capture.

The decoded channel is byte-identical to native185. The strict stop remains
BEGIN7 at `03B7BF34`, PUT `03B80158`, EIP `003FAC58`. The new image has not
been presented: last scanout135 remains black with SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
The original main menu is still absent. Next is private shader/pixel validation
of this composition route using the matching inputs from native186, followed
by a checked original command consumer.

Native186 verifies 174 dependency targets. Private ELF SHA256:
`154f5eb31e9391933dbc91de7da3eeebc1456017c93492e3be45b81e08851dbf`;
EBOOT `6e24c2b39120423a36724aa2cf096ba6d56072c29b6265411dbb4e51ff7fb738`;
trace `fe5f7a2a098214eb60e899de5ba060140e28763dae6f22c010a7149f2a96bf59`;
channel `01c41cab9b4892c0831c5ff35f8682a3d991ece0ebba78f049cabf01f3196fef`;
push `6ee03a2355bda9dc878ca2412efb320dc849e945e90f4bcabb86c0f3e9f69498`.
Artifacts and visible-intro capture are in private `native-186-artifacts/` and
`native-186-view/`; build/source records are in `composition-capture/`. The
owned emulator was stopped after capture. Assets, generated code, diagnostic
packages and traces remain private and outside Git; no CE or hardware change.
