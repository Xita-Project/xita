# Original associated-object release interface

Native219 completes motion deletion, then stops at `1D19D0` from `30BFB4` in the base rigid-body destructor `30BF50`. The original caller obtains the associated object at member `54h`, decrements its reference count, checks the original protected-object predicate, and invokes slot0 with deleting flag1. The native vtable is `455950`.

Original constructor `1D1820` calls its base initializer and assigns this ten-entry table at `1D1832`. The complete constructor covers both original returns. The table occupies `455950..455978`, followed by a null; adjacent masks, floats and later data are excluded. The complete constructor, deleting wrapper, observed caller and entire table including its null boundary are fingerprinted.

Only discovery changes. The original deleting wrapper keeps the stored high-bit flag check, masked size write, call to `2D7240`, base-vtable transitions, deleting-flag check, allocator category26h and `ret 4`. No host replacement alters ownership, object flags, size or freeing. All ten targets must be non-null executable title `.text`; the null boundary is checked separately.

All 60 callback-root tests pass, including each invalid target, wrong section, all four revision guards and bad null boundaries. Synthetic adjacent data is absent so a wider walk fails. Private proof and build evidence is `associated-release220/`. Owned bytes, generated code, traces, captures and packages remain outside Git. Diagnostic packages embed owned game material and must not be distributed. Halo CE, hardware, audio/graphics runtime and the shared emulator remain unchanged.

Generation adds ten reachable functions and 377 instructions, totaling 13,590 functions, 177,870 blocks and 1,241,245 instructions. All prior 13,580 function bodies are identical apart from surrounding separator whitespace. Unsupported instructions stay at 3,663 and kernel references at 106. These metrics describe automatic translation, not executed coverage. The new total creates a 128th generated code unit, which is compiled freshly rather than reusing an absent prior object.

The completed native build verifies all 180 dependency targets, 130 generated C source paths and 179 objects. Only 105 generated-code/function-table objects differ, including the fresh `code_127.o`; all runtime/support objects remain byte-identical.

Native220 completes the associated-object release and reaches a subsequent original motion constructor. The next strict stop is configuration method `31ADF0`, called from `311B58` in factory `311940`, table `415230`, ECX `014F2380`, ESP `005E1B20`. The original Microsoft Game Studios intro is visually verified in `native-220-view/early-movie-middle.png`. Last frame136 and the channel remain unchanged and RGB-zero; no original main menu appears. The retained timer callback has no following dispatch before this main-thread stop. Owned PID `3789507` was stopped after capture.

| Native220 artifact | SHA-256 |
| --- | --- |
| `halo2-boot.elf` | `ba075ccbfcacbedd5cce680e54ba7f44349bb8a6fd4398a32b365fab8bdcb358` |
| `eboot.bin` | `6e29c1f61ef28296e05ed106e8179843d61df838b9f0944a5b83aa1642fcd1f7` |
| `boot.log` | `ee00c6290ae8da9139843db87d581d80040ff293a9b9f8a1d37f6ab848179633` |
| `channel-at-stop.json` | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| `last-presented-at-stop.bin` | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact frozen replay from the private directory uses `python3 preserve_fresh_cache.py replay220`, `python3 capture_run.py replay220 native-220-artifacts`, then `python3 drive_startup.py replay220 native-220-artifacts`, sequentially with successful exit checks and no other owned main-lab process. The next task is admitting the specific configuration slots used by the already-audited original motion factory and rigid-body constructor.
