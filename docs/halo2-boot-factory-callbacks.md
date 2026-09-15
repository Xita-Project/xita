# Halo 2 startup factory and object lifetime callbacks

Native 163 stops at `2D76F0` through factory vtable `411D2C`, called by
`2D7570` at `2D75AF`, return `2D75B2`. The complete initializer `2D8780`
allocates that factory and installs it through `2D74D0` in `480180`.
Constructor `2D7570` calls its slot `4h` twice, with arguments 1 and 2.
The original method allocates an eight-byte object, initializes its size
and reference count, assigns vtable `411D3C`, and returns with `ret 4`.
It does not inspect the argument. This behavior remains original code.

Original helper `2D9090` stores each child and increments its reference
count. The constructor then releases its temporary references. The complete
constructor guard includes both returns and the destruction branch after
the first return. No reference count, null handling or destructor flag is
supplied by a host replacement.

Discovery also includes the paired deletion callbacks proven by the three
original owner setters (`2D73F0`, `2D7460`, `2D74D0`) and child release helper
`2D8E70`. These setters retain their special-object check, reference count
updates, possible destruction and final owner pointer write.

| Vtable slot | Original target | Proven use |
| --- | --- | --- |
| `411D20` | `2D7670` | Composite owner deletion |
| `411D2C` | `2DA270` | Factory deletion |
| `411D30` | `2D76F0` | Child creation |
| `411D3C` | `2D7720` | Child deletion |
| `411DA4` | `2D84E0` | Second startup owner deletion |
| `4537A0` | `147320` | Static allocator free selector |
| `4576B8` | `22C320` | Arena free wrapper |
| `4576B0` | `22C280` | Original arena allocation-record removal |

The deletion methods conditionally forward to allocator offset `14h` with
original object size and allocation tag. Selector `147320` retains its
pointer-range routing to the correct allocator. Only the observed arena's
free wrapper and record-removal method are added here; other allocator
implementations still require supported dispatch. Arena metadata compaction
runs in original code, including the unchanged missing-record result.

Fourteen complete caller/body fingerprints plus the three existing arena
constructor/forwarder guards limit this extraction to the owned revision.
Every selected target must be nonnull executable `.text`; adjacent vtable
slots are not scanned. All 54 focused callback, profile, LOOP and sparse-jump
tests pass, including every guard, invalid targets, ignored neighbors and
unchanged synthetic input. This changes only Halo 2 discovery roots. Regeneration adds 13 reachable
functions, 45 blocks and 340 instructions; the unsupported instruction count
remains 3,825. These are translation counts, not executed coverage.

Private audit: `arena-singletons/factory-callback-guards.json`,
`factory-callback-full.txt`, `factory-destruction-readonly.txt` and
`allocator-lifetime-readonly.txt`. Private build stage: `boot-factory`.
The diagnostic package embeds owned game content and must not be distributed.

Native 164 builds successfully and verifies all 171 dependency targets.
The original factory stage now runs; the next checked stop is unsupported
`MINPS` at `2E1E1A` in original function `2E1D10`. This is an instruction
implementation boundary, not a missing factory callback. The trace reports
EAX `80000000`, ECX `17`, ESP `005E5AE0`.

The original Microsoft Game Studios intro is visibly present in
`native-164-view/early-movie-middle.png`, SHA-256
`57cd7014b7f3fa95aad1bc975201819c53592b9b9c70ac87726833d367b40463`.
The original map copy completes before normal Start. The transition clip
lasts 43.6 seconds, ending with the controlled emulator stop. Final frame
135 remains black; decoded channel and last-presented hashes are unchanged
from native 163. No original main menu is visible.

| Native 164 artifact | SHA-256 |
| --- | --- |
| ELF | `36eb279c162235744a028ac1bab002560ee471fdeb73f608f8b7f952d0acd035` |
| EBOOT | `3f45cf834fb357b4bf6dbd4fe1187165233eb867a182980194b162fb22f94c15` |
| Boot trace | `1fff820a8064e483654c8073558b47edd16ecf6526472fd23df700f97107bd01` |

Private replay from the private base, with only this lab stopped:
`python3 preserve_fresh_cache.py native164-replay`, then
`python3 capture_run.py 164-replay native-164-artifacts`, then
`python3 drive_startup.py 164-replay native-164-artifacts`. Archived artifacts
contain owned game content and must not be distributed.
