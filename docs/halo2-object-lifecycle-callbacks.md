# Halo 2 earlier object callback walks

Native157 stops at `BE760`, called by `108B80` at `108BCA`, return
`108BCC`. The original caller resolves the object handle, selects its type's
catalog parent and walks the parent's direct child list, invoking field
`38h` when nonnull. The target performs further original object creation
and attachment work. This stop is not established as a cleanup or title-exit
path.

The earlier walks use the same bounded descriptor structure as
[the first object callback group](halo2-object-descriptor-callbacks.md).
Their complete caller fingerprints establish eleven additional fields:

| Original walk | Child field |
| --- | --- |
| `108B10` | `30h` |
| `108B80` | `38h` |
| `108BF0` | `3Ch` |
| `108C60` | `40h` |
| `108CD0` | `44h` |
| `108D30` | `48h` |
| `108D90` | `4Ch` |
| `108E10` | `50h` |
| `108E80` | `54h` |
| `108EF0` | `58h` |
| `108F60` | `5Ch` |

Each fingerprint now carries its corresponding field offset. The rule roots
only nonnull executable targets from the original direct child lists; it
retains the existing descriptor span, overlap and terminator checks.
Fields `34h`, `60h` and `78h` remain unselected. This is not a contiguous
pointer scan or a replacement dispatcher.

The original `108D30` and `108D90` walks stop when a callback returns true.
`108D90` has a second return path that clears outputs on failure; its
fingerprint includes both paths. All arguments, list order, short-circuit
behavior and output writes remain in the original translated code. The
previous `64h` walk's AL accumulation also remains unchanged. No callback
result, sound behavior, graphics behavior or instruction implementation is
changed here.

All 43 focused callback, sparse-jump, profile and LOOP tests pass. The
synthetic object fixture covers all 16 selected fields and caller guards,
null and invalid targets, ignored adjacent fields, parent-versus-child
selection, trailing data, non-recursion, terminator bounds and unchanged
image bytes. Regeneration adds 226 reachable functions, 4,600 blocks and
32,021 instructions; unsupported instruction count remains 3,825. Generated
coverage does not establish runtime or menu compatibility.

Owned listings and extracted field evidence are private in
`object-callbacks/earlier-walks.txt`, `earlier-walks.json` and
`earlier-walk-shapes.json`. Native158 uses private `object-lifecycle`
output. Its diagnostic package embeds owned game code/data and must not be
distributed.

Native158 executes these callbacks and reaches target `E8980` from `E6900`,
return `E6919`. The original Microsoft Game Studios intro is directly visible
in private `native-158-view/early-movie-middle.png`, SHA-256
`5a0bc32bb88da095c29b49bd4e91fcc235410a65aef561313313857f8cd53be8`.
Final frame135 remains black and the decoded channel state is unchanged;
there is still no original main menu.

The completed four-job build passes all171 dependency-target checks. ELF
SHA-256 is `f225c9cd5fc08e84771461a142f64af33f75811d5db3f5faad9333f12c1768c0`,
EBOOT `29cf3c30ef7954569e00282baeaceb29d3116e8d23d35bb45a001a1a3b234f1c`,
trace `3eddf83aa6636b0557653c184182a6e77a2d5464cc81c6dad48a449ce9eeb5ee`.
The private checkpoint is `native-158-artifacts`, `native-milestone-158.json`
and `native-158-view`. Replay from the private directory with
`python3 capture_run.py 158-replay native-158-artifacts`, then
`python3 drive_startup.py 158-replay native-158-artifacts`, after preserving
and resetting the private cache with
`python3 preserve_fresh_cache.py native158-replay` while the owned emulator
is stopped. The original map-copy and normal Start path are retained.
The native build and owned emulator are stopped after capture.
