# Original empty sound work path

The [native201 audit](halo2-sound-work-audit.md) establishes that the current host-backed sound device has no original hardware singleton, its ordinary streams have never submitted packets, and accurate notifications already run through the real sink-gated worker. The next admission executes the original zero-argument `37B844` wrapper and its critical-section helper without replacing their instructions or synthesizing a return value.

This is an empty low-priority-work subset, not general DirectSoundDoWork support. The read-only guard requires the exact caller chain, passive IRQL, null original hardware singleton, unsuspended state and a healthy real device. It validates the original stack/critical-section write footprint and physical aliases before allowing original code to execute. Uncommitted buffer locks reject. Ordinary streams must retain their exact callback/empty routing and have no packet history or pending ownership. Every stream has a mapped matching object header and consistent packet counters. An unsubmitted stream must also be inactive in the real mixer. Active accurate streams require the existing worker, callback and GP27–30 route.

The guard never changes stream packets, device references, samples, listener dirty state, return registers, guest memory or callback timing. Accurate packets stay pending even when the sink reports readiness; only the existing cooperative worker retires them. Ordinary Process, timed operations and nonnull hardware-device paths retain strict stops. A later implementation that adds such work must extend this contract before it can pass the wrapper.

The existing emitted original wrapper/body is unchanged. Its new 41-byte preparation fingerprint is `edd97faddb280cb5fe23b85740d14540772206c0cc5af4f08e0cdb1b9b7931b7`; the existing generic DSOUND entry remains a guard followed by the original instructions. The build uses the same generated code and image.

All54 host executables and52 callback-root tests pass. Added fixtures check exact caller/helper chains, missing/aliased control pages, passive IRQL, hardware-owner/suspend rejection, state preservation and unsupported lock/accounting cases. Real ordinary codec voices pass while empty, then reject when marked active or given unsupported packet history. Real-decoder accurate-stream fixtures remain pending across the guard even with sink completion ready, and only their original callback worker retires them. Device, ordinary-stream and packet suites also pass ASan/UBSan.

Native202 executes the original wrapper, then the original `RtlEnterCriticalSection` return `379EB8` and `RtlLeaveCriticalSection` return `37B86C`, and returns to the game. The next strict stop is unresolved original callback `21BF00`, invoked by `127F10` through `[EDI]`, return `127F59`. The build verifies180 dependency targets and all179 existing objects; only `audio_host.o` differs from native201. Generated original code, the graphics renderer, audio backend and shared CE paths are unchanged. The original intro is visually confirmed. The terminal capture and full last-presented frame136 remain black: **the original main menu is still absent**. Channel and presented-image hashes match native201; no additional flip occurs before the callback stop.

Private build and validation evidence is in `empty-sound-work`; prior strict native201 remains archived. Diagnostic packages embed owned game code/image and must not be distributed as releases.

The owned :111 PID3560669 was stopped after capture. Frozen evidence is in `native-202-artifacts`, `native-202-view` and `native-milestone-202.json`. ELF SHA-256 is `a3e15003342efb4f736523a984146162396d01ca3ec0434d3dfdfe3c02a0b866`; EBOOT is `4318ec343f36a3894497c997e68dfd71143e2acb038eb203e587f55777237a8d`. Trace SHA-256 is `bb69e724d5fc26422340e00c9f83f72abba2d1088a593cc1721b1540f94369f5`, channel snapshot `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e`, and last-presented image `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54`.

Exact private replay with the owned emulator stopped:

```sh
python3 preserve_fresh_cache.py native202-replay
python3 capture_run.py 202-replay native-202-artifacts
python3 drive_startup.py 202-replay native-202-artifacts
```

Next: validate the original callback's registration/table before adding discovery roots, then execute its original body. No callback return or replacement data is supplied.
