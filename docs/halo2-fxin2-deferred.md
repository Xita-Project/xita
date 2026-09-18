# Halo 2: deferred spatial parameter storage

Native123 executes the original deferred nine-word parameter update on spatial
FX25, then stops at a filter request on nonspatial FX23. Native124 preserves
that stop and adds a read-only capture of its complete descriptor. **The game
still presents a black frame with zero nonzero audio grains. No main menu or
gameplay is demonstrated.**

Public `37C6E5`, called from `2AEF6A`, takes the interface, a 36-byte parameter
block and deferred flag1. Original `37C0E9` copies raw words into the spatial
block at offsets80..A0 and ORs byte7E with7F. With flag1 it returns without
updating the active voice or issuing MMIO. Its size matches
[Cxbx's DSI3DL2BUFFER layout](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/DSOUND/XbDSoundTypes.h).

The adapter checks the exact caller, owned active spatial25 object, configured
route state, flag and mapped input before copying. The active mixer/filter is
unchanged. Immediate application and later spatial commit remain unsupported;
this stores pending parameters rather than fabricating active effects support.
The public wrapper has a revision-checked fingerprint, and unknown sound
methods retain their original strict guard.

A private audit executes 2,764 unique original instruction addresses including
construction, Play, route configuration and this setter. It verifies the entire
spatial block against the expected copy/dirty update and observes no MMIO from
the deferred call. Public wrapper32-byte SHA-256 is
`ebc647fb92ed1dad3a4e54a336bf325642e44338cb6767ef2e6f6be852b8a2ca`;
the 202-byte implementation hash is
`171bbc1cd970de1374ac0896bc24cb72b7c033b8bb7a344f531abb691b9a8e96`.
The owned input at470060 has SHA-256
`46f2a7b846bcb343249ec477969f560ef6459e4afcfdca9302f057b368c71edf`.

All 40 host executables, extra modes and 27 Python tests pass, together with
the private owned-coefficient ABI test and its ASan/UBSan run. Synthetic input
uses distinct raw words, signed zero and NaN patterns to check bit-preserving
storage, full ABI state, unchanged input and rejection without mutation.
Regeneration changes only `code_106.c`; 131 other generated files are verified
byte-identical to the prior source before reusing their timestamps for building.
These automatic generation counts do not represent additional runtime coverage.

Native124 stops at `37B68B`, return `2AEFBC`, ESP `005E5ED4`, interface
`0128601C`, descriptor `005E5EE8`. Its six words are
`{1,0,0,8000,0,0}`: a mode1 filter, Q selector0 and four coefficient words.
Original `381710` and the pinned reference's active low-pass behavior require
a real filter implementation before accepting this call. The current code
only logs the mapped descriptor at the terminal stop.

Pre-drain counters are 15,360 computed/submitted frames and 14,336 consumed,
all seven sources active, 916,273 microseconds total computation and 72,116
maximum per grain. Nonzero grains, peak and error are zero. Close and all
terminal snapshots succeed. Computation remains slower than real time.

| Artifact | Native123 SHA-256 | Native124 SHA-256 |
|---|---|---|
| ELF | `c22b034fc9cd8c04e2efc26cc1401d737600f90b481c7a3fbc40d0b7fff86fcf` | `8f012aacc6b9b2799cc6cf4f7f0231f690e326d067962b1cff9bb58f32b259cb` |
| EBOOT | `b4d14349583636cce0224f48b99b271f8ccf64dff71317c36a89ba05dfb77840` | `fd8e65b5b5d60a6dca33bd38eef0f18a5b3d201747405958bf1a7280bf797d58` |
| VPK | `1608661d169d9aa72f816a14985338153cfbfe72dbcd3ee2b6fd02d9b50f11e4` | `b1ef880529bd9a2f137b1ba60177b1076b4cfb50ccf1085ca577ce8f967d6ee1` |
| Guest trace | `9c092432fbaa7b2fdfa8000e812fb8199662cfd7508d2a6a87425292b3e0748e` | `6198610bd5c52cf8008caff3811376786b85176fd6a848f0368c7ae2f757fd10` |

Both black scanouts retain SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
Private evidence is under `native-123-artifacts`, `native-124-artifacts`, their
views/manifests, `audio-fx-deferred`, `audio-fx-filter-trace`,
`audio-host/fxin2-spatial25-deferred-original.json` and `dsp-bringup/fx-deferred*`.
From the private directory:

```sh
python3 run_lab.py 124-replay native-124-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

The build uses the previous DSP/spatial options, `audio-fx-deferred/generated`
and image, and `audio-fx-filter-trace/build`. **Diagnostic packages embed owned
game content and must not be uploaded or distributed.**
