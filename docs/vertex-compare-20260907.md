# Exact NEON vertex comparison candidate — September 7, 2026

The [hardware residency test](hardware-20260907-vertex-resident.md) avoided writes
but raised stream preparation from approximately 7.1 to 9.9 ms/frame. Residency
is now disabled by default. This candidate instead accelerates the existing exact
comparisons used to protect immutable vertex snapshots. Physical Vita performance
and the previously reported driving crash remain unverified for this candidate.

## Implementation

`xv_bytes_equal.h` compares cached bytes in 64-byte groups using ARM NEON loads,
XOR and OR, reducing to a nonzero/zero result. Loads remain within the requested
span; 16-byte groups and scalar tails handle the remainder. The implementation
does not require aligned inputs, hash data, change floating-point values or mutate
either input. The non-NEON host implementation retains exact `memcmp` equality.
The intrinsics are defined in the [Arm reference](https://arm-software.github.io/acle/neon_intrinsics/advsimd.html).

`xv_vertex_upload.c` uses this helper for comparisons of at least 64 bytes. Small
spans keep the existing libc comparison. `XV_VERTEX_COMPARE_NEON=0` disables the
helper; the candidate default is enabled. Upload residency stays disabled unless
explicitly enabled with `XV_VERTEX_RESIDENT=1`. There are no extra allocations,
GPU waits or changes to per-slot ownership, copies, indices, shaders or transforms.

The comparison benchmark now toggles **scalar / NEON / scalar** only. Its production
hook is tested to leave the residency override and queue policy unchanged. Both
completion and cancellation restore configured defaults. Standard settings still
use single-flight submission and disable cutout, stale-visibility and half-precision
experiments.

## Verification

`tools/test_arm_bytes_equal.py` compiles the actual header with the Vita compiler
for Cortex-A9/NEON, extracts the current native libc `memcmp` from the installed
candidate's archived ELF, and runs both in Unicorn's Cortex-A9 model. It checks:

- 21,997 input cases and 44,026 native function calls: all alignment pairs around
  vector boundaries, every mismatch position through 129 bytes, larger buffers,
  identical pointers and empty spans.
- Every input read stays within its declared range; only stack writes are allowed.
  Additional buffers end directly before unmapped pages. No bounds fault occurs.
- Exact agreement with the scalar routine. Byte patterns include arbitrary float
  encodings; no arithmetic equivalence or tolerance is substituted for equality.

For equal aligned 4 KiB spans, the wrapper executes 1,935 instructions versus
11,278 for native `memcmp`. These are **instruction counts, not cycles or timings**.
The simulator does not model Vita cache bandwidth or GXM contention.

Host upload tests cover both comparison overrides alongside residency switching,
same-frame mutation, initialized padding and 2,000 mixed slot generations. Host
and ASan/UBSan tests pass. Production frame acquisition/completion, benchmark
restoration and Vita input tests pass. Native build and decoded SELF equivalence
checks pass; generated guest code remains the preserved staging baseline.

Private Vita3K renders the menu, Blood Gulch, camera movement, firing, flashlight,
campaign opening and first-person cryo bay after skipping. Ten geometry captures
report zero changed draws before completion. There are no normal Finish calls,
fence errors, upload shortages or storage drops. Maximum slot usage is
1,576/8,192 KiB and maximum pending submission count is one. The private instance
was stopped and its previous executable/configuration restored.

The capped Blood Gulch comparison is **19.779 / 19.787 / 19.783 FPS**, with matching
views and restoration. Selected static wall and sky rectangles are pixel-identical
off/on/off. The sampled ground rectangle changes in both later captures, so this
does not establish whole-frame pixel identity. Upload counts and residency stay
fixed across the comparison.

Emulator stream preparation **increases**, approximately 0.58 to 0.73 ms/frame,
despite the lower ARM instruction count. NEON translation and memory behavior
differ from physical Cortex-A9 execution. This is a correctness check and an
unmeasured hardware candidate, not a demonstrated FPS improvement.

Reproduce the native comparison with Python packages `unicorn` and `pyelftools`:

```sh
python3 tools/test_arm_bytes_equal.py \
  --baseline-elf /home/birchwoodgod/xita-backups/2026-09-07-181523-vertex-resident/xita.elf \
  --output-dir /tmp/xita-native-equality-check
```

## Delivery and next measurement

Archive: `/home/birchwoodgod/xita-backups/2026-09-07-183926-vertex-compare/`.
Candidate executable SHA-256:
`4663b0253e116f697fda40df3437d5b05c6510915fd6d958933de1c4a39808d1`.
The padded executable retains the current 30,891,526-byte allocation. Installed
over USB at 19:00 CDT; direct read-back and a fresh read-only mount at 19:05 verify
the executable and all 1,654 other checked files. Standard settings remain
unchanged, and USB is safely unmounted. See `deployment.json` for the verification
record. Hardware performance and driving stability remain unmeasured.

In a stationary first-person Blood Gulch view, press **L + R + Square**, release,
and keep the camera still until the overlay disappears. Each phase settles 60
frames and measures 120; allow about 90 seconds at the current hardware speed.
Keep standard graphics settings. This is a new comparison, so the preceding
residency result cannot answer it.

Compare measured throughput, stream-preparation time and copy/comparison traffic
on Vita. If the comparator is slower there too, disable it rather than treating
the lower instruction count as success. Driving still needs a separate short
route: the user explicitly confirmed no Warthog driving in the preceding run.
The next larger dependency to inspect is the early guest flare-result read and
whether its exact result can be consumed later without changing observable frame
behavior. Material shader cost remains a separate GPU target. Stable 20 FPS on
real hardware has not been established.

Hardware follow-up: the [September 7 comparison](hardware-20260907-vertex-compare.md)
measures stream preparation at 6.755 / 5.599 / 6.704 ms and whole-game throughput
at 7.003 / 7.241 / 7.145 FPS. The local CPU gain is small in the full frame;
next test the exact deferred-query scheduling change. No repeat comparator
benchmark is needed for this result.
