# Material dataflow prototype, September 17

The private `705CC → 707D8` experiment preserves the original `70110`
control flow and child calls. It does not establish a substantial performance
gain and remains unintegrated. No hardware build or settings changed.

The earlier whole-material shadow-context fusion added instructions, memory
operations and stack. Keeping only GPRs local removed its 4,320 copied bytes
per call but still added work. The follow-up therefore changes two small x87
dataflow spans and seven comparisons inside the original material prefix.

Named doubles retain the original precision, float spills, stale x87 slots,
TOP, flags and guest-memory order. The original RNG, waveform, vector,
shader, constant, combiner and draw calls remain. The selector defaults OFF.
Fast admission requires PC53, normalized TOP, masked native FP exceptions,
and separation between the native context and guest arenas. Admission is
rechecked after original child calls; a decline runs the original span at
that continuation without repeating earlier writes.

Actual retained ARM objects supplied the reference and arithmetic children.
Whole-call observations still use controlled shader/draw/HLE ABI sinks;
these tests do not prove actual rendering publication or hardware speed.

| Path | Instruction change | Load change | Store change |
| --- | ---: | ---: | ---: |
| Common material variants | +26 | +11 | −16 |
| Channel multiplication | −4 | +8 | −27 |
| Full comparison chain | +7 | +22 | −22 |

Isolated native stack peak was 288 bytes originally and 272 bytes with the
dataflow rewrite. Both paths copied zero context bytes. Instructions are
not cycles or FPS; fewer stores and slightly more loads form a small mixed
tradeoff, not evidence for a new hardware build.

Validation passed 24 full-call cost comparisons and 32 targeted comparisons.
The latter checked all context and arena bytes, FPSCR, original pointer
identity, mapping roots/tables and ordered guest writes. Cases included
physical vector/stack aliases, page-crossing vectors, qNaN/sNaN, unsupported
PC64 fallback and a child-entry mutation that replaces the global page table
and changes FCW. Two TOP values and two native FP control combinations were
used. Actual active CRT waveform yields, native trap delivery, real draw
publication and complete caller continuations remain unqualified.

The useful finding is the boundary: exact observation points and guest
aliases retain most scalar loads and float spills. This particular prefix
has little remaining bookkeeping to remove once those constraints are
preserved. Later material/UV/draw regions were not expanded into this task.

Private receipts and reproducible drivers are saved under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/`
`native-material-dataflow/`. The prior layouts remain separately preserved
under `native-material-builder/` and its `gpr-only/` subdirectory. Owned
generated guest sources and test images remain private.
