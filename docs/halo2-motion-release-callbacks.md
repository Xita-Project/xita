# Original motion type and lifetime callbacks

Native218 executes both original type queries in rigid-body destructor `311410`, then reaches the nested deleting destructor `31A710`, called at `311488` with flag1. The observed vtable is `4151B0` and its original type getter returns7. Cleanup still runs in guest code; no type result or successful deletion is supplied by a host replacement.

Original rigid-body constructor `311BA0` selects its member at `+3C` through factory `311940` or the explicit type7 constructor. The factory's bounded switch, fallback and subsequent original virtual calls are fingerprinted as a complete span, including its six-entry jump table. Its six concrete constructors already have individual revision guards. They install tables `4143A8`, `414428`, `415130`, `4151B0`, `415230` and `4152B0`.

The original destructor queries slot `18h` twice, keeps its type6/type7 child cleanup, then invokes slot0 with the deleting flag. Only those two slots from the six verified tables become roots here. The first 28 bytes of each table are fingerprinted; the other method fields do not become roots. The six original getters return2,3,6,7,5,4 respectively; the two distinct deleting wrappers retain their original destructor call, stored size, allocator category and `ret 4`. This bounds discovery without claiming every motion type has executed successfully.

All 59 callback-root tests pass. Synthetic fixtures omit every unselected method and neighboring table, include duplicate targets, and reject each invalid target, wrong section, all nine caller/table fingerprints and each of the six constructor fingerprints. Input metadata remains unchanged. Private proof and build evidence is `motion-release219/`, with the preceding complete owned disassembly in `object-release218/nested-motion-candidate.txt` and `nested-release-audit.txt`.

Generation adds seven reachable functions and 45 instructions, totaling 13,580 functions, 177,818 blocks and 1,240,868 instructions. All prior 13,573 function bodies are unchanged apart from surrounding separator whitespace. Unsupported instructions remain 3,663 and referenced kernel exports remain 106; these are automatic coverage metrics, not executed compatibility.

The completed native build verifies all 179 dependency targets, 129 generated C source paths and 178 objects. Only 111 generated-code/function-table objects differ from native218; all 50 runtime/support objects remain byte-identical.

Game bytes, generated code, traces, captures and packages remain private and outside Git. Diagnostic packages embed owned game material and must not be distributed. No Halo CE, physical hardware, audio/graphics behavior or shared emulator changes are included. Native219 completes the original motion deletion and advances into the base rigid-body destructor. The next strict stop is associated-object deleting callback `1D19D0` from `30BFB4` in `30BF50`, table `455950`, ECX `014F09E0`, ESP `005E1DC4`. The original Microsoft Game Studios intro is visually verified in `native-219-view/early-movie-middle.png`; last frame136 and the channel remain unchanged, RGB-zero, with no original main menu. The retained timer callback has no subsequent dispatch before this unrelated main-thread stop. Owned PID `3779521` was stopped after capture.

| Native219 artifact | SHA-256 |
| --- | --- |
| `halo2-boot.elf` | `58929032d2a3d6de149c8d429ab899d88e9269ab51030f927564016151abab8b` |
| `eboot.bin` | `aafd95cf07c15a1ddb9e9057203a7eacc279e9d8e293eb882bcecd16b3f8d8f7` |
| `boot.log` | `154e3bf41d2b81a5fec857c2a4f7956e4c26c45631ced94c748d28ba730574ac` |
| `channel-at-stop.json` | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| `last-presented-at-stop.bin` | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact frozen replay from the private directory uses `python3 preserve_fresh_cache.py replay219`, `python3 capture_run.py replay219 native-219-artifacts`, then `python3 drive_startup.py replay219 native-219-artifacts`, sequentially with successful exit checks and no other owned main-lab process. The next task is auditing the associated-object release interface.
