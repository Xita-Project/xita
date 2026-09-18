# Original sound-record callback discovery

Native202 passes the original empty sound-work wrapper and stops on callback `21BF00`, invoked through `[EDI]` by `127F10`, return `127F59`. The preceding original intro is visible; the post-Start frame 136 remains black, with no original main menu.

The owned constructor `21B940` stores table `44A110` in record field14h at `21BAF4`. Original update `127FC0` reads that field, checks the table and first callback for null, then invokes `127F10`. That routine derives four arguments from the original record and calls the first table slot. The owned image contains `21BF00` at that slot. These registration/dispatch spans are fingerprinted before discovery.

Only the observed first slot is added as a root. Adjacent entries and data are not inferred as callbacks, and no function result, record data or game control flow is replaced. Invalid/non-text targets and changed registration/dispatch fingerprints reject. All 53 synthetic callback-root tests pass, including a fixture in which no adjacent table words exist.

Regeneration adds exactly one original function containing 64 instructions and 12 basic blocks. All 12,614 previous function bodies and the image remain byte-identical; the unsupported-instruction count stays 3663. These are automatic translation counts, not runtime or compatibility claims.

Native203 executes that callback and continues through the original update path. The next strict stop is DirectSound entry `37BA6F`, return `21EE74`; its behavior remains unsupported pending the next ABI/consumer audit. The original Microsoft Game Studios intro is visually verified, followed by normal Start after the complete 59,670,016-byte map copy. The terminal image and all RGB pixels of the last-presented frame 136 remain black: **the original main menu is still absent**. Channel and last-presented hashes match native202. The transition recording is 76.4 seconds.

The build uses `update-callback202/generated` and its private image. All 180 dependency targets and 128 generated code plus two support source paths were checked. All 50 nongenerated/runtime objects remain byte-identical to native202. Private evidence is `native-203-artifacts/`, `native-203-view/`, `native-milestone-203.json`, and `update-callback202/native203-summary.json` under the existing isolated private directory.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `e68607504a6ed6843ff7a4a825f48d535c470117c962f643e6889275f1cc50b8` |
| EBOOT | `f273fc4414fd99c5afbb29b1d1378a6917992ae335f102e2758876afdfa0c34c` |
| Trace | `909061f20f8142ad8b62acf2140757feb0fe4c5fe97dcf3e0c6eadcb8a75b5d2` |
| Channel | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| Last presented frame | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact replay from the private directory, with its owned emulator stopped:

```sh
python3 preserve_fresh_cache.py native203-replay
python3 capture_run.py 203-replay native-203-artifacts
python3 drive_startup.py 203-replay native-203-artifacts
```

The owned data, generated code, traces/captures and diagnostic package remain private and outside Git. The package embeds owned game code/image and must not be uploaded as a distributable release. No CE or physical hardware changes are involved.
