# Halo 2 first original quad submission

Actual Vita3K native72 reaches the original first quad BEGIN command. It stops
at class97 `17FC=7`, GET `03B4438C`, PUT `03B44520`, through guest `3FAC58`.
No BEGIN, vertex emission or END has executed. The actual desktop capture and
last presented buffer remain black; there is no displayed movie frame or menu.

The command consumer now retains six additional texture descriptors for each
of four units: offset, original format/DMA selector, addressing, pitch, filtering
and rectangle. These are inert method inputs, including invalid/unmapped resource
descriptions: storing them does not read, allocate, resolve DMA, upload or sample
a texture. A future draw must validate selected formats/modes and the entire
source span before accessing any guest data. The unknown offset18h method and
fifth-unit range still reject. The pinned [xemu texture method implementations](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c#L2523)
separate these assignments from later resource binding; original command words
are retained here, rather than asserted to be a complete hardware register file.

The observed non-executing scalar setup is also retained: Boolean lighting,
point and specular enables, bounded light masks/control, fog equation/source,
point size, raw fog parameter bits and texture-shader stage modes. Invalid enum,
mask and size inputs reject. The [pinned method definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h)
identify these fields. Shader-stage mode validity remains a future draw check.
All 21 host executables plus the timed case pass; command tests pass ASan/UBSan.
Full-state comparisons cover all four texture units, repeats and unmapped inputs
with failed mappers, and verify no clear state, resource or guest-byte mutation.
Draws, END, immediate positions and packed vertex color still reject.

Independent parsing verifies 30 final texture/scalar words in native72's decoded
snapshot. Texture0 selects a 640x480 linear X8R8G8B8 image at offset `01336000`,
pitch2560, and filtering word `02062000`. The pending original packet contains
four positions and texture coordinates spanning (0,0) to (640,480), with white
vertex color, and the 21-slot shader previously observed in the XBE. Its current
c10=(320,-240,16777215,0), c11=(320.5,240.5,0,0). This establishes a concrete
rendering boundary; it does not prove a valid texture span, shader translation,
combiner execution, geometry output or a correct presentation. The next task is
read-only resource validation/capture and a tested backend for this actual draw.

| Private native72 artifact | SHA-256 |
| --- | --- |
| ELF | `b08758f73ffc566a12f10ecd35c6816822370850ef033356349cbd00525638c3` |
| EBOOT | `c94800dc8564ce84078237f13351a47574e0012763127f96f353b1a94afd28b2` |
| VPK | `3de5d6f5825be0b52ca2f8a7b879f3d8def7278728e7764e5c2bc190f5e40fae` |
| Boot trace | `40f1defc077d50b86c199e7e1cef6ec0d4c6b8fce5a31e58a804685fe66d3899` |
| Channel snapshot | `a008f536b05e8e3aebcea56d148da630b0cd302f8d74797417b76d673a4bf659` |
| Last presented buffer | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Replay: `python3 ../private/run_lab.py replay72 ../private/native-72-artifacts/halo2-boot.vpk`.
Frozen generated source remains `private/bink-pixel-callbacks/generated`.
The VPK embeds owned game code/image and must not be distributed or uploaded as
a release. Game-derived packets, private inputs, generated source and captures
remain outside Git. The unavailable-audio diagnostic remains explicit.
