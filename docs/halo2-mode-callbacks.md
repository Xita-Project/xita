# Halo 2 paired startup callbacks

Native47 reaches the original callback dispatcher at `18EF00`, then stops at
`81780` from call `18EF8B`, return `18EF8D`. The caller selects a state value via
`18EEB0` and compares it with `4ED290`. On a change, it may call the first field
of eight records at `453C00` with stride eight, then calls each second field
at `453C04` with the selected value on the stack. Both loops stop after `40h`
bytes and skip null callbacks. Discovery includes these exact 16 fields;
original control flow, argument passing and all callback bodies are preserved.

The pinned 152-byte dispatcher fingerprint is
`c499facfbe49993ebd3e15bb55a4f65adafb4bfd53eb99474ba7bb96ad3f8102`.
Preparation rejects changed callers and non-null targets outside executable
`.text`, while retaining null-skip semantics. All 16 fields in this owned image
are non-null valid code pointers. Synthetic tests exercise both fields, table
bounds, null entries, invalid final targets and caller mismatch. All ten
callback-root tests pass.

The normal audio-hardware profile remains strict. Native testing below uses
the explicitly selected unavailable-audio diagnostic and does not establish
audio support. No callback result or guest data is fabricated by these roots.

## Native 48 result and allocator continuation

Native48 executes inside the original first callback `81780`; its call at
`81F24` enters `32A850`, which reaches an allocator method at `32A881`. The
callback has not yet completed. It stops at undiscovered `1A1A80`, dispatched
through `[454970+4]`, return `32A884`. The original method returns its real
constant `1000h`; no replacement is installed. The object was constructed by
original code at `81EC2`, which assigns vtable `454970`. The independent
assignment at `32A871` establishes the next vtable at `454980`, bounding this
allocator interface to four executable methods. Preparation now validates and
adds the whole four-slot interface, with both assignment fingerprints checked.

| Assignment | Length | SHA-256 |
| --- | --- | --- |
| `81EC2` | 6 | `12ddc689a0e651bd82e523e0305410591c34c0ecd1da8fda19d119363ba1520f` |
| `32A871` | 6 | `090c672f0e3b32b564eec33dbae02580b53d91bf1c9d4e1eb0b378b0d2faa881` |

Native48 also integrates the read-only decoded channel snapshot. Its JSON parses
and completion is logged true. GET=PUT=`03B54280`, pending count zero, second
channel program bank zero and surface format/pitch zero. The device and push
snapshots still match native45. The actual native48 display is black: no menu
or geometry has been rendered. The screenshot, logs, raw captures and decoded
state remain private in `native-48-artifacts` and `native-48-view`; frozen
generation is `mode-callbacks/generated`. All 15 host tests and 27 Python tests
passed for native48. The added allocator bounds test brings the focused callback
suite to 11 tests for the next build.

| Native 48 artifact | SHA-256 |
| --- | --- |
| ELF | `b28e99d7382abf3e976c14f960d7d032313cbdad59e4f8e975dc8de3b071fe08` |
| EBOOT | `72d167669c3cf40622c33906b17956e60e3a86c626226c0777d9ce729688c799` |
| VPK | `9fc8f7b45ccf6346d185632fcdffe3db2324d5b0dec8125f492454ff0c0da5f0` |
| Boot trace | `0c2635ff32a56f4fe32f67746091189f0398db2f926b8c9e043bc0e0e913a34d` |
| Decoded state | `f461fd24cc4d5a7292da55faa2f857f8df7d93b89f86f2f97921eabe54f70327` |

Owned generated code, snapshots and packages must stay private and out of Git.
These diagnostic packages embed game code/image data and must not be uploaded
as distributable releases. No hardware deployment or storage-device access is
part of this work.

## Native 49 and the global arena

Native49 passes the four-slot allocator and completes first callback `81780`.
It reaches second callback `1A474C`, whose direct path `18E1F0` calls global
arena object `47D924` at `18E208`. The stop is original target `531B0`, return
`18E20A`. The pinned image stores `4508FC` in that object; `4508FC..450904`
contains exactly the allocate/free pair (`531B0`, `72C70`) and is followed by
string data. Preparation validates that stored binding and both code targets
before including the pair. The original allocator rounds and updates the real
guest arena state; no host replacement or canned allocation result is used.

The unrelated 16-slot vtable beginning at `454980`, identified during the
constructor audit, has not been added speculatively by this milestone.
Native49 decoded channel/device/push state remains identical to native48;
no second-device geometry or menu has been submitted. Private generation is
`mode-allocator/generated`, with artifacts at `native-49-artifacts` and captures
at `native-49-view`.

| Native 49 artifact | SHA-256 |
| --- | --- |
| ELF | `d3da21dcf014f7ee6f40d9b36b1558e5b6c762121084cdb152bdf28ceb123c8c` |
| EBOOT | `01118e28f8c27fed9a43f8915a69f041f0ef7905e34e25ef6c8297cf4f1dcd97` |
| VPK | `af8885a52aea2a4756146e55e24eaea66c5dfdde48b100b17bcb64470e1b6caa` |
| Boot trace | `8c0b6a22bd3009d6663b0636b89e9d65e6c87768bdd0fa6fc59fdadaf79c67c3` |

## Native 50: a real kernel API boundary

The grouped table roots now execute past the global arena call. Native50 reaches
`MmCreateKernelStack(6000h, 0)` at `33564D`, return `335653`, and the strict
missing-kernel handler stops there. The guest has its own null-return failure
path (`8007000E`), and its paired deletion supplies stack top and top minus
`6000h`. This API requires actual mapped stack memory and guard-page semantics;
no success stub has been added by this discovery milestone.

Native50 uses `mode-arena/generated`. Private artifacts/captures are in
`native-50-artifacts` and `native-50-view`. The kernel-missing stop does not call
the graphics snapshot hook, so there is no new decoded channel/device/push
capture for native50; native49 state must not be mislabeled as native50 state.
Only the first black startup scanout is available; no visible menu is claimed.
All 28 Python regressions pass, including 11 callback-root tests. All 15 host
executables passed after snapshot integration in native48; this continuation
changes preparation roots only. The emulator is stopped after capture.

| Native 50 artifact | SHA-256 |
| --- | --- |
| ELF | `da37f0ec794d40659009153e391047d9a1a2aeaeab4f17ea4d41291686da34d4` |
| EBOOT | `d40356b122a06da0d3859300e72a0b7a39c1d9f95bf2b4b608e747f943cbb0d8` |
| VPK | `53e6de9df637f510ddd1c6fd94b83408e913feede4f2c084b110024729f7053e` |
| Boot trace | `16b6d5c53e63b4f32731b274a9122a6679cb4554e1da9512c02f68702f824fef` |

Exact archived replay, with an unused label:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay50-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-50-artifacts/halo2-boot.vpk
```
