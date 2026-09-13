# Halo 2 original dispatch initialization

Native46 dispatches object `54D62C` through vtable `4599A8` and stops at its
missing first method, `235486`, from `147736` in `1476C7`. The loop iterates
five times over objects at `54D62C` (stride `40h`), `54D76C` and `54D884`
(stride `38h`). This caller is entry index 50 in the 68-entry game initializer
sequence. Its index does not measure total game/menu readiness.

The original constructor `23546B` calls its base constructor `234E43` and then
writes `4599A8` to the object. The base constructor independently assigns
`4599DC`, bounding the preceding table to 13 dwords. Every entry in
`4599A8..4599DC` points into executable `.text`. The first method clears object
field `38h` and tail-calls the original base initialization `234E64`.

`prepare_boot.py` checks both constructor fingerprints before adding those
13 table entries. It preserves the whole-image revision check, the original
object constructors, all dispatches and method bodies. It does not scan nearby
data or assert the meaning of this object class from its address alone.

| Constructor | Length | SHA-256 |
| --- | --- | --- |
| `23546B` | 27 | `cbd17bebf8667c708be45cbe65a4fc6dfc672448edf8c83151e4173d16e69e71` |
| `234E43` | 33 | `84924bde2768f01fd262d3d0cd0916038e22c201d8200008e05c9cdff85bd95f` |

The synthetic callback test checks the exact table bounds, the last slot's
validation and changed constructor rejection. The previous descriptor tests
remain in place. These discovery roots do not implement graphics or audio.

## Native 47 result

The original object initializer runs and startup advances through five further
allocations. The next stop is callback `81780`, called at `18EF8B` in `18EF00`,
return `18EF8D`. Its caller traverses eight callback pairs at `453C00..453C40`;
this next table has not yet been added by this milestone. No unsupported draw
has been reached. Device/push snapshots match native45/46 and the viewed actual
`native-47-view/window-2.5.png` is black, with no menu or geometry.

Private evidence: `native-47-artifacts`, `native-milestone-47.json`,
`native47-next-boundary.txt`, `native46-vtable-audit.txt`. Frozen generation is
`dispatch-vtable/generated`. All nine callback-root tests pass. The build
reduced its generated unit count from 128 to 127; a stale private unit was
removed and the final successful link uses precisely the new generated tree.
The first failed build log and corrected final log are retained privately.
No Halo 2 emulator remains running after capture.

| Native 47 artifact | SHA-256 |
| --- | --- |
| ELF | `9216b49534e8c42519de28fd2f989c95fe2c2633ad92c34127344fa238089fdf` |
| EBOOT | `a6b824c48bf75087346d88a82843f88ddeed13fb7d3e892389ee664037fb7f0d` |
| VPK | `f8a84afa3137d6bdc533e339e290b81faba565570b53f1a03ef7db2a77999b14` |
| Boot trace | `2fd0513b9b148dfbeaea5942c3035653598bd23544f4a85e9abe4d3863596ccd` |

Exact archived replay with a fresh label:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay47-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-47-artifacts/halo2-boot.vpk
```

This package embeds owned game code and image data; keep it private and do not
upload it as a distributable release. The run uses the explicit unavailable-audio
diagnostic, not the strict normal profile and not a working audio backend.
