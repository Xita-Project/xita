# Halo 2 indexed CRTC constructor checkpoint

Native attempt 15 completes the original driver's CRTC unlock and TV-output
latency update. It stops on an unsupported PVIDEO read at `0xFD008088`, instruction
`0x401ED1`. Device creation has not returned; no frame or menu has rendered.
This closes the small indexed-register step after the instance-memory clear.
Further work should target a supported device/submission contract, rather than
extending display-register storage without a rendering consumer.

The opt-in Halo 2 D3D adapter now translates scalar byte/word MOV memory accesses
as well as dword MOV and byte/word MOVZX to dword registers. Ordinary memory uses
the existing page-aware copy functions. Partial register loads preserve the
other lanes, including AH and an address register aliased with the destination.
Segment-register moves retain their existing path. No shared lowering or CE
profile changes are included.

The narrow PRMCIO model supports byte accesses to the CRTC index/data ports. It
models index `0x1F` lock status (0 locked, 3 unlocked), unlock `0x57`, lock `0x99`,
and unlocked index `0x52` latency storage. The lock values are documented in the
[pinned envytools register definitions](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/rnndb/display/nv_vga.xml).
The latency register's name and byte layout are identified by
[Cxbx's NV2A definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/devices/video/nv2a_regs.h).
Initial locked/zero-latency values and reset with the virtual PCRTC gate are
explicit virtual boot configuration; this is not a captured retail reset state.
Unknown selected registers, packed port writes, locked extended-register access,
and unsupported lock commands stop without mutating state. Actual display
timing, scanout and video processing remain unimplemented.

The bus report counts instance-memory writes without spending its 128-register
log budget on the clear loop. The native trace records:

| Instruction | Operation | Value |
| --- | --- | --- |
| `0x401E98` | Select lock register | `0x1F` |
| `0x401EA0` | Read lock | `0` |
| `0x401EAD` | Unlock | `0x57` |
| `0x401EB4` | Select latency register | `0x52` |
| `0x401EBB` | Read latency | `0` |
| `0x401ECB` | Write incremented latency | `4` |
| `0x401ED1` | Read unsupported PVIDEO `0xFD008088` | Stopped before supplying data |

The prior instance clear still records 5,120 writes to 5,120 unique words of the
`0x5000`-byte claim. Vita3K completes diagnostic exit and returns to idle; its
isolated process was then stopped.

Validation: 14 Python regressions; all five Halo 2 host executables; register
ASan/UBSan; four-job native build and the owned Vita3K trace. Tests cover partial
registers, flags, high-byte stores, cross-page word copies, indexed lock/readback,
state isolation on rejection, and disabled-unit behavior. The regenerated
experiment retains 10,316 candidate functions and 3,754 unimplemented instruction
sites; these discovery numbers are not compatibility claims.

Artifacts remain private under `private/native-15-artifacts`, with the complete
manifest at `private/native-milestone-15.json`. The diagnostic VPK embeds owned
game code/image data and must not be uploaded as a distributable release.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `c0bd5676b2c6a3fe8174c27b8bc6dabd26edebf2d736be978efdeecb02df0de9` |
| EBOOT | `063420b98f967e0ba39f4bac2b518c14de86f7c034cf4f90d4b7bb3e449440a8` |
| VPK | `865ce800e178e34471d6e8d345cf1af518eae41fe8354763aefb512b21f54ed5` |
| Native trace | `4ca052b373a90e15bb653706a9af060c5ff88504a12e141ce73b442cb6787d15` |
