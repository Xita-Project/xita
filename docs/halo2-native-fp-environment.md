# Halo 2 native floating-point environment

Native58 runs both original SSE environment pairs in `53580`, then reaches an
undiscovered virtual method at `6E130`. The display remains black; no main-menu
map load or geometry is observed. This continuation uses the corrected timer
ABI and the explicit unavailable-audio diagnostic.

## Deliberately limited environment bridge

The current recompiler emits SSE arithmetic as native C floating-point work and
does not maintain a separate architectural MXCSR exception history. The H2-only
hook now lets the observed mask/clear sequence operate on that actual native
environment. It does **not** establish fully virtualized x87/SSE status isolation,
complete SSE exception equivalence, or precise denormal-exception behavior.
Those require a broader arithmetic/runtime implementation. In particular, native
status can include x87 or runtime floating-point operations too. No constant
exception-free status is returned to hide that limitation.

The bridge accepts only all exceptions masked, nearest rounding and gradual
underflow. Unmasked exceptions, other rounding modes, DAZ, FTZ, reserved MXCSR
bits, unsupported native control modes and unmapped guest spans stop explicitly.
The six status bits are translated to/from native FPSCR sticky bits. STMXCSR
stores the resulting word; LDMXCSR applies the status bits to the real native
register. Neither changes CPU integer/FP register state or unrelated FPSCR bits.
Diagnostics restore native status after logging so they cannot silently change
the environment. Each Vita guest fiber is an actual SCE thread with its own
native register context. This bridge does not change the shared `xctx` layout or
ordinary/Halo CE instruction lowering.

The field mapping follows the
[Intel software developer manual](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
and [Arm's Cortex-A FPSCR definitions](https://arm-software.github.io/CMSIS_5/Core_A/html/group__CMSIS__FPSCR.html).
Intel status bits invalid/denormal/divide-by-zero/overflow/underflow/precision
map to native bits `0/7/1/2/3/4`. This register mapping alone does not claim that
every native arithmetic instruction generates exactly the same status as x86.

## Validation and native result

All 18 host executables and 33 Python regressions pass. The environment test
also passes ASan/UBSan. Host fixtures cover all 64 status combinations,
unaligned accesses across independently mapped pages, full CPU preservation,
unchanged input memory, reserved/control/mask rejection and unchanged state
after faults. Emission tests decode synthetic instructions, check indexed/FS
addressing, reject unsupported address forms and confirm generic emission stays
unchanged.

The actual native58 ELF helpers also pass 128 status transfers under Unicorn
2.1.4 configured as Cortex-A9, using the real VMRS/VMSR functions. CPU context
remains unchanged and deliberately altered diagnostic FP status is restored.
This checks native instruction/register behavior, not game rendering or complete
arithmetic equivalence. The private reproducer is `check_fp_arm.py`, with output
in `native58-fp-arm-test.log`.

Native57 first captured FPSCR `60000011` at the old strict STMXCSR stop.
Native58 logs the original execution:

```text
STMXCSR 535A2: native=60000011 value=00001FA1
LDMXCSR 535B9: value=00001FA1 native=60000011
STMXCSR 535DB: native=60000011 value=00001FA1
LDMXCSR 535E8: value=00001F80 native=60000000
```

The next stop is original indirect target `6E130`, dispatched at `6DF3B` in
`6DEC0`, object `52738C`, return `6DF3D`, ESP `005E5F84`. It is an undiscovered
method body, not an original assertion. Its vtable/constructor will be audited
before adding roots. Instruction failures now also capture the actual channel
state, avoiding reuse of stale snapshots.

Native58 JSON parses with completion=1 and SHA
`bd6eaf29545dd91c4c4c280abab21c49e597e178e48c017326176586a9a1e962`.
GET=PUT=`03B43280`; the second channel has not submitted its queued setup yet.
The palette-state change remains host-tested, not natively exercised on this
corrected startup path. Private generation is `fp-environment/generated`,
artifacts are in `native-58-artifacts`, captures in `native-58-view`.
The emulator was stopped after capture; no hardware was accessed.

| Native 58 artifact | SHA-256 |
| --- | --- |
| ELF | `2c4eb55ce74fce959bd55502ecc6f7613b579ca40cfec62043b5a180934c7867` |
| EBOOT | `210779b4b4f2462f9cae3471b6aa110b9541350e6c4ed73f68bccd1507d61907` |
| VPK | `4bd6f37af9b67dc54179c249c66bbdda79709c2a3d15d5af992ecedf63457696` |
| Boot trace | `fdb4d32242a6deb4e04158719b239c7632a547c9ac3575d246fbdd9c1ec052e5` |

Exact replay, using an unused label:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay58-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-58-artifacts/halo2-boot.vpk
```

The diagnostic package embeds owned game code/image data. Keep it, generated
code and captures private and out of Git; do not upload distributable releases.
