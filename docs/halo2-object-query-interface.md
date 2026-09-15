# Original object query interface

Native216 passes the initial script callbacks and stops at original function `2E29B0`, called from `30E19A` in `30E140`. The caller loads member `+C4` into ECX, obtains the object's vtable, passes two original pointers and selects slot `+C`. In the owned executable, the sole aligned pointer to this target is `41260C` in the 16-entry table `412600..412640`. Original constructor `2E1D42` and destructor `2E500E` both assign that table. Constants precede it and text data follows it; neither becomes code roots.

The constructor binding, destructor binding, observed dispatch and complete table are independently fingerprinted. Discovery checks all 16 non-null callbacks are executable title `.text` and never reads neighboring data. No API replacement supplies query results: original guest traversal, allocations, bitsets, arguments and cleanup execute normally. All existing unsupported runtime and rendering checks remain active.

All 57 callback-root tests pass. The synthetic interface covers duplicates, each invalid/null callback, wrong code sections and each fingerprint independently. Neighboring constants and strings are omitted so an accidental wider walk fails. Private audit/build evidence is `object-query217/`. Game bytes, generated code, traces, captures and packages stay outside Git; diagnostic packages embed owned material and must not be uploaded as distributable releases. Halo CE, hardware and the shared emulator remain unchanged.

Generation adds 46 reachable functions, totaling 13,563 functions, 177,749 blocks and 1,240,415 instructions. All prior 13,517 bodies are exact after ignoring only surrounding separator whitespace. The unsupported instruction count remains 3,663 and kernel references remain 106. These are automatic translation metrics, not executed coverage.

The completed native build checks all 180 dependency targets, 130 generated C source references and 179 objects. Only 43 generated-code/function-table objects differ from native216; all runtime/support objects remain byte-identical. The auxiliary post-build path checker initially mistook a Make `-MP` header target ending in a colon for a source filename. Its corrected check validates header paths after removing that delimiter and counts only C sources. The failed check was preserved; no emulator launched until the complete checks passed.

Native217 executes the original query and advances to unresolved callback `311570`, called from `1D0323` in `1D01C0`, ESP `005E1E08`, EAX `00414310`, ECX `014F0480`. The original Microsoft Game Studios intro is visually verified in `native-217-view/early-movie-middle.png`. Last frame136 remains RGB-zero with the unchanged channel snapshot; no original main menu appears. The retained timer callback has no subsequent dispatch before this unrelated main-thread stop. Owned emulator PID `3759975` was stopped after capture.

| Native217 artifact | SHA-256 |
| --- | --- |
| `halo2-boot.elf` | `6839e91e9365eab9851e6ea370d007a42638c432f82a459182e88bd7611b92f5` |
| `eboot.bin` | `5f8221927faf4543a714ae6324ac4a485a73f7281de5c74158067f4e8415b9f2` |
| `boot.log` | `912c1fae4c4db8723cb32b7a415cad21d53efa742427f9a069599449b544cefd` |
| `channel-at-stop.json` | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| `last-presented-at-stop.bin` | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact frozen replay from the private directory uses `python3 preserve_fresh_cache.py replay217`, `python3 capture_run.py replay217 native-217-artifacts`, then `python3 drive_startup.py replay217 native-217-artifacts`, sequentially with successful exit checks and no other owned main-lab process. The next bounded task is auditing the original interface selecting `311570`.
