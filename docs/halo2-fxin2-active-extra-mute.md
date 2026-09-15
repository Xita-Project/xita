# Original active FX15–22 volume update

Native208r executes the original mute calls for bins 15–18, then reaches the second reverb description, effect index 8. The original intro is visible and the last presented frame remains black; the original main menu is still absent.

After the [native207 reverb update](halo2-reverb-startup-adapter.md), the original `21EE80` loop calls `SetVolume` at `37B66F`, returning to `21F1A0`. The first observed object is source bin 15 with value -6400. The loop addresses the eight original FX15–22 voices, whose established single routes are bins 6, 7, 8 and 9 repeated twice.

The adapter admits only that caller, exact mute value, an active reviewed source, zero headroom and the established route with unchanged per-route volume. Unsupported gain values, unmute, wrong caller, wrong route, inactive/stopped voices and backend rejection remain explicit stops. Mute changes only output attenuation under the existing audio worker lock. It preserves source ownership, source/DSP time, routes, filter histories and already computed grains.

`tools/verify_halo2_fx_mute.py` executes the owned original constructors, zero-volume setter, one-route replacement, Play and active `SetVolume(-6400)` for each selected bin. Only allocation and FIFO capacity are isolated; sound APIs execute their original x86 code. Each of eight fixtures executes 1,418 distinct instruction addresses and preserves the stack/nonvolatile-register ABI. The setter changes only the parameter volume field, leaves the voice structure and route untouched, and emits `FFFFFFFF` to all three packed attenuation registers. Each decoded slot is therefore `FFF`, the mute value used by the pinned [xemu attenuation implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c#L87-L91).

The oracle requires Unicorn, the owned XBE and fresh output outside the checkout, for example:

```sh
python tools/verify_halo2_fx_mute.py /private/default.xbe --bin 15 --out /private/fx15-audit
```

All 54 host executables pass. Synthetic GP tests verify removal of only the muted source's contributions, unchanged live ownership, advancing time for every active source, retained filter state and repeated-call behavior. Adapter tests cover all eight sources, wrong values/callers/methods, seven invalid object states and backend failure before object mutation. Concurrent worker tests retain a previously computed grain while applying mutes, then verify continued per-source sink consumption and complete drain. The three focused mixer/adapter/worker executables pass ASan/UBSan.

The new explicit mute mask keeps repeated map configuration checks strict: a zero output mask is accepted only with its matching recorded mute bit. Arbitrary missing routes or corrupted masks remain rejected. Original spatial23/24 and nonspatial25 mute behavior is preserved.

Native208's first capture was interrupted for a conservative object-layout check and is excluded. The check found no dependency/build error: ARM structure size remains 1,416 bytes and all five fields accessed by the native backend retain their offsets, so its freshly compiled object is legitimately byte-identical. The internal low-pass field moves from offset 1,396 to 1,400 and is accessed by the changed, freshly compiled mixer object. Separate compiled old/new layout tables verify this distinction. All 180 dependency targets and 130 generated source paths are checked; only `audio_host.o` and `audio_fx.o` differ from native207. The same verified package is replayed as native208r.

The native208r trace confirms the original reverb9 update is consumed again, then calls `21F1A0` for interfaces `012E601C` through `0131601C`, source bins 15–18, each with -6400. It stops before the remaining group at `37BA6F`, caller `21EE74`, index 8, type 12, with the same description values and control word `023F`. Index 8's live prefix and monitor extent require the next audit; the adapter still refuses it. All eight mute cases pass host/original-code tests, while only the first four have run in the game.

The Microsoft Game Studios intro is visually verified in native208r, and the full last-presented frame 136 remains RGB-zero. The transition recording is 22.5 seconds because its capture starts later than the early intro recording. The owned process is stopped after archival. No main menu or gameplay is claimed.

| Native208r artifact | SHA-256 |
| --- | --- |
| ELF | `adf99480e1ba693a287e9b605f6d802dc5bba6d5e9a7516f0765885cba861db7` |
| EBOOT | `d60d815ae6d0e8b105bc25aeb628cac4b0d705a3723b22b0d52479b43be4e9bd` |
| Trace | `af470d9e4462743f7cdf44c065c93b30f5bbd7facdfe9a5ac6706e618f0960e5` |
| Channel snapshot | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| Last presented frame | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

With both owned labs stopped, replay from the private directory using a fresh label:

```sh
python3 preserve_fresh_cache.py native208r-replay
python3 capture_run.py 208r-replay native-208r-artifacts
python3 drive_startup.py 208r-replay native-208r-artifacts
```

Owned XBE/DSP bytes, generated code, oracle descriptors, traces, captures and game-embedded packages stay private and outside Git. Diagnostic packages must never be uploaded as distributable releases. Halo CE, the shared emulator and physical hardware are unchanged.
