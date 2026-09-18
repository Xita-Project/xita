# Original startup script commands

Native214 executes the initial expression evaluator and reaches script function index `22A`. The unchanged dispatcher at `20995B` selects `[474D88] = 44DB50`, then callback `[44DB54] = 2A9740`. The callback has zero explicit script parameters, but still receives the original three-argument evaluator ABI and returns with `ret 12`. Its first call, `13BFF0`, changes original startup flags through actual game globals; it then completes the expression using `209AE0`. No host routine supplies those state changes or a fabricated evaluation result.

The next bounded discovery group is the four consecutive descriptors at indices `22A..22D`, records `44DB50..44DB80`. Their type/flags/help/parameter metadata is the same fixed zero-script-parameter shape. Their original callbacks are `2A9740`, `2A9760`, `2A9780` and `2A97A0`: the first two call original paired state routines `13BFF0`/`13CA80`; the latter two set/clear the same original state byte before completing their expressions. The following descriptor has a script input and is excluded. The common dispatch, entire 16-byte pointer group and 64-byte descriptor group are independently fingerprinted; every record binding and required `.text` target is checked.

Only discovery changes. Runtime graphics/audio behavior, interpreter state, guest arguments and original return paths remain unchanged. All 55 callback-root tests pass, including omitted metadata and neighboring records, each bad record binding/target/section and each changed fingerprint. Owned game bytes, generated code and diagnostic packages remain private and outside Git. Packages embed owned code/image data and must not be uploaded as distributable releases. No Halo CE, shared emulator or physical hardware changes are included.

Private audit/build evidence is `script-command215/`. Native215 executes the original startup command and advances to script index `1B`, unresolved callback `2AB6C0` at the same dispatcher `20995E`/`209850`, ESP `005E1F10`. The original Microsoft Game Studios intro is visually verified, last frame136 remains RGB-zero and no original main menu appears. The owned process `3732244` was stopped after capture.

Generation adds four callbacks and three directly reached original helpers (`F8190`, `13BFF0`, `13CA80`), totaling seven functions and 126 instructions. All previous 12,631 function bodies remain identical, and the unsupported count stays 3,663. An initial pre-build assertion expected only the four wrappers; it rejected the extra direct helpers before any build began, then passed with the verified full seven-function set. All 180 dependency targets, 130 generated source references and 179 object identities are checked. Only 101 generated-code/function-table objects differ; all runtime objects remain identical to native214.

| Native215 artifact | SHA-256 |
| --- | --- |
| `halo2-boot.elf` | `f38ccb37485e74b7bed31df99b77e84dee824ed5212490dbf03e941833a5e894` |
| `eboot.bin` | `059ce889070a6ffe109bd2f46ee77697f93d08fd17de312472a0809aad834f19` |
| `boot.log` | `74a11ed52de5da7ab61912803fedbde24bf0302549b257f31ed06419b9557295` |
| `channel-at-stop.json` | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| `last-presented-at-stop.bin` | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact replay from the private directory uses `python3 preserve_fresh_cache.py replay215`, `python3 capture_run.py replay215 native-215-artifacts`, then `python3 drive_startup.py replay215 native-215-artifacts`, sequentially with successful exit checks and no other owned main-lab process. Evidence is frozen in `native-215-artifacts/`; the checked intro is `native-215-view/early-movie-middle.png`.
