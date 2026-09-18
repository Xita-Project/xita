# Original fixed expression callbacks

Native213 reaches an unresolved original indirect target `207C20`, called at `20995B` with return `20995E`. The observed function index is zero. The dispatcher loads `[4744E0 + index*4]`, then calls descriptor field `+4` with three original arguments. Index zero binds record `44B098` to `207C20`; that routine manages the original expression frame and evaluates its linked child expressions. The adapter does not supply an evaluation result.

The private initializer admits the first 25 fixed descriptors, indices 0–24, as one reviewed prefix. Their pointers select the consecutive 16-byte records `44B098..44B218`. The complete 100-byte pointer prefix, 400-byte record span and 24-byte dispatch sequence are independently fingerprinted. There are 14 distinct non-null callback targets, all in the executable title `.text` section. Only field `+4` is discovered as code. Type/flags, help text pointers and parameter metadata remain data. Index three retains its actual null callback; skipping it during discovery does not change the original dispatch or manufacture a return if the game tries to call it.

This is an explicit bounded prefix, not a claim about the full registry's size. The next descriptor is variable in size and remains excluded. Original callback arguments, expression-stack allocation, child order, short-circuit behavior and returns execute as generated guest code. No runtime hook or replacement interpreter is added, and unsupported code retains its normal strict stop.

All 54 callback-root tests pass. The added synthetic fixture rejects each incorrect record binding, absent/nonexecutable/wrong-section target, and each independently changed fingerprint. It includes duplicate callbacks and the real null hole, and omits metadata/neighboring records entirely so any unintended read fails. Regeneration adds 16 functions (14 callbacks plus two directly reached helpers), 203 blocks and 1,767 instructions. All 12,615 previous function bodies are unchanged; only surrounding translation-unit separator whitespace is ignored in that comparison. The image remains identical and the automatic unsupported count remains 3,663. These counts describe generated coverage, not executed or compatible game behavior.

An initial pre-build comparison included separator newlines and rejected the changed translation-unit grouping. Inspection established that all reported differences were surrounding blank lines; the corrected comparison passed before any native build began. The rejected comparison and raw registry investigation remain private. In particular, a speculative larger pointer scan reaches strings that happen to resemble code addresses; it is not used for discovery.

Private audit/build evidence is `script-dispatch214/`. Owned executable data, metadata contents, generated output, traces and packages stay outside Git. Diagnostic packages embed owned code/image data and must not be distributed as releases. Halo CE, physical hardware and the shared Vita3K binary are unchanged. Native214 now executes the original expression callback and advances to an unresolved command at function index `22A`, target `2A9740`, through the same `20995B` dispatch (return `20995E`, ESP `005E1F10`). The original intro is visually verified; frame136 remains RGB-zero and no original main menu is visible. The owned process `3721209` was stopped after archival.

All 180 dependency targets, 130 generated source references and 179 object identities are verified. Seventy generated-code/function-table objects differ from native213; the other 109 objects, including every runtime object, are identical.

| Native214 artifact | SHA-256 |
| --- | --- |
| `halo2-boot.elf` | `c03000e173cc37ee74ecbe37b0948958ea3bed7541c6269dd8f953351c9e3a6f` |
| `eboot.bin` | `b46b2aa9972443f82aaeb0500b53c2c0eb892c6e04bcaed37ecbedeb38f3f7a7` |
| `boot.log` | `03925818ecc18c62acfa81f29447d694b2d3d58c77acf3505de8e93484120e97` |
| `channel-at-stop.json` | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| `last-presented-at-stop.bin` | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact replay from the private directory uses `python3 preserve_fresh_cache.py replay214`, `python3 capture_run.py replay214 native-214-artifacts`, then `python3 drive_startup.py replay214 native-214-artifacts`, sequentially with successful exit checks and no other owned main-lab process active. Frozen evidence is in `native-214-artifacts/`; the visually checked intro is `native-214-view/early-movie-middle.png`.
