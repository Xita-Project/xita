# Second original reverb instance

Native209 captures the second original `XAudioSetEffectData` request without changing its outcome: index 8, caller `21EE74`, type 12, no raw-output pointer, guest FCW `023F`, empty x87 stack and clear DF. Its 13-word startup description matches the first instance's observed preset. The read-only probe captures the complete 280-byte live prefix under the mixer lock. The original Microsoft Game Studios intro remains visible, but last-presented frame 136 is entirely RGB-zero and the original main menu is absent.

The owned DSP descriptor gives index 8 a separate state range beginning at `464C`, with 2,216 bytes. Index 9 begins at `4EF4`; both use the same type-12 layout. The saved image's state region begins at `3F98`, after 3,552 code words. Index 8 has its own scratch pointers in the live prefix; those are supplied to the original converter rather than copied from index 9.

`tools/verify_halo2_reverb_commit.py --effect 8` executes the original deferred setter, helper, copier and commit across 112 distinct original instruction addresses. It coalesces the two writes into the exact 528-byte extent beginning at `465C`, retains the saved gap, and publishes monitor header `[433,3552,18012,132,2]`. The tool's default remains effect 9, whose original header is `[987,3552,20228,132,2]`. Both passes preserve source data, nonvolatile registers and stack cleanup; no original function is substituted.

The native adapter admits only these two observed instances and the previously verified exact caller, startup preset and FP controls. It still executes original converter `3838A4` in its checked private guest workspace. The queue has distinct public entry points for indices 8 and 9 and publishes one pending command under the mixer mutex. A request cannot overwrite the other instance's pending update. The real worker tracks which instance was submitted and checks the original monitor acknowledgement, advancing GP frames and cleared update flag. Other indices, presets, busy states and unsupported sound behavior retain explicit stops.

Synthetic tests cover both instances, exact untouched peer/shadow/engine state, aliased source parameters, repeated cross-instance busy rejection and 22 invalid state/range/control cases for each. Adapter tests check index selection and full caller preservation for all three admitted control words. The host suite and ASan/UBSan engine, adapter and concurrent worker tests pass.

Using native209's actual prefix and description, all 544 converted workspace bytes match the original x86 oracle. A native Cortex-A9 utility independently checks `037F`, `027F` and `023F`, with zero differing bytes, unchanged input and intact nonvolatile registers/stack. A separate utility executes the owned original GP monitor: it imports 132 words exactly once starting at X word `231`, acknowledges command 2, completes its third frame with 104 transfers and processes the update flag from 3 through 7 back to 3. Publication leaves index 9 and all live GP state untouched before that real DSP execution. Both utilities report `completion=PASS` and their owned :111 processes are stopped afterward.

| Effect-8 utility | ELF SHA-256 | EBOOT SHA-256 |
| --- | --- | --- |
| Original converter | `8eba6182d3d3e746546e099e6b48a5d4cc0c851ea31473221113fc76bfd531a7` | `1db40f9d84f930fb46a43c421a3e0c0cff79a0fe4041105772211ac810dd49e5` |
| Original GP monitor | `68c3df236b10f28dd5bf70b8d351df252ddc6ee52a7e3068902f89bacc9c3c04` | `67bd364ba34d6c04ad5791ee71e8e9bd5530b4d9bc2659757c0dbccf28181131` |

Private evidence is `private/reverb8-probe209/`, `private/native-209-artifacts/` and `private/reverb-pair210/{converter-native,monitor-native,original-commit8,original-commit9}/`. The effect-8 converter result SHA-256 is `65a504419514b72549d028eccd937ac43cf168905c9f8c83109051d038f77be3`; the monitor result is `7e31a9e62fb2cb0acaec30645ae4ed93f611acf1b97c487ea9764bcf6c2c61cf`.

| Native209 artifact | SHA-256 |
| --- | --- |
| ELF | `849c44c90659a0c1c75a8541b8eaa6b83cd3252520dd2f8dc0022331f959f9d3` |
| EBOOT | `44a0d573811f2401ecf892652ff98ddbf12cb23bf0b30b9a1acbd85d4e67c059` |
| Trace | `4dc10aa00665638061e947d97a17bb4d94a0aa03cf57f3c973d943403a7f0d77` |
| Last presented frame | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Owned executable/DSP bytes, generated code, converted parameters, traces, captures and diagnostic packages remain private and outside Git. The packages embed owned code and must never be uploaded as distributable releases. A converter match or DSP acknowledgement does not establish visible menu output or physical audibility. Halo CE, the shared emulator binary and physical hardware remain untouched.

Native210 connects this path to the original game. Effect 9 is consumed with GP frames 80,800 → 80,832; effect 8 is then consumed with frames 80,896 → 80,928. Both original update flags return to 3. The original caller now completes all eight active FX15–22 mute calls, retaining the sources and GP time. The next strict boundary is DSOUND entry `37D141`, returning to `21F201`, with its sole argument, device interface `00936008` (the wrapper returns with `ret 4`). No behavior is added for that method in this checkpoint.

The normal fresh-cache replay preserves the original intro, captures 72.6 seconds of transition video and verifies that the last presented 960×544 frame 136 still has zero nonzero-RGB pixels. The main menu remains absent. The owned :111 process is stopped after capture. All 180 dependency targets, 130 generated source paths and 179 object identities are checked; only `audio_host.o`, `audio_vita.o` and `dsp_engine.o` differ from native209. Generated code and owned image are unchanged. The final 54 host executables and three ASan/UBSan executables pass.

| Native210 artifact | SHA-256 |
| --- | --- |
| halo2-boot.elf | `396018c4b70f34647fcfa3e774ae0fd2578aeba0632d25e46b1aff9a3a2a4b8b` |
| eboot.bin | `c144442f772db3d2a5e7d26e6344c09c47b2a07b5fc67a87cafcbccf3eeb2083` |
| boot.log | `abbf0ac9ecf5db9fea01e2a99f0994f2da8446d2a646ccf3d62d8c0fba024f76` |
| channel-at-stop.json | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| last-presented-at-stop.bin | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

With both owned Halo 2 lab instances stopped, replay this exact frozen private build from the private directory using a fresh label:

```sh
python3 preserve_fresh_cache.py native210-replay
python3 capture_run.py 210-replay native-210-artifacts
python3 drive_startup.py 210-replay native-210-artifacts
```

The next bounded task is the read-only original `37D141` method/caller contract audit, followed by an adapter only if its behavior can be backed by the live device state and validated against the original implementation.
