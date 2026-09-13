# Halo 2: original constructor reaches a bounded host DMA channel

The opt-in host-channel experiment now runs the original device constructor
through DMA/context object creation, the bootstrap jump and the first push-ring
submission. The constructor then allocates a 2,457,600-byte framebuffer resource
and stops at an unsupported FIFO-control access in its tile/resource setup.
**No native clear, displayed frame, title screen or menu has been observed.**

This is a separate profile from the original strict graphics-bus experiment.
The original experiment still stops at the PVIDEO read `FD008088` in `401ED1`.
The host-channel profile replaces three complete, fingerprinted functions with
a synchronous software device contract; it does not claim the omitted physical
NV2A setup ran successfully.

## Contract and scope

| Boundary | Inputs / return | Host behavior |
| --- | --- | --- |
| `3FE005`, 352 bytes | EAX = miniport; RET | Initializes software miniport lists, DPC metadata and gamma tables; executes the original PCI identity, clock and instance-memory helpers; provides a synchronous command consumer without physical IRQ or display registration. |
| `4026CE`, 410 bytes | EDX = miniport, EAX = scheduling priority; three stack arguments, RET 12 | Validates the guest's DMA descriptor, reserved context extent and ring; clears the actual instance context, fills RAMFC/context-table fields, and attaches one bounded channel. |
| `3FADE0`, 45 bytes | RET | Uses a real sequentially consistent host memory fence for shared CPU RAM visibility. |

The adapter validates the complete owned executable and each replaced function's
SHA-256. It preserves the original static device at `404FE0`, miniport at `406C08`,
allocation routines, DMA descriptor creation, RAMHT installation, graphics context
initialization, default-state generation and `3FAC30` submission routine. The
shared Halo CE adapter and runtime defaults are unchanged.

The first submission checks the constructor's real jump word at DMA offset zero
before entering the allocated ring. Subsequent submissions use the checked
[push parser](halo2-submission-boundary-20260913.md),
[DMA resolver](halo2-gpu-objects-20260913.md) and
[clear consumer](halo2-clear-consumer-20260913.md). GET advances only for the
bootstrap transfer, parsed headers and accepted methods. Partial packets retain
their pending word; unsupported methods retain GET at the rejected parameter.
A finite word budget stops cycles. The virtual PUT/GET and busy queries describe
this synchronous consumer; they do not provide GPU fences, vblank or presentation.

Every physical RAM lease validates all cached-alias page-table entries before
returning a contiguous host span. Image pages and unmapped trash pages cannot
become framebuffer mappings. Unsupported MMIO, tile operations, classes, methods,
draws, formats and display behavior still stop explicitly. The current clear
consumer implements only its documented linear ARGB8/Z24S8 subset.

## Native observations

Attempt 16 preserved the original graphics-bus constructor's 9,376-byte device
snapshot at `401ED1`. Its ring is `83C2B000..83CAB000`; the initial free instance
slot is `1112`. Attempts 17 and 18 use the host-channel profile. Their original
guest code creates DMA instance `111C`, context `111D`, and configures the
512-KiB ring at physical `03C2B000` with RAMFC schedule `00086078`.

The first `3FAC30` call submits PUT `03C2B000`. After consuming the checked
bootstrap jump, GET is `03C2B000`, result COMPLETE, clears 0 and pixels 0. Original
framebuffer setup then calls `MmAllocateContiguousMemoryEx` for `00258000` bytes,
alignment `4000`, yielding `839D0000`. The next stop is checked MMIO `FD003214`
in function `3FE927`; static inspection identifies the byte TEST at `3FE92A`,
reached through resource/tile helper `3FE86A`. No unknown register value was
supplied. Both emulator runs returned to Idle and were stopped explicitly.

Attempt 18's device snapshot matches attempt 16 in all 31 compared identity,
clock, memory-geometry and table-address fields. This checks the preserved
constructor prefix, not the omitted display/IRQ setup or complete device ABI.
Snapshots and generated files remain private owned-data artifacts.

| Attempt 18 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,290,492 | `989eadb3da2facbc4270bd834968d517576c509cf6617d87516adf297add4a81` |
| EBOOT | 80,013,326 | `32d367eeff3cf1a44e55785fbdb801cff7248d16b3f5b545eedc761185279ef3` |
| VPK | 22,780,235 | `1ff606fb4d1c134c65079b322266bda2d809b8d45fa20350e8d228ab5363ff1a` |
| Emulator trace | 299,999 | `63caee90b8f5e7ce8bcb3fd55029cf5951331c445335a2461146a6a66297ebbb` |
| Boot log | 193,030 | `abc2ddc354b7f76003d6edad9d312bb5d8b1f7cd6262a1ab358c8051df2d1709` |
| Device snapshot | 9,376 | `a4d640e045c11531dffe451881cdec17d9164dfe8c0aa3856d90b267d5851548` |

Private files are under the agent backup's `private/native-18-artifacts/`, with
`private/native-milestone-18.json`; attempts 16 and 17 are retained separately.
The executable/image hashes are unchanged from the base Halo 2 profile.

## Reproduction and checks

Use an independent output/build directory for this profile and `HOST_CHANNEL=1`.
Do not reuse a build directory from another profile or switch its build mode.
The diagnostic VPK embeds owned game image/code and **must not be uploaded as a
distributable release**. Keep the image, generated C, snapshots, logs and package
outside Git and use the isolated emulator lab from the native-boot report.

```sh
python games/halo2_5849/prepare_boot.py /path/to/owned/default.xbe \
  --host-channel --out /private/path/host-channel
make -C games/halo2_5849 -j4 HOST_CHANNEL=1 GUEST_OPT=-O0 \
  GENERATED=/private/path/host-channel/generated \
  BUILD=/private/path/host-channel/build \
  IMAGE=/private/path/host-channel/halo2_image.bin
make -C games/halo2_5849 test-host BUILD=/private/path/host-channel/build
```

All ten Halo 2 host executables pass. The new channel tests cover DMA permissions
and extents, exact bootstrap checking, split PUT, non-replayed commands, cycle
budgets and honest rejection. Runtime tests cover the LTCG stack/register
contracts, software instance state, real parser-to-framebuffer writes, broken
physical-alias rejection and fatal unknown MMIO. Both new test executables also
pass ASan/UBSan. Fourteen profile, scalar-bus, checked-address, LOOP and half-SSE
regressions pass. These are synthetic inputs; no owned executable bytes are
tracked in tests. The native runtime build emits no warnings.

The next bounded task is the tile/resource configuration contract around
`3FE86A`, then the first real default-state command submission. It requires a
supported memory-layout model and explicit command semantics, followed by native
evidence. A general NV2A emulator, shader/draw support and presentation remain
outside this checkpoint.
