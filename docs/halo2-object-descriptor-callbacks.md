# Halo 2 object descriptor callbacks

Native156 reaches `112070` through child-descriptor field`6Ch` at `1090A5`,
return `1090A7`. The original caller resolves an object handle, reads its type
byte, selects the corresponding catalog parent, and walks the parent's direct
child array at`84h` until null. It skips null callbacks and passes the original
object handle plus three arguments. Three of the17 owned child descriptors
have nonnull callbacks in this field; `112070` belongs to child`4679B0`.

The adjacent object walks are grouped with that observed boundary:

| Original walk | Child field | Callback arguments |
| --- | --- | --- |
| `108FD0` | `64h` | One; original caller accumulates true AL results |
| `109290` | `68h` | Four |
| `109050` | `6Ch` | Four |
| `1090D0` | `70h` | Two |
| `109140` | `74h` | Three |

Each complete walk through its return is fingerprinted. These original
callers retain their register/stack conventions, null handling, iteration
order and return handling. The new discovery rule only supplies their
original callback bodies to the compiler; it changes no callback result or
runtime behavior.

The existing checked catalog/child extraction is shared by the earlier
initialization fields and these five fields. It validates descriptor spans,
overlap and initial links, enumerates the13 catalog parents, follows only
direct child arrays, and requires a null terminator in their16 slots before
the mutable`C4h` link. It does not recurse into a child's own array. A parent
is called only when it is itself a listed child. Unselected adjacent fields
remain data, and nonnull selected targets must be executable title code.

All43 callback/sparse-jump/profile/LOOP tests pass, including the earlier child
rules after the extraction refactor. The new synthetic fixture checks all
five fields and walk fingerprints, null callbacks, invalid/noncode targets,
ignored parent/adjacent fields and trailing data, unchanged image bytes,
non-recursion and the terminator bound. Shared instruction/runtime behavior
is unchanged from `ff7313f`; no sound or graphics adapter is added here.

Private evidence is `packed-keyframes/object-walks-audit.txt`,
`object-walk-fingerprints.json` and `native156-child-6c.json`. Native157 uses
private `object-callbacks` output. Its game-embedded diagnostic package must
not be distributed. A visibly verified original main menu remains the goal;
this discovery change alone does not establish rendering success.

Native157 executes the new callback group and reaches the next original
child field, `38h`: target `BE760`, caller `108B80`, return `108BCC`.
Regeneration adds21 reachable functions,435 blocks and2,940 instructions,
with no increase in unsupported instructions (3,825). No callback result or
unsupported instruction was suppressed.

The actual window clip visibly shows the Microsoft Game Studios intro in
`native-157-view/early-movie-middle.png`, SHA-256
`01cc1cd8a33b40e63039f342aa4a91636b2ea9abac0c00332f2726cb72701319`.
Final frame135 remains black; the channel snapshot is unchanged and the
original main menu is still absent. The private capture reader now records
a short clip after the first nonblack output instead of relying on a single
transition-frame still. This changes no guest state or runtime rendering.

All171 completed dependency targets are verified. Native157 ELF SHA-256 is
`b071588ec295f6900b698284af607cbad9ea498d650d0ab83f98f8ea651023d6`,
EBOOT `06782593fca3fec2bd9524be1905520e57d0b03df1a494126f5521a7ca50d198`,
trace `66f2e5bc100b4988bd808dcf169cdf08c681ed24b37a1c4b67ae4cb648f81edd`.
The private checkpoint is `native-157-artifacts`, `native-milestone-157.json`
and `native-157-view`; the owned emulator and native build have stopped.
