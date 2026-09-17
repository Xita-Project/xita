# Exact packed vertex layout prototype

Private experiment based on `7fdca4c`. Repository default is
`XV_PACKED_VERTEX_LAYOUT=0`; there is no runtime selector or live override.
No shader arithmetic, guest unit, query/solver helper, or graphics setting changes.

The ON build admits only `halo_vs_06` (`3068A44B`), `halo_vs_29` (`F0F51170`)
and `halo_vs_58` (`53C00F6C`). `packed_layout()` checks their complete attribute
declarations, strides, function hash/length and constant window, then checks the
actual linked attributes and compares every validated GXP program byte against
the matching embedded program. GXP file alignment padding is outside the
program. Differing overrides and unknown bindings decline. The alternate GXM
vertex program uses the same shader ID and attributes, changing only stream 0
stride from 32 to 16. Creation failure retains the original.

Capture copies bytes `[0,16)` of every source record without conversion.
VS06/58 fetch position `[0,12)`; VS29 additionally fetches `[12,16)`. VS58's
second eight-byte stream stays original. Immediate, histogram/mesh-capture,
non-32-byte stride, and sparse-reference-eligible draws use the original path.
VS40 is deliberately excluded. Every post-capture geometry diagnostic also checks
the captured representation, so a later histogram request cannot read a packed
span with the raw stride; skin diagnostics explicitly decline packed commands. The command captures the representation; replay
uses its corresponding vertex program pointer, which remains part of the
existing draw-state cache key. The existing append-only shader-slot lifetime
holds both variants until shutdown. Fragment links still use the original GXP.

Raw and packed upload entries have different layout keys. All three packed
programs share one representation because their preserved 16 bytes are identical.
Source address alone never proves equality. Same-frame rewrites append; shorter
requests validate every preserved prefix; retired-slot reuse compares the actual
destination mirror again. Guest loans end only after preparation finishes.
Deferred GPU copies read the owned mirror, and reset still joins the last CPU
ticket after the caller obtains the retired GPU slot. The dirty-envelope,
padding, final retirement and queue ownership protocols are unchanged.

`[vertex-packed]` reports requested source/payload volume, including reuse; these
are not GPU writes. Existing transfer counters continue reporting queued/caller
GPU writes. The optional vertex-work profiler includes packed comparisons.

## Qualification

Private receipts are in `E/packed-vertex-layout-prototype`, where `E` is the
direct-cluster-query validation directory. No game bytes were added to source.

* Production allocator and real pthread-backed upload/preparation workers:
  source and unused-tail mutations, unaligned inputs, raw/packed cache separation,
  shorter reuse, delayed copies/preparation, clean trailing spans retaining ticket
  zero after wrap, three-slot reuse, allocation/dispatch failures, mixed batches,
  malformed descriptors and retained fetched-byte checks pass. Normal and
  ASan/UBSan builds pass.
* Production shader loader with owned embedded programs and stubbed SDK metadata
  lookup/patcher: exact admission, program/declaration/binding declines, failed
  alternate creation, binding-cache transitions and both-program release pass.
  This is not real GPU shader execution.
* The complete stream-capture loop extracted from production passes ASan/UBSan:
  physical alias resolution, base vertex, second stream, immediate/trace/stride
  changes, sparse decline, six retained slot generations and actual fetched bytes.
* 420 executions of the actual Vita-compiled uploader pass exact snapshot and
  memory-bound checks. Counts include entry, controls, lookup, comparison, packing,
  metadata and dispatch admission. SDK copy/fill work is modeled and its byte
  volume is reported separately; copy-worker execution and GPU time are excluded.
* Four changed runtime units compile with VitaSDK in OFF/ON configurations.
  OFF `.text` matches the parent exactly for upload, shader and D3D; preparation
  differs only in six assertion-source-line immediates, with identical instructions
  otherwise. Real Make builds `0→1→1→0→0` rebuild exactly six runtime ABI owners
  on a transition, none on repeats, and no generated guest units on transitions.
  Invalid C/Make settings fail.

Reproduce with locally owned retained shader data:

```sh
python3 tools/test_packed_vertex.py --stage "$OWNED_STAGE" --output-dir "$OUT/qualification"
"$PRIVATE_PYTHON" tools/test_arm_packed_vertex.py --output-dir "$OUT/arm"
```

## Cost and recommendation

Representative aligned complete-uploader instruction counts, warmed controls,
residency and existing grouped comparisons enabled:

| Source bytes | Retired hit raw → packed | Same-frame hit raw → packed | First-byte miss raw → packed |
|---:|---:|---:|---:|
| 128 | 291 → 252 | 213 → 169 | 304 → 298 |
| 512 | 423 → 333 | 345 → 250 | 304 → 343 |
| 2,048 | 951 → 657 | 873 → 574 | 304 → 523 |
| 4,096 | 1,655 → 1,089 | 1,577 → 1,006 | 304 → 763 |
| 16,384 | 5,879 → 3,681 | 5,801 → 3,598 | 304 → 2,203 |
| 47,520 | 16,595 → 10,256 | 16,517 → 10,173 | 304 → 5,854 |

Raw misses additionally invoke firmware memcpy for the full source span. Its
work is **not represented** by these instruction counts; packed misses execute
the gather loop directly and write half the payload. Thus the last column
identifies a real producer-work tradeoff but cannot establish a hardware
regression or win. Last-prefix mismatches at 47,520 bytes count 16,622 raw plus
47,520 firmware-copy bytes versus 15,862 packed. Small spans are explicitly
included rather than extrapolating large-loop savings. Compiled-ON raw fallbacks
add 3 instructions for tested same-frame hits, 6 for retired hits and 8 for misses.
Capture/selection overhead is outside this uploader-entry count.

The four ARM objects add 1,568 bytes of executable `.text` combined. Uploader
`.bss` grows 24,584 bytes (primarily a four-byte layout field per cache entry),
and D3D shader-slot storage grows 384 bytes. Additional GXM patcher allocation for
three alternate programs is not established by host stubs. The command flag fits
existing padding in the compiled command layout; source/payload pools stay fixed.

This is a usable default-OFF prototype for independent review. Unchanged-span
counts improve materially enough that comparison overhead does not rule it out.
It is not evidence of a frame-time gain: current logs do not weight these three
programs' bytes or hit/miss positions, source cache-line traffic may not halve,
firmware/cache/GPU costs are unmeasured, and graphics execution has not been
tested. Do not attribute the entire approximately 9 ms stream stage to this work.
An eventual reviewed fresh-launch candidate must preserve the existing startup
flags, add `XV_PACKED_VERTEX_LAYOUT=1`, observe actual ready/request logs, and
validate menus, gameplay, retained draws and visual correctness before any keep
decision. No package, device, live benchmark or game emulator was used here.
