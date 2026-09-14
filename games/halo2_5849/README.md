# Halo 2 executable identity and native startup

This is an **identity and discovery profile**, not a working game port. It
selects no game adapter, function overrides, extra roots, variables or symbols.
`5849` identifies the linked XDK library build, not an asserted game build number.

The [latest native checkpoint and exact private replay command](../../docs/halo2-host-audio-device.md)
describe the current normal and diagnostic startup stops. There is no main menu.

A separate [native startup target](../../docs/halo2-native-boot-20260912.md)
executes the owned XBE's entry in Vita3K with an isolated title and filesystem.
It has reached the actual application entry and D3D initialization. The Halo 2 title/menu,
3D rendering, audio and gameplay are not working. This target has no CE renderer
or CE game-address hooks.

The [initial inspection report](../../docs/halo2-initial-profile-20260912.md)
records the executable identity, validation results and known discovery gaps.

Use your matching local executable and keep all generated files in an ignored
per-game directory:

```sh
mkdir -p local/halo2_5849
halo2_input=/path/to/your/default.xbe
halo2_output=local/halo2_5849/recomp

python -m recompiler "$halo2_input" --profile halo2_5849 --check-profile
python recompiler/xbe_parse.py "$halo2_input" --json > local/halo2_5849/manifest.json
python -m recompiler "$halo2_input" --profile halo2_5849 \
  --no-data-roots --files 32 -o "$halo2_output"
```

The existing `tools/recomp.sh` wrapper selects Halo CE explicitly. Use the
module commands above for this profile. `--no-data-roots` disables scanning
non-code sections for roots; the discovery engine still follows direct calls
and considers code-address immediates. Its output contains false boundaries
and unsupported instructions and is not ready to execute.

For the diagnostic startup target, use `prepare_boot.py` in this directory.
It verifies the executable identity and includes the bounded initializer tables
observed in this revision's XAPI startup. See the linked native startup report
for build, isolation, validation and current limits.

`prepare_boot.py --graphics` selects a separate experimental profile for
[explicit graphics bus accesses](../../docs/halo2-graphics-bus-20260913.md).
The original guest constructor has passed PCI, clock, memory and instance setup
and stops at an unsupported PVIDEO access. This is not rendered output.

`prepare_boot.py --host-channel`, built with `HOST_CHANNEL=1` in its own output
directory, adds the [explicit host miniport/channel boundary](../../docs/halo2-host-channel-20260913.md).
It has allocated color and logical depth resources and
[completed the constructor's initial command submission and semaphore write](../../docs/halo2-constructor-submission-20260913.md).
It now also [executes the first depth/stencil clear](../../docs/halo2-first-depth-clear-20260913.md)
and [presents the initial, entirely black framebuffer](../../docs/halo2-first-scanout-20260913.md).
The original game has since retired its first device, constructed its second,
loaded its font assets and [opened the real Vita controller](../../docs/halo2-vita-input-20260913.md).
Startup now [stops at Xbox audio hardware](../../docs/halo2-audio-boundary-20260913.md);
there is still no menu.
General draws, blits, recurring flips and the title/menu remain unsupported. Unknown
commands stop explicitly. Diagnostic packages embed owned game image/code and
must not be distributed or uploaded as releases.

The separate `--host-channel --audio-unavailable` diagnostic selects the
[original game's audio-driver failure path](../../docs/halo2-audio-error-path-20260913.md).
It returns an explicit error at public sound creation, creates no audio object,
and now executes original deferred timer callbacks with the corrected kernel
ABI, runs a limited native FP mask/clear bridge and executes original state
interfaces. Fixing the [font-cache object lifetime](../../docs/halo2-file-lifetime.md)
allows the original code to rebuild its cache and open the intro movie. The
[movie CPU feature probe](../../docs/halo2-eflags-id.md) now selects its original
pixel converters; no movie frame or menu has been displayed. Earlier
texture-state progress followed an incorrectly balanced timer return; the
current checkpoint documents this limitation. It supplies
no audio, general 3D rendering or menu; the ordinary profile is unchanged.

The separate [GXM quad probe](../../docs/halo2-quad-gxm-probe.md) validates the
observed original vertex program against synthetic pixel fixtures. Its shader
preparation and `quad-probe` build target remain separate from the game app.
The opt-in `QUAD_RENDER=1` target now
[executes the first two original movie quads through GXM](../../docs/halo2-original-movie-quads.md).
The [recurring flip milestone](../../docs/halo2-recurring-flips.md) executes
original swaps. [Caller/cache diagnostics](../../docs/halo2-exit-cache-diagnostics.md)
identify the title-exit path on a reused unsupported cache volume. With an empty
private cache4 formatted by the original code, the
[inactive-palette milestone](../../docs/halo2-inactive-palettes.md) copies the full
main-menu map and begins loading its content into guest RAM after Start.
[Per-map lifecycle discovery](../../docs/halo2-map-lifecycle-roots.md) addresses
the next bounded original callback family. Native87 now executes the map and
descriptor callbacks and proves a [null sound-object dependency](../../docs/halo2-menu-audio-dependency.md)
after the unavailable-driver error. Actual captures remain black; no visible
movie or main menu is established. Populated FATX raw/namespace coherence
and general geometry/texture support remain incomplete.

The separate `--host-channel --audio-host` profile, built with `AUDIO_HOST=1`,
now [creates a real Vita audio device and completes the first sound queries](../../docs/halo2-host-audio-device.md).
A separate synthetic PCM utility verifies nonzero mixer output and worker
lifetime. Original game startup stops at the next unsupported per-bin headroom
method, before any game audio or menu. The no-driver diagnostic remains intact.
