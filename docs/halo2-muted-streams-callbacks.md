# Halo 2: muted stream ownership and original callbacks

Native137 creates fifteen further empty mono16/8000Hz streams with the original
five muted routes, then executes sixteen original completion callbacks for the
four previously playing zero streams. Those callbacks refill packets through
original game code. **This is sound initialization progress: the displayed
frame remains black, with no nonzero game audio or main menu.** It follows
[zero packet ownership and sink fences](halo2-zero-stream-packets.md).

The original caller `333890` constructs five routes: four descriptor-derived
bins27–30 and center2, each at volume -10000. It calls `335EF0` for each context;
that path creates a stream and associated game bookkeeping but submits no
packets. Native136 captured the same exact descriptor. A private oracle executes
187 original parameter-constructor instruction addresses and verifies all five
stored bins and volumes, callback/context and default headroom600, with no
allocation or MMIO. Its failed exploratory assertion about unrelated field18
was corrected using the actual constructor layout; that field is not a combined
route gain. Headroom is independent from the retained route-volume array.

The adapter retains the exact five pairs and owns a real inactive mixer voice
and parent reference. Removing headroom preserves the real mixer's mute. A
synthetic nonzero feed verifies that gain produces zero output, rather than
replacing source samples. All other route lists and changes remain strict;
Process on these five-route streams is unsupported. Tests check every pair
field, cross-page descriptors, state and references, headroom, gain and release.
All43 host executables and the stream ASan/UBSan run pass.

Native137 creates voices88–102 for the fifteen muted streams. The cooperative
worker completes tickets1–16 for voices84–87 only after the corresponding real
sink fences, executes original callback `335D82`, and observes its original
`335D38`/`33586D` copy-and-refill path returning through Process caller `335D7B`.
Twenty-four zero packets have decoded at the stop; eight remain pending. This
validates native callback execution and control-state checks, not precise Xbox
interrupt timing. The next original global creation at `37D835`, return
`335ED8`, requests a different callback `335D99` and remains a strict stop.

All170 copied dependency targets were verified before launch. The runtime
change is confined to H2 stream creation, headroom and read-only diagnostics;
generated code and CE sources are unchanged.

| Artifact | Native137 SHA-256 |
|---|---|
| ELF | `5f2394ec25e58827f666c7fbd395e1989fd3acda42ebb01d16c286ab422404bf` |
| EBOOT | `496cef67446e985cc937cefe2c6fadcbb7d305761507ba06dd85efdc2987fe3b` |
| VPK | `cecc85fc6fa5d79608e5f2a3fde508ea4a338f67bfda5cc67df3b28d3a7a7b42` |
| Guest trace | `35965bd682e9a2a6bb83c6a0c6a7d836b312e33d45aa93fd90e1bab1fdf76b70` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence is in native137 artifacts/views/manifest,
`audio-stream-five/native-build-identity.json`,
`audio-host/five-route-stream-params-original.*`, and
`dsp-bringup/audio-stream-five-*`. Exact replay from the private directory:

```sh
python3 run_lab.py 137-replay native-137-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses `audio-stream-process/generated` and its image with output
`audio-stream-five/build` and the existing DSP/spatial/filter diagnostic flags.
**The package embeds owned game content and must not be uploaded or distributed.**
