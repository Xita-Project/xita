# Halo 2: map sound setup repeats the active FX configuration

Native142 uses the same executable as native141 with a fresh private cache4.
The prior cache directory and raw image were preserved and hashed before the
original formatter created new metadata. Main-menu map loading now succeeds
far enough for normal Start to complete the original movie cleanup and reach
map sound setup. This avoids the populated-volume failure documented in
[the Stop checkpoint](halo2-movie-gp-stop.md); it does not add FATX coherence.

The next strict stop is `37C5E4`, caller `2AEC87`, for active nonspatial FX23
interface `0128601C`. Its list repeats `{6:0,8:-6400,7:-6400,9:-6400}` after
all fifteen FX sources have been created and started. The backend previously
accepted this configuration only during the initial seven-source sequence.

The adapter now accepts unchanged repetitions only when all fifteen exact
routes, active ownership bits and both fixed filters match the completed
configuration. The permitted route, mute and filter setters retain all source
counters, DSP state, HRTF histories and low-pass histories. Different route
values, an incomplete or altered layout, other mute/filter keys and restarting
an active voice still reject. The original caller and guest ABI checks remain.

Five private original-code probes repeat route, filter and mute setters on
already configured voices. They execute 342, 141, 292, 292 and 221 distinct original
instruction addresses respectively, with unchanged parameter and voice memory.
Allocation and command FIFO capacity are controlled oracle fixtures, not Xbox
hardware execution. Host tests exercise the real GP/mixer before and after
these calls, verify exact state preservation, and reject changed routing or
an invalid fifteenth voice. All 44 host executables and FX ASan/UBSan pass.

Native142 records 291 nonzero sink grains, peak 2654, zero worker error and a
successful real worker/port close. This muted SDL-dummy lab does not establish
speaker audibility. The display remains black: draw60 has 307,200 nonblack
original texture pixels but zero nonblack GXM output pixels. The identical
archived native106 VPK also reproduces that mismatch in a control replay,
although it displayed the intro in the earlier archived run. Shader bytes,
constants and validated geometry are unchanged; the lab rendering difference
requires separate investigation. No visible main menu is established.

| Artifact | Native142 SHA-256 |
|---|---|
| ELF | `732a7ef4ada15a74a7acce06625b691b9725d1d915a1beba8dfb35f1d6e036bb` |
| EBOOT | `8957f99b3e2fdefeebc889e7c5cd5bd916ced24565b0381e5d371ec7c07846ab` |
| Guest trace | `7f1543f9fc75435498c3c4cf1ffbe8cf78890b7964c7b46f5d03ed78b8f60dd4` |
| Final black frame229 | `fc2626ddb31350e1333861025e2a177ad48038e8e8c924fc2f7aad301deb0e2f` |

Private evidence: `native-142-artifacts`, `native-142-view`,
`native142-prior-cache4/manifest.json`, `audio-host/repeated-fx-configuration-*`,
`dsp-bringup/fx-repeat-*` and `native-106-regression-artifacts`.
Native142 input was Enter/Start at 22:00:38.022 UTC, held for two seconds.
Both native142 and the control replay are stopped. Diagnostic game packages,
shader caches, traces, coefficients and captures remain private/outside Git;
packages embed owned game content and must not be uploaded or distributed.

## Native143 validation

The new build passes the complete repeated route/mute/filter sequence during
original map sound initialization. Normal Start reached the guest only after
the owned SDL game window was explicitly focused (22:09:04.689 UTC, held two
seconds); the earlier unfocused key attempt was not observed by the guest.
The next original stop is `37B60D`, caller `191294`, with six arguments:
device `00936008`, effect index4, offset32, source `007342BC`, eight bytes,
flags0. This is a new effect-data boundary, not an accepted silent operation.
The worker closes successfully and the final frame201 remains black.

All 170 dependency targets were validated. Native143 hashes:
ELF `85a21904cfff300e567a747688cb69c4ea2feccdfcdf99f5e6639cd0199e7473`,
EBOOT `147942542e2c56305736d226c7c04dd3cd845e96607ccc5e4080cd369fb87698`,
VPK `c572cf596bb0ba1fcda9baa136643187816bdd47903855383ee0ebdefe4ab972`,
trace `54bb75fcf3b4fca469b8a938ddcf1abb08f36c5fb9e6eddc4f7733c08439000d`.
The owned process is stopped. Private replay, after preserving any populated
cache4 and allowing the original formatter to initialize a fresh directory:

```sh
python3 run_lab.py 143-replay native-143-artifacts/halo2-boot.vpk
python3 focus_game.py
python3 movie-skip-audit/send_start.py 143-replay
python3 run_lab.py stop
```

Allow the original main-menu map copy to finish before Start. Build directory:
`audio-fx-repeat/build`, with the prior generated source and diagnostic flags.
The separate old-binary shader-cache regeneration control still produces black
GXM readback; its preserved evidence is `native-106-recompile-artifacts`.
