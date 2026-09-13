# Halo 2 movie CPU feature probe

Native67's null callback is caused by lost EFLAGS.ID state. The original Bink
routine `372030` toggles bit 21 with PUSHFD/POPFD before querying MMX support.
The runtime retained only arithmetic/direction flags, so the round trip failed
and the movie conversion setup skipped its function pointers.

`XV_EFLAGS_ID=1` now stores ID independently from lazy arithmetic flags and
includes it in PUSHFD/POPFD. The field is appended only when the guard is enabled;
CE's default context layout and behavior remain unchanged. The isolated H2 target
enables the guard consistently for generated code and runtime. This does not
add general privileged flags, 16-bit POPF, CPUID leaf handling, or new CPU feature
claims. [Intel's CPUID instruction description](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
specifies set/clear testing of ID as the CPUID availability probe.

Both guarded and default synthetic tests pass 128 combinations of existing flags
and ID, stack round trips, restoration, lazy arithmetic replacement, shifts, x87
comparison, context copies and masking of unsupported high bits. The guarded test
passes ASan/UBSan. All 21 host executables plus the timed case pass, including the
H2 context layout throughout the runtime tests.

The actual original `372030` in both archived ARM ELFs was independently executed
under Cortex-A9 Unicorn with initialized owned image data. Native67 reports
CPUID=0/MMX=0. Native68 reports CPUID=1/MMX=1; its original explicit MMX-disable
argument still reports MMX=0. ESP, EBX and EBP restore correctly in both builds.
The script and game-derived evidence remain private (`check_id_arm.py`,
`native67-id-arm.log`, `native68-id-arm.log`).

Actual Vita3K native68 confirms the original detector stores one at `466D6C` and
`5637DC`, and selects real conversion callbacks `3EDE70`, `3EDA80`, `3EDC20`.
The next stop is the undiscovered callback `3EDC20` from `3E9C70`, return
`3EA026`. Its descriptor is the image-backed movie conversion table `57A080`.
The original audio helper handles DSERR_NODRIVER separately; no audio behavior
was changed. The display is still black, with no decoded movie frame or menu.

| Private native68 artifact | SHA-256 |
| --- | --- |
| ELF | `4d8f272320ece50674615e6c0615a27d8a9628bdcce5470dad340fc3fd2fb2fc` |
| EBOOT | `56ad2ed58789f503175f22e868d3feb28c1e195597b4be711ccc69282d14e90b` |
| VPK | `44da8e146d36d76b8eaf106af9732032768d6513482e6ae4c82849fc23472990` |
| Boot trace | `fb4d1af2961be47bba12cb2334d9be64ae4699d7421985230cad39893d4f933a` |
| Last presented buffer | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Replay: `python3 ../private/run_lab.py replay68 ../private/native-68-artifacts/halo2-boot.vpk`.
The generated source remains `online-interfaces/generated`, recompiled with the
H2 ID flag option. The package embeds owned game code/image and must not be
distributed or uploaded as a release. Assets, generated code and captures remain
outside Git.
