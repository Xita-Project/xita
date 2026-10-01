# Three-branch integration — September 5, 2026

The user requested the separate branch fixes alongside the existing 480p build.
The three newest unmerged branches were integrated into the working tree:

| Branch | Commit | Result |
| --- | --- | --- |
| `offscreen-rt` | `299af6662f8b60263abcd9b7c18fd241c1af7c86` | Pooled render targets and ordered mesh/UI scene replay |
| `dsound-state` | `13980e1a08556055218eb8e3d9005285db62eece` | Buffer/stream playback state, duration, pause, stop and looping queries |
| `adhoc-net` | `1abd6f4b610e135b69793e37814d4d3e797dfd4e` | Opt-in PDP/PTP transport for Halo networking |

The older `psdef-hash` branch remains separate. Integration used per-file three-way
merges with backups; no commit or index changes were made. Existing dashboard,
480p, loading, HUD and texture changes were preserved.

## Integration corrections

Offscreen replay now receives the active backbuffer dimensions and surfaces.
In 480p mode it returns to 848x480 before the existing upscale to 960x544; loading
feedback still uses the completed display image. Native network connection UI runs
before the game switches resolution. Wireless remains disabled by default.

Restoring offscreen draws exposed a radar regression: the legacy font texture path
whitened black pixels and tinted the final copy green. The exact active radar
combiners now use existing texture-only/texture-times-color shaders, the material
decoder and captured blending. Blood Gulch uses four targets, totaling 512 KiB in
the observed emulator run.

The user then reported a broken main menu during final validation. The trace showed
VS04 menu glyphs matching the HUD combiner, but the compiled HUD shader existed
only for VS03. These draws fell back to a plain texture shader and could lose their
foreground. Routing now checks the vertex program too: VS04 retains its existing
menu coverage path, while its supported radar producer/copy pairs keep the new
route. Host fixtures cover both supported radar pairs and rejected menu/HUD pairs.

Further testing found that the per-vertex-program combiner cache stopped after
12 links. VS47 consumed all twelve during menu/weapon rendering, so its later
sky materials silently fell back despite having matching packaged shaders. A
shared, hashed cache now distributes the same total 1,152 slots across programs;
the runtime metadata increase is about 8 KiB. Entries are not evicted while the
GPU can reference them, failed links are cached, exhaustion is logged, and linked
programs are released before vertex programs at shutdown. The next emulator run
successfully linked 31 VS47 combinations without a link failure.

The old camera-recovery heuristic fired at 17:41:20 and 17:42:47 during normal
opening cinematic shots. It now requires `XV_CAM_FIX=1`; default startup leaves
camera control to Halo. The user reports the next opening sequence looks almost
one-to-one with Xbox, but later reports a white haze and a freeze. The freeze
stops Present calls and spends 99.8% of sampled game-thread time in `0x51E90`, a
polygon-processing function. A second reproduction captured polygon address
`8188A33C`, count `724E4800`, and vertex pointer `000003FF`. The loop sign-extends
its 16-bit vertex index, so this invalid count cannot be reached before the index
wraps; it also writes beyond the intended stack scratch buffer. The source of the
invalid polygon data is still unknown. The user subsequently requested the latest
build on hardware; this freeze remains a known issue in that testing snapshot.

## Validation

- Native `make RECOMP=1 -j8` passes, including the menu correction.
- Shader loader/semantic HUD tests and texture/cache tests pass. Coverage includes
  dead combiner state, active-state rejection, material alpha and render-target
  aliases bypassing cached CPU texture data.
- `python3 tools/test_render_targets.py` passes ordered scene, lifetime, failure
  handling and 848x480 viewport checks.
- DirectSound state tests pass against the real HLE with a controlled clock.
- `sh tools/tests/net_hle.sh` passes with address/undefined-behavior sanitizers.
- Final executable boots through the dashboard; main-menu, profile-selection and
  multiplayer-lobby labels and highlights are visible in Vita3K.
- Shared combiner-cache host tests cover more than twelve links on one vertex
  program, distinct blend/vertex keys, collisions, failed links, pool exhaustion,
  retained entries and shutdown/reinitialization. The same cache test passes with
  address/undefined-behavior sanitizers.

These checks do not establish hardware FPS gains, complete lighting/HUD correctness,
campaign dialogue progression or two-Vita wireless play. Full GXM waits between
offscreen scenes can increase render time and need hardware measurement. The
25 fps target, geometry spikes, flashlight disappearance, HUD meters, sniper
arms/scope overlays and main-menu checkpoint resume remain open.

## Artifact and hardware status

Local evidence and rollback copies are under
`/home/birchwoodgod/xita-backups/2026-09-05-163259-hud-cutscene/branch-integration/`.
The superseded menu-regression executable is archived in `menu-regression/`.

The superseded menu-only USB candidate is 32,918,474 bytes, compressed and padded to the
existing installed file size. All three decompressed SELF segments match the
native build. Candidate SHA-256:
`e7a19cafc812f8c23664f3ca6c6f9dd4881e8989427b24a4c287a1e1eef7a221`.

The cache/camera snapshot requested for USB testing is recorded in
[the latest deployment report](hardware-20260905-latest-usb.md). The prior hardware
build and its 480p Blood Gulch measurements are documented in
[the hardware report](hardware-20260905-bloodgulch-480p.md).

The integrated snapshot subsequently crashed on hardware in Blood Gulch. The
[crash report and recovery deployment](hardware-20260905-bloodgulch-crash.md)
document the frame handoff race and its correction, deployed at 18:31 CDT.
The user now accepts stable 20 fps as the first target, with 25 fps following.
