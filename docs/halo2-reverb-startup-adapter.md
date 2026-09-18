# Original reverb conversion and live worker integration

Native207 executes the original reverb conversion and the real audio worker consumes its deferred DSP update. Startup then reaches the next original FXIN2 volume call. The original Microsoft Game Studios intro is visible; the main menu remains absent. The detailed validation below separates host/oracle tests, native utilities and actual game execution. The [second-instance follow-up](halo2-reverb-second-instance.md) extends this same audited path to effect 8.

The opt-in Halo 2 audio adapter now connects the observed `XAudioSetEffectData` call at `37BA6F`, returning to `21EE74`, to the original conversion routine `3838A4` and the [original DSP monitor queue](halo2-reverb-monitor-queue.md). It admits only the captured effect-9 type-12 startup description, with no optional raw output. Other indices, presets, control modes and callers still stop explicitly.

The adapter reads the current effect prefix under the audio lock and executes the original converter and its complete 14-function call graph in a private mapped guest workspace. Every admitted original function has a checked owned-executable fingerprint. Admission is limited to the exact temporary context, bounded stack and root context pointer; it does not enable these DSOUND functions for other guest callers. The converter retains its original arithmetic and its normal generated control flow.

The caller's context and native FP state are preserved across conversion. The adapter checks the original nonvolatile-register and stack contract, unchanged input, context pointer and workspace boundaries before releasing temporary memory and publishing any DSP update. Only the observed 53/64-bit x87 precision modes with nearest rounding are admitted. The 24-bit x87 mode remains unsupported after the comparison exposed a mismatch. A genuine temporary-allocation failure returns `E_OUTOFMEMORY`; unsupported conversion or a busy monitor stops rather than reporting completion.

The real Vita audio backend queues the deferred update under its existing mixer mutex, requiring the live playing FX engine. The original DSP monitor imports it on the next actual GP execution. A worker-side check requires command acknowledgement, advancing GP frames and consumption of the reverb update flag. Its diagnostic says that the grain has not yet been submitted: GP consumption and sink completion remain distinct events. There is no separate substitute reverb algorithm, reset of DSP history or manufactured completion.

Synthetic adapter fixtures cover the caller ABI, noncontiguous guest input, both supported control words, all 13 changed preset fields, invalid mappings and aliases, allocation failure, busy publication, damaged conversion ABI/footprint and seven invalid execution-scope cases. Concurrent backend fixtures reject a foreign engine and an engine without the required effect. The actual original arithmetic and monitor execution are checked separately against owned assets, which remain private.

All 54 host executables, the adapter ASan/UBSan checks and 61 focused Python checks pass. The expanded negative tests initially triggered `-Wclobbered` in their `setjmp` loops; isolating the expected-stop call in a helper fixes the test harness without changing native implementation behavior. Both failed and corrected logs are retained privately.

The private original-converter comparison executes 717 distinct original instruction addresses. All 544 workspace bytes match the original x86 oracle on both host and native Cortex-A9, for control words `037F` and `027F`; input and nonvolatile registers/stack remain intact. The isolated native converter utility reports `completion=PASS` in `private/reverb-converter/native-probe/`.

| Converter utility artifact | SHA-256 |
| --- | --- |
| ELF | `dc5916f4cb7aaf078a55ca0d53526eff607125acd1ab926080383ff90d2264b6` |
| EBOOT | `1b2a1ebbf5563bf4d239d6c1a1074ab0a9799542d810d4fad8496f12868b4575` |
| Result | `5834d8c18235b5136370a08fcb152eb93c8140038935f824a38956709501fe24` |

Native205 generation changes only the `37BA6F` function entry; all 12,614 other generated function bodies and the loaded image are byte-identical to native204. Its 12,615 discovered functions and 3,663 unsupported instructions are automatic coverage metrics, not runtime compatibility. The complete 14-function converter call graph contains no unaccounted indirect call.

Native205 completes the normal fresh-cache startup and original Start input, then stops at the new admission guard before conversion or queue publication. The original caller and all 13 preset words match; this trace does not identify which control or mapping check rejected admission. The terminal probe is extended to record those values read-only for the next replay. No admission condition is relaxed on that basis.

Native205's original Microsoft Game Studios intro is visually verified. Its full last-presented frame 136 is black and the original main menu remains absent. The terminal window capture is the emulator library after guest exit, not a game frame. The transition recording is 73.7 seconds. All 180 dependency targets and 130 generated source paths are verified; 175 of 179 objects match native204, with only the adapter, backend, DSP engine and generated unit 107 changed. The owned emulator PID was stopped after capture.

| Native205 artifact | SHA-256 |
| --- | --- |
| ELF | `0e41f3ec1b4d835f149378302c1eb7054cb636e0438969ca4c86378a7795cb5a` |
| EBOOT | `3c85eb4cb9fa438d10b8f44bbab85e99c79c889625898e9099a461cec2b82444` |
| Trace | `720a6d23069d3780a1e5df61bdc954acf6f768624e3f785526f3bd0ad9f2e1ef` |
| Channel snapshot | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| Last presented frame | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

With the owned labs stopped, replay the frozen package from the private directory using a fresh attempt label:

```sh
python3 preserve_fresh_cache.py native205-replay
python3 capture_run.py 205-replay native-205-artifacts
python3 drive_startup.py 205-replay native-205-artifacts
```

The monitor utility demonstrated actual original DSP consumption before the main-game builds above reached it. Neither a converter utility nor an acknowledged DSP command alone establishes menu rendering progress.

Native206 identifies the exact admission mismatch: guest control word `023F`, empty x87 stack, clear DF and native FPSCR `20000011`. All effect/input mapping checks pass and neither owned storage nor the caller frame aliases the input. The other caller/preset checks still match. It stops before conversion or mutation; the intro is visible and frame 136 remains black. Only `audio_host.o` differs from native205; all dependency paths are verified again.

| Native206 artifact | SHA-256 |
| --- | --- |
| ELF | `51472c236ca3ca0fbc25f728db8ec1f64f73a2c82e7e86e3492dfabc40d43708` |
| EBOOT | `8069da3aebd59cd1bacf42d12a3664f850133bb3f0fff608a38de4d6672a8b52` |
| Trace | `53348ee78ea7676db537a2f1108e3e4d9e0794171710ad3f2a94d06cdafea10a` |

The observed `023F` differs from `027F` only in reserved bit 6. Precision, rounding and exception-mask fields are identical; see Intel SDM Volume 1, figure 8-6 and section 8.1.5 in the [December 2024 manual](https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf). The adapter now admits this exact observed word and retains it unchanged. All unsupported native rounding, flushing, default-NaN, vector-length/stride and exception-enable controls are rejected.

Before changing admission, the original-x86 comparison and native Cortex-A9 converter utility were rerun with `023F`. All 544 workspace bytes match, with unchanged input and nonvolatile-register/stack ABI. The native utility checks `037F`, `027F` and `023F` in one run and reports `completion=PASS`; evidence is `private/reverb-control207/native-probe/`. Its ELF SHA-256 is `bc1f5e9f14c39cda40f36a6ad2e40cb2b28f54882e7fa37b02453677eb63e00f`, EBOOT is `b7f628a7cedb53ff206126a9870fb3073f438baad90d00d746e0e71e720dae9e`, and result is `65a504419514b72549d028eccd937ac43cf168905c9f8c83109051d038f77be3`. Host tests cover all three words and every rejected native-control bit.

A separate private experiment corrected 24-bit arithmetic rounding in an isolated generated converter and matched its oracle. It is not used by this adapter or the main-game build: the observed caller uses 53-bit precision, and `007F` remains explicitly unsupported here. Shared x87 emission and Halo CE remain unchanged.

Native207 executes converter `3838A4` for the original `21EE74` call, queues flags 7 and 264 parameter bytes for effect 9, then records actual worker acknowledgement: monitor command 2 consumed, reverb flags cleared to 3, GP frames 82,528 → 82,560. This log occurs before sink submission and does not claim audible output. Startup then stops at original volume API `37B66F`, caller `21F1A0`, interface `012E601C`, value `FFFFE700` (-6400). That new caller/source route is the next audit; it is not accepted as a blanket mute.

Native207 retains the verified original intro and black frame 136. Its transition recording is 76.61 seconds. The owned :111 process is stopped after capture. All 54 host executables and the final adapter ASan/UBSan checks pass; the 61 Python hook/root checks remain applicable because subsequent changes only affect the native adapter and its tests. Only `audio_host.o` differs from native206, with 180 dependency targets and 130 generated source paths verified. Replay the frozen native207 package with the three commands above, replacing `205` with `207` in both labels and artifact paths.

| Native207 artifact | SHA-256 |
| --- | --- |
| ELF | `d25c12d231ed577a429d87762d9c52662459d6933f027e694134a39c7a06a634` |
| EBOOT | `0a127173b62000b2bac350dc890c0ef32eda708494c16e3454084f185388e16d` |
| Trace | `52baa35d359d7fb1b9b006cb71c56434a6078254f6d59f6976109615141d03f8` |
| Channel snapshot | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| Last presented frame | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Owned executable/DSP bytes, generated code, converted parameters, packages and captures stay outside Git. Diagnostic packages embed owned game material and must never be uploaded as distributable releases. This work does not modify Halo CE, the shared emulator or physical hardware.
