# Halo 2 palette descriptor state

Native53 stopped on Kelvin method `1B20`, value `037CE000`. The captured tail
contains the same assignment for all four texture units at stride `40h`.
[Pinned xemu handlers](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
identify this as a palette descriptor assignment: DMA selector bit 0, length
bits 2..3, and offset bits 6..31. Assignment does not read or decode the palette.

The H2-only command consumer now stores these exact inputs in its existing setup
array and validity bitmap. It rejects reserved bits 1, 4 and 5. No palette
allocation, guest memory access, image decoding or draw execution is introduced.
A future texture backend must validate the selected DMA and full palette span
before reading it. Unknown methods, resource bindings and draw paths remain
fatal. The existing decoded snapshot includes this state without schema changes.

All 16 host tests pass. Tests cover each unit, length and DMA selector, exact
full-state preservation, no guest/instance-memory mutation, reserved bits,
alignment, class isolation and unchanged rejection of draw/vertex methods.
The command-state test also passes ASan/UBSan.

## Native 54: an earlier timer ABI fault

The built package launches, but this run stops before the palette assignment.
The preserved stack-window guard catches access to `D2F650D0` from the shared
`KeSetTimer` implementation. That value is the low word of an inline 64-bit
deadline, not a pointer. Original caller `332B7A..332B8E` pushes DPC, deadline
high word, deadline low word, then timer; the API takes four stack words. The
existing runtime instead dereferences the low word and pops three words.

The time value changes between runs, so earlier native continuation depended on
which guest page it accidentally addressed. Native53's graphics capture remains
real evidence of its strict submission stop, but it does not establish correct
timer execution or call-stack balance. This must be corrected before claiming
native palette validation or further startup progress. The guard is not weakened.

Native54's complete decoded snapshot remains the earlier empty second-channel
state, SHA `f461fd24cc4d5a7292da55faa2f857f8df7d93b89f86f2f97921eabe54f70327`.
No menu is visible. Private traces/builds are archived in `native-54-artifacts`
and `native-54-view`; the emulator was stopped after capture.

| Native 54 artifact | SHA-256 |
| --- | --- |
| ELF | `6875d9003e10575d72519ecd8345ee2335eb4a578f63050ff06c6547356b3b7d` |
| EBOOT | `5f7d8d219bb55bf7a22885cacd95b76e33a5549aba34411e4e03fd9fd9d0d196` |
| VPK | `1541b5816976c41c94c7d07e40403cbfc9e78d2897b9f366e6e6179c21a52044` |
| Boot trace | `365dc98ea02167870541c82241f5404f0f678ff88763ce2915fe66875b6e0963` |

The diagnostic package embeds owned game code/image data. Keep it, generated
code and captures private and out of Git; do not upload distributable releases.
