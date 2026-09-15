# Legacy scalar comparison and sign-mask extraction

Native178 reaches the original Halo 2 widget/menu setup sequence at `2CE15`
in `2CBF0`: CMPNEQSS, CMPLTSS, ANDPS, MOVMSKPS, then integer subtraction.
The comparison was unsupported. This change translates legacy CMPSS and
MOVMSKPS so the original numeric sequence can run. It does not substitute a
calculated answer, skip setup, or add packed/double/AVX comparisons.

CMPSS compares only the low scalar and writes either all ones or zero there;
the destination's upper 96 bits remain intact. Legacy predicates 0 through 7
cover equality, less-than, less-or-equal, unordered and their complements.
Intel reserves the other immediate bits, so noncanonical predicates retain an
explicit unsupported stop. MOVMSKPS extracts the four sign bits into the low
four bits of the destination GPR, zeroing its upper bits and causing no SIMD
floating-point exceptions. These contracts follow the CMPSS and MOVMSKPS
entries and comparison predicate table in the [Intel SDM, revision 089](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).

The ARM comparison uses integer bit classification to retain signed zeros,
subnormals, infinities and NaN payload behavior without host FP conversion.
Every predicate signals invalid for a signaling NaN; predicates 1, 2, 5 and 6
also signal for a quiet NaN. A NaN suppresses denormal status from the other
scalar operand; the independent native-x86 oracle checks this exception
priority. Upper lanes do not participate in either the result or exceptions.
Memory sources read exactly four bytes, including unaligned/page-crossing
sources, and same-register operands use the original bits.

As with the reviewed packed MINPS/MAXPS path, ARM rejects default-NaN,
flush-to-zero, vector length/stride and unmasked exception controls before
changing destination or FP status. All rounding modes are supported; unrelated
FPSCR bits and earlier cumulative exceptions survive. Native x86 builds execute
the real SSE comparison and honor MXCSR. MOVMSKPS uses integer extraction and
does not require those FP restrictions. Other architectures keep the comparison
unsupported. The shared helper/emitter changes apply only when regenerating
these instructions; no existing CE game profile or runtime hook changes.

## Validation

All fixtures contain synthetic code and values, with no owned executable bytes.

- 808,192 integer scalar/result/status comparisons against native SSE cover
  all eight predicates, the 32-by-32 corner-pattern cross product and random
  inputs. NaNs, signed zeros, infinities and denormals appear in both orders.
- 329,728 emitted full-context/memory/native-MXCSR cases pass normally and under
  ASan/UBSan. They cover every XMM pairing, scalar memory sources, upper-lane
  preservation, all GPR destinations (including ESP), all 16 sign masks,
  rounding/DAZ/FZ controls, prior status flags and reserved predicate stops.
- Cortex-A9 execution at both `-O0` and `-O2` matches native x86 in
  45,056 full 360-byte context,
  memory and FPSCR cases per build. Another 256 reserved encodings and 4,032 unsupported
  writable FP controls stop without context/status mutation. MOVMSKPS preserves
  state across 448 writable special-control cases. The harness excludes 3,840
  control probes that Unicorn exposes as read-as-zero; these are not counted
  as tested rejections.
- All 44 Halo 2 host executables and 70 focused callback/profile/branch and
  preceding SSE regression tests pass.

Run `python -m unittest tools.test_sse_scalar_compare`; set
`XITA_TEST_SANITIZE=1` for ASan/UBSan. The independent Cortex-A9 runner is
`tools/test_arm_sse_scalar_compare.py --output-dir <private-directory>` and
requires the VitaSDK, Unicorn and pyelftools. Use `--optimization=-O0` to
match the diagnostic guest build (the default is `-O2`). Private logs are under
`scalar-comparison/`; the synthetic `-O2` ARM ELF SHA-256 is
`99f9d1415bfe964025a27a41611d0e87ac5d0c8f70850be059c1de4dcc1438ab`;
the `-O0` ELF is
`318b82c42dad5af1396adf75d80b01a2fd1a8a5841915ec7ee4cd8d4e5a1c671`.

Regeneration retains 12,549 functions, 170,285 basic blocks and 1,189,027
instructions; unsupported instruction sites decrease from 3,792 to 3,663.
The report is otherwise identical: 34 CMPSS and 95 MOVMSKPS sites account
for the entire difference. These are automatic translation counts, not runtime
compatibility evidence.

## Native179 outcome

The private native build passed, including verification of all 171 dependency
targets. Native179 shows the original Microsoft Game Studios intro and finishes
the original 59,670,016-byte map copy before normal Start. It passes the previous
scalar-comparison stop and reaches a new vertex-array graphics submission:

```
PUT=03B80158 GET=03B7A45C result=6 source=03B7A45C
word=03131000 sub=0 method=1720
NV2A write eip=003FAC58 address=FD800040 value=03B80158 reason=3
```

The command remains unconsumed. No graphics handling was relaxed. The snapshot
records packet header `03B7A458`, 16 remaining words and incrementing methods.
The current consumer lacks vertex-array binding; the next task is to audit the
captured binding/format and eventual draw contract against the actual data and
primary NV2A references. The last presented frame is still black frame 135;
**the original main menu is not visible**. The actual transition recording is
60.9 seconds, ending at the controlled stop. Audio behavior is unchanged,
including the explicit unavailable multibin diagnostic.

Private evidence: `native-179-artifacts/`, `native-179-view/`,
`native-milestone-179.json`, and `scalar-compare-boot/`. The actual intro capture
`early-movie-middle.png` was viewed; its SHA-256 is
`9dbd001ec91e6d57215d2c865217f93c68fa6c17ad87a7c558feb0f0b898fc19`.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `8e232558f0b73e91541fbb9a5a149895c1783f5fcee8dc0f1cfd96edbc7f01c3` |
| EBOOT | `2453fede1ee428288b09debd00202addce5eefd3c120e2571ad51c5f82af6829` |
| Guest trace | `b83c32f3bd8c564380c80882607ef7f8ffbc6ec754650c24ceb987cef3035896` |
| Decoded channel | `089bab7c1eb7ee3f11affda9f0b4746e14272d3cad633dfc9e7411cb524995b5` |
| Raw push | `398bff2c3619cd0da134b8ed173c204753daa001e930f10d3074e80f3c45cbd0` |
| Last frame | `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893` |

For an exact replay from the private directory, while its owned emulator is
stopped, run `python3 preserve_fresh_cache.py native179-replay`, then
`python3 capture_run.py 179-replay native-179-artifacts`, then
`python3 drive_startup.py 179-replay native-179-artifacts`. This retains the
original cache format/copy and normal Start path. The diagnostic package embeds
owned image/code and must not be uploaded as a distributable release. The
native/capture processes are stopped; the owned `:111` X server remains running.
