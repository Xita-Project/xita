# Halo 2 kernel stack allocation

Native50 reached `MmCreateKernelStack(6000h, 0)` at `33564D`, return `335653`.
The original caller checks for null, returns `8007000E` on allocation failure,
and deletes a competing allocation with `(top, top-6000h)`. Its later cleanup
uses the same pair. This is an actual kernel memory service, not a callback
that can be replaced by a successful constant return.

The public signature and downward-growing top/deletion contract agree with
[Cxbx's kernel exports](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlMm.cpp).
Its [memory manager](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/memory-manager/VMManager.cpp)
reserves an extra lower virtual guard page without allocating a physical page
for that guard. Usable memory is read/write, with page-rounded backing. Debugger
stacks require a separate devkit memory arrangement.

## Scoped implementation

Only the independent Halo 2 target wraps create/delete. Existing shared memory
allocation and Halo CE behavior are unchanged. The backing allocator owns real
pages; the wrapper aliases each page independently into the H2-reserved guest
window `D0000000..D4000000`, preserving any pre-existing mappings. This window
is an implementation choice, not a claim that hardware must choose the same
address. The lower guard stays unmapped; the returned pointer is the requested
usable end. No physical contiguity is assumed. Up to 64 live records are tracked.
Zero/oversized requests or exhausted resources return null without a success
object. The API's BOOLEAN uses its low byte; nonzero debugger requests stop
explicitly because devkit stack memory is unsupported.

The full argument span is checked before access. Deletion accepts only an owned
exact `(top, limit)` pair, verifies its backing size and every page mapping,
then frees backing and removes aliases. Incorrect pairs, changed mappings and
repeated deletion stop before mutation. Create changes guest EAX/ESP; the void
delete changes ESP only. Flags, other registers, FP state and caller argument
bytes are preserved.

The checked boot runtime also rejects access to unmapped pages in this stack
window, including lower guards and retired stacks. This is a diagnostic fault;
it does not implement resumable guest stack-overflow exceptions. Shared
virtual-memory/allocation-size queries do not yet enumerate these high aliases
as VAD allocations. Address-validity and physical-address queries use the real
page map; broader alias-query support has not been exercised or asserted.

## Validation and native 51

Host tests use the actual shared allocator and verify zeroed data, guard-page
accounting, page boundaries, unrelated mappings, byte-sized requests, BOOLEAN
width, all CPU/FP state, argument bounds, deletion ownership and no mutation on
rejected calls. They fill the 64-record limit, exhaust real physical memory,
then free three nonadjacent physical pages and successfully use them for one
stack. Normal and ASan/UBSan runs pass; all 16 Halo 2 host executables pass.

Native51 returns the real stack top `D0007000` for the observed `6000h` request
and continues in original code. This trace proves allocation and startup
continuation; it does not yet prove native execution on or deletion of that
stack. The next missing method is `3331CC`, dispatched from `332780` within
`33275E`, return `332783`, object `462E00`, vtable `417378` slot `+28h`.
The private channel snapshot parses with completion=1 and remains identical to
native48/49 (`f461fd24...70327`). Device/push captures and the first black scanout
are also unchanged. No menu or geometry is rendered.

Private artifacts: `native-51-artifacts`, `native-51-view`,
`native-milestone-51.json`, `native50-stack-audit.txt`, `native51-next-boundary.txt`.
Generation is unchanged from `mode-arena/generated`; only native harness/kernel
objects were rebuilt. No emulator process remains after the archived run.

| Native 51 artifact | SHA-256 |
| --- | --- |
| ELF | `9289c4e4245bfd4ffac463a76bf1b6330f91344cf07b147c4d9d7f7c044daaca` |
| EBOOT | `7ed03e8d3d0bbf6f23e98f2c22a1d2ed23301ad69108349d44b31461a92ff7b8` |
| VPK | `2132443c01cb128d69985e62936ca5968e0c2d8b7ce649f5fc8db61e30d5fdac` |
| Boot trace | `5c558694c5d50dc845674db7f47d89815660f969aaea90650120d7debcebd3ae` |

Exact replay with an unused label:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay51-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-51-artifacts/halo2-boot.vpk
```

The run still uses the explicit unavailable-audio diagnostic, not a working
audio backend. Owned code/image data and shader snapshots remain private.
Do not upload the game-embedded package as a distributable release.
