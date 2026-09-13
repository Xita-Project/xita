# Halo 2: checked comparisons and default-state callbacks

Native attempt 26 executes the constructor's default-state callback walk and
queues its first 640-by-480 depth/stencil clear. It stops at an unsupported AV
filter-setting call before submitting that clear. **No native clear, displayed
frame or menu has been observed.**

## Two evidenced execution gaps

The Halo 2 D3D bus adapter now handles scalar memory-operand CMP at 8, 16 and
32 bits. It reads the memory operand through the same strict bus as MOV, captures
both operands before updating flags, and uses the existing subtraction-flag
contract. Other instructions retain their existing paths. The hook remains
restricted to the validated image's D3D section; CE and default emitters change
no behavior.

Synthetic execution covers 1,764 operand/order/width combinations, sign-extended
immediates, high-byte registers, nonadjacent guest pages, register/memory
preservation and unknown-register rejection before flag mutation. CF/PF/ZF/SF/OF
match independent expected arithmetic. The shared runtime's existing `xf_eflags`
omits AF; this change preserves that limitation rather than altering shared
flag semantics.

Attempt 25 consequently passes the PUT/GET comparison at `3FC406`. It then stops
on an undiscovered indirect target `3F5CC0` called by `3FBACA`; this is a dispatch
gap, not an original guest assertion. The target is a real RET-4 state setter.

The original caller's audited loop establishes exactly 29 callback roots:
EBX runs `E4..294` in steps of four, ESI equals `EBX-170`, EBP is `403A48`, and
the indirect call runs for ESI at least `B0`, except EBX `268`. Thus callable
slots are `[403AF8,403B70)` excluding `403B40`. The subsequent explicit call at
`403B14` is already included. The 135-byte walk at `3FBA54` is checked against
SHA-256 `e0cc1649c0b744615b3de0f5b2446411bb408d0b3ce4d1d3da59980219abc70c`,
in addition to the whole-image revision check.

Only `prepare_boot.py --host-channel` adds these roots. Every called slot must
contain a nonzero code address in D3D. Synthetic tests verify exact bounds,
the skipped slot, invalid/missing targets, a wrong section and caller mismatch.
No general data-root scan or default discovery rule changes. The private build
has 453 explicit roots, 10,317 candidate functions, 975,789 decoded instructions
and 3,754 unsupported instructions. These counts do not establish compatibility
or validate every inferred boundary.

## Native checkpoint

Attempt 26 completes the same first submission and semaphore release as attempt
24, then executes the callback walk. Its next AV call is option `0B`, parameter
`5`, output zero, from `3F8C59`: flicker-filter configuration. The existing exact
capability-query adapter rejects it. PUT/GET remain `03C2B280`, clears zero,
pixels zero. The private ring cursor is `03C2BA98`. Its queued tail contains a
640-by-480 clear rectangle, Z/stencil value `FFFFFF00`, color zero and clear flags
`3`; this is pending guest work, not a completed clear.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Attempt 25 ELF | 65,282,568 | `c83f95dbc96d13808b43f727a61ba0b031d9f649950a051811a7beceaa7a9e48` |
| Attempt 25 EBOOT | 80,004,214 | `28465e7f842e62a4dce23441e6cb3ed1b253611f7b295963081958dd4b890f7d` |
| Attempt 26 ELF | 65,338,940 | `122b6ee6d0b2af2a4a7047787271319b84acefe63ae9e12d469ff415b7e2e3fe` |
| Attempt 26 EBOOT | 80,071,206 | `bf22d1c5717f145a1f8644ba8bef4056456a3900cc665ce3ba5d03ec068694e4` |
| Attempt 26 VPK | 22,795,964 | `96d76e4918b5d00971aa5f5680cfc1efed6731490134d4f85f638c72cd1fa652` |
| Attempt 26 emulator trace | 301,778 | `93acf85b7bf5cf40a2a70546eb63a711de81d810fa82b4a5de49695c5b116188` |
| Attempt 26 boot log | 194,190 | `edf8a4c4abe7b401c8e1632c5692351f47230711c95bdecf4eead744b9d327a3` |
| Attempt 26 device snapshot | 9,376 | `ed8c836020e616983067d7fcce84b2bbfc15f07e694278f2e84ebcfd5b896b02` |
| Attempt 26 push snapshot | 524,304 | `cf27ed9fadd53dd11cf134bc871092bfe4aac98b54ac95a26aceda7881cf2342` |

Artifacts are private in `private/native-{25,26}-artifacts/` with matching JSON
manifests. Attempt 25 did not create a fresh graphics snapshot, so none is claimed
for that run. Its intentional runtime-trap exit was followed by a Vita3K host
SIGSEGV during session teardown. Runtime traps now also save bounded diagnostic
snapshots. Attempt 26 reached Idle and its emulator was stopped.

All sixteen Python regressions and twelve host executables pass. The native
build compiled all 128 generated units without warnings. Input game hashes are
unchanged. Reproduction remains the [host-channel build](halo2-host-channel-20260913.md).
Diagnostic packages contain owned game image/code and must not be distributed
or uploaded as releases.

Next is explicit deferred AV configuration, followed by the audited state and
program uploads preceding this queued clear. Presentation must account for those
settings before it can be supported; unknown commands and unimplemented draws
must continue to stop.
