# Original menu text-token callbacks

Native197 consumes the first complete supported graphics batch and then reaches original text substitution in `22D2EE`. Its indirect call at `22D44C` selects `22D82C` for token `E415`. The owned record names that token `build_number`; the callback's complete original body is three bytes, `RET 8`. The caller clears its temporary UTF-16 output before dispatch and performs the string replacement afterward. Discovering and executing this original callback preserves that behavior; no substitute text or success-return API is introduced.

The caller first limits incoming tokens to `E000..F8FF`. Its search increments a byte offset by 12 until `0x624`, proving 131 records at `470200..470824`. The record fields are name pointer, token and optional callback. The callback load at `22D42E` addresses only the third field and checks for null before calling with the token and output pointer. Two whole-span caller fingerprints gate this H2-only extraction; the existing full-XBE revision gate remains in force. Other record fields and adjacent tables are never scanned as code.

The owned table has 86 nonnull callback slots, 43 distinct title-code entries and no duplicate token keys. Those entries add 59 functions through ordinary direct-call discovery, with no increase in unsupported-instruction count. The prior 12,549 functions become 12,608 automatically translated functions; this is coverage, not proof that these callbacks or the game are fully supported. One existing function, `2B7460`, keeps its original jump to `13FC90` as a generated tail call to the newly separate function. The lifted callee body is identical to the previously inlined body. Other existing function bodies are unchanged.

All 51 callback-root tests pass. The new synthetic case checks the exact 131-record extent, field selection, both input-range endpoints, out-of-range exclusion, null preservation, deduplication, invalid/incomplete targets, wrong sections, missing sections, and both caller fingerprints. The private source audit and generated-body comparison are in `private/text-token-callbacks`.

Native198 passes the original token callback and reaches the caller's overlapping string copy in `320890`. The next strict stop is the unresolved internal alignment-table destination `32092C` selected by its jump at `3208DD` with EAX=2, ECX=42. No memory-copy or callback replacement was introduced. The six original graphics rectangles still complete; GET=PUT remains `03B80158` before the next incomplete batch is submitted.

The original Microsoft intro is visually confirmed. The terminal screenshot and complete last-presented frame135 remain black: **the original main menu is absent**. The 70.3-second transition recording is private evidence, not a new menu result. The owned :111 process was stopped after capture. The next task is the original copy routine's bounded alignment branch table.

Private evidence: `native-198-artifacts`, `native-198-view`, `native-milestone-198.json` and `text-token-callbacks`. All 180 dependency targets and 128 generated dependency source paths were verified. Graphics/dispatcher/sprite/audio objects and the owned image are byte-identical to native197. ELF SHA-256 is `087bbf050e886c4762042f481236879560640c875ff29e482cea82e65ebf3a15`; EBOOT is `7a794ad4af2669047b0becd1593a22d5957aef618a8cc6c17483dcd9ed5a88c7`. Trace SHA-256 is `d6a2c9dd792a422963622682f01706e44d708fcdcdea64108291e30bab52eebf`, channel snapshot `db5d4f757594407c3ce2a02fe64717b5326646a5efca143a9b871307d552fd33`, and last-presented image `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.

Exact private replay, with the owned H2 emulator stopped:

```sh
python3 preserve_fresh_cache.py native198-replay
python3 capture_run.py 198-replay native-198-artifacts
python3 drive_startup.py 198-replay native-198-artifacts
```

These diagnostic packages contain owned game image/code and must not be distributed as releases. Generated callback coverage does not establish a visual milestone.
