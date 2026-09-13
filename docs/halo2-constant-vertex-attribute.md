# Halo 2 constant vertex attribute 15

Native60's original submission stops at method `1AF0`. Its four-component
packet `1AF0..1AFC` carries `0`, `3F000000`, `3F800000`, `42280000`.
The pinned [xemu method implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
and [vertex bookkeeping](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/vertex.c)
identify this as `SET_VERTEX_DATA4F`, attribute 15. Each method replaces one
component. Completing attribute 0 emits a vertex; attribute 15 does not.
Attribute changes can backfill earlier inline vertices in an active draw,
which this consumer cannot have because begin/end and all emission commands
are rejected.

The supported boundary therefore stores only `1AF0..1AFC` in the existing raw
setup-word bank, with independent component validity. It accepts every bit
pattern without host floating-point evaluation. Other float attributes,
position emission, packed aliases of attribute 15, begin/end, inline vertices,
indexed vertices, and draws remain unsupported. No shader runs, no guest
memory is read or written, and no graphics output is implied. A future backend
must honor the retained attribute and implement the active-draw bookkeeping
before accepting those execution commands.

Tests cover all four components, repeated writes, the observed values, signed
zero, infinity and quiet/signaling NaN payloads. Full command state, clear state
and fixture memory comparisons verify that only the target word and validity
bit change. They also check wrong-class and misaligned writes, every adjacent
float-attribute method, the packed alias, and strict begin/end rejection.
All 18 host executables and the command-state ASan/UBSan test pass.

Native61 preserves the actual four words in the decoded `setup` array and sets
all four validity bits (`setup_valid[53] = F0000000`). The original submission
passes them and releases two more real semaphore writes. Startup then stops
at XDK software method `0100 = 71D00021`, source/GET `03B43FB0`, PUT `03B43FF4`.
Selector 1 requires a separately audited execution contract; it is not ignored.
There are still two clears/921,600 cumulative pixels, three semaphore releases
(last value 9), and no draw. The actual `native-61-view/window-1.5.png` is black,
as is the first scanout SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
No main-menu map load or visible menu is observed.

| Private native61 artifact | SHA-256 |
| --- | --- |
| ELF | `3d10929de293e236c2a8ce21097081ad49ed5c16c6352c70ace09b5f5a881ef4` |
| EBOOT | `906cc3dd09d08059794ed01053fac8b411e8288283bb51ff399acffcf371d30a` |
| VPK | `58b50f5254090929617bf2c5f6bb5cb260d2c97ba47082b83053eca3da32bf5d` |
| Boot trace | `19e0ab92b265e739cfe4fa138863644dac89e471a6dfe01d97cc3315aa1ac0e8` |
| Decoded channel | `4804aaa649c5a7ba5c7f6dbea09dcfc738908f1fa7235fe9105eb16fb4df4d8e` |

The generated source remains the private native60 `online-interfaces/generated`.
Replay from the source directory with
`python3 ../private/run_lab.py replay61 ../private/native-61-artifacts/halo2-boot.vpk`.
This package uses the explicit unavailable-audio and limited native FP probes,
embeds owned game image/code, and must not be uploaded as a release or distributed.
