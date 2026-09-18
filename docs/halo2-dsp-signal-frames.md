# Halo 2: checked DSP signal frames and FX output

The original `FXIN2` request at [native112](halo2-submix-owner.md) needs a real
effects-buffer source. Original `382BE3` binds 32 mono samples from allocation
`387330 + (input_bin - 11) * 128`. The observed input bin 13 therefore selects
offset `100` within that allocation. `37F43E` confirms 128 bytes for 32 samples
of its 24-bit-in-32-bit format. This allocation is distinct from movie PCM.

The original loader establishes the corresponding GP scratch address:

1. `3848B6` sets up the GP scratch and SGE descriptors. `383C74(204)` leaves
   eight initial GP scratch pages mapped.
2. EP construction `385614` calls `385513`, which registers three additional
   pages through `37E42A` / `383D27`.
3. Only afterward, `384337` calls `37E43E` to append the FX allocation at page
   eleven, GP scratch `B000`.

This is an audit of the owned original loader, not an Xbox hardware trace.
A private instrumented interpreter independently observes the original monitor
export: PC `11C`, node `1E`, control `49E2`, 640 words from DSP X:`1560` to
scratch `B000`. That is exactly twenty 32-sample FX buses corresponding to bins
11 through 30. The circular scratch buffers at `8000` and `8800` serve a
different output path and must not be mistaken for these FX buses.

`h2_dsp_mix_frame` now accepts 32 planar bins of 32 signed-24 samples, validates
every input before any mutation, loads the actual mix bank and executes a real
interpreter frame to the checked halt. Invalid representation leaves the
engine unchanged; execution faults poison it. `h2_dsp_read_fx_frame` reads one
completed FX bus with correct 24-bit sign extension. It rejects other bin
indices, undersized scratch, incomplete/failed engines and concurrent execution
without changing the output. These functions provide no voice ownership,
clock scheduling, sink output or performance guarantee.

Synthetic tests exercise every input bin and edge lane, signed extrema,
the actual MOVEP frame-halt peripheral, the monitor's transfer shape, all twenty
FX output indices, invalid inputs, serialization and poisoned-state rejection.
All 36 host executables and additional channel modes pass, as does the DSP
ASan/UBSan fixture. No owned instruction or asset bytes are in those tests.

The native DSP utility supplies a synthetic `100000` pulse to all 32 samples
of bin 13 for one frame, then zero input for 63 frames. It executes the owned
monitor and effects program throughout; no callback or processing function is
replaced. Host and Vita3K produce identical results:

| Signal probe result | Value |
|---|---|
| Completed signal frames | 64 |
| Nonzero FX13 samples | 2,048 |
| Absolute peak, signed-24 units | 1,802,239 |
| Output FNV-1a, canonical little-endian signed-32 samples | `0047432FB510E5B1` |
| Canonical final DSP register/stack/RAM/scratch fingerprint | `468A61507CB6CF3A` |
| Total instructions / DMA transfers including initialization | 1,575,904 / 6,735 |
| Vita3K signal execution/read/hash time | 117,540 microseconds |

The timing excludes asset loading, initialization, the full state fingerprint
and file writes. It includes frame submission, output reads and output hashing.
At 48 kHz, those 64 frames represent about 42.7 ms of audio, so this measured
interpreter path is approximately 2.75 times slower than real time. This is
emulator evidence, not a physical Vita timing result. Scheduling and throughput
must be addressed when connecting it to the real sink.

| Private native signal-probe artifact | SHA-256 |
|---|---|
| ELF | `593d2c4410e9e537392e98392472fedb8deb87e27b09f2dcb55e7c6a68efc4cd` |
| EBOOT | `420706afb652158532489816dd1c021b8c911d57b6a7debab56c2f22223883d1` |
| VPK | `6db36703f0a1929e7278f5de1628e9f05bed26d35d07301d6e34c689a340f05a` |
| Probe result | `dd0a45caa5865f31c0ef2d1809419c489b9800e54659276aa4e741b4fe4e2584` |

Private evidence is under `../private/dsp-bringup`: `signal-milestone.json`,
`native-signal-probe`, `native-signal-probe-result.txt`, `check-signal.log`,
`fxin-dma.log`, `signal-host-tests.log` and `signal-san.log`. From that private
directory, `python3 dsp-bringup/run_signal_probe.py` replays the exact archived
utility package in the owned `:111` lab. The helper preserves the previous
result and stops the utility afterward. Build the utility using the existing
`dsp-probe` target with `BUILD` and `DSP_ASSET` pointing to private locations.
The package embeds owned DSP code and must not be uploaded or distributed.

The game remains at native112's strict FXIN2 creation boundary. No new game
frame, audible output or main-menu rendering is demonstrated here. Native106
remains the visible Microsoft intro checkpoint. The next bounded task is
connecting this verified FX source to the original FXIN2 ownership/routing/Play
contract and a real sink, with explicit underrun/progress behavior and without
accepting unsupported spatial processing or stream packets.

The subsequent [native113 FXIN2 sink milestone](halo2-fxin2-sink.md) connects
this source to original creation/routing/Play and the real output worker. The
native signal utility demonstrates nonzero sink samples; native113's game
checkpoint remains silent and black.
