# Gameplay performance candidate — September 9

The September 9 gameplay build removes development function tracing from normal
play. The installed profile/LiveArea build still executes those markers and
runs the 1 kHz sampling profiler. This candidate retains the recent profile and
rendering fixes, graphics settings, frame timing and per-core CPU display.
Its physical Vita FPS benefit remains unmeasured.

## Change and evidence

Fresh generation produces the same 8,021 functions and 928,726 lifted Xbox
instructions. Omitting `--trace-funcs` removes 8,021 entry-marker sites and
35,402 return-marker sites. These are static sites, not calls per frame.
The generated units match the previously validated untraced generation. Against
the installed build, the only additional differences are a comment and moving
an existing decal hook before local scalar initialization.

The only changed runtime source is `recomp/xv_funchist.c`: startup resolves the
function-watch configuration even without traced entries, and explicitly logs
whether sampling and compiled function tracing are enabled. No parity,
simulation, renderer, shader or queue-policy optimization is combined with it.
With the device's existing settings, the expected startup line is:

```text
[prof] sampling profiler off; guest function tracing absent
```

Setting `XV_PROF=0` on an instrumented build stops only the sampling thread;
it cannot remove compiled entry/return markers. An untraced build still keeps
frame/prepare/submission/retirement diagnostics, but complete guest-function
attribution and entry/return watches require an instrumented build. The previous release remains
available for that purpose. Smaller code and fewer instructions do not predict
a specific FPS improvement.

## Collected hardware baseline

Read-only USB collection `2026-09-09-185208-livearea-followup` confirms the
profile/LiveArea executable is installed, SHA-256
`a7c3107d0900360e5636b3701258e74ee35ffe4c4d0ffecc3ed2e9dd01b33aed`.
The new log's SHA-256 is
`c17448a7e8896b4778117322f34a006d46dcc64b0e924888ca272db213c55f32`.

All 79 complete render-time windows report zero ordinary Finish calls. Geometry
worker reports include 397 parallel sort jobs; later windows have only small
serial jobs. The run includes Blood Gulch, menu activity and resolution changes
from 360p through 544p/480p. It contains periodic vertex-comparator statistics,
not controlled benchmark phases. Do not pool the run as a matched before/after
test. The saved configuration finishes at 544p; requested CPU 500 MHz is
reported as effective 444 MHz. Device settings are preserved for this update.

## Validation

The host runtime suite, original profile/variant loader regression and native
build pass. All 81 checked runtime inputs match the candidate source. The linked
ARM comparison passes all 640 synthetic cases across five math routines: full
guest context, 2 MiB guest memory, modeled firmware copies and native fast/fallback
choices match. Instruction counts fall by 0.27–1.39% in those isolated routines;
these are neither frame-time estimates nor hardware speed measurements.

The exact VPK passes an isolated Vita3K smoke test: Default profile → normal
Split Screen → Blood Gulch, movement, camera rotation, plasma-pistol firing and
Leave Game; a named campaign profile enters the cryo bay on Normal, responds to
camera input and returns through Save and Quit. Two geometry lifetime samples
check 226 and 510 draws with zero changes before completion. The log reports no
vertex-upload failures or constant-buffer draw rejections. The emulator was
capped at 20 FPS and stopped deliberately after returning to the menu.

The VPK contains 1,585 files. Only the executable and three LiveArea assets
change against the profile release; all other 1,581 payloads match. The executable
is 3,898,564 bytes smaller. Physical FPS, LiveArea display, longer campaign play
and the prior driving/rocket GPU crashes remain unverified.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `xita-gameplay-20260909.vpk` | 13,295,453 | `a7c7288e13b861d48c2e2fcf52c0b553d3c642a6ed8e3300cba06dc301ddcd8d` |
| `eboot.bin` | 31,093,418 | `5bd4663efe5e3a7a93d8e3923888812180e946458169cf9840942b42546e611f` |

## LiveArea compatibility follow-up

The user confirms a blank/plain background after tapping the bubble. USB hashes
show the revision-2 artwork and template are installed. The cause is not yet
proven; this is not a missed file copy.

Revision 3 follows the [VitaSDK sample guidance](https://github.com/vitasdk/samples/blob/master/README.md):
128-color PNG-8 artwork and UTF-8 XML with CRLF line endings. Dimensions remain
840×500 and 280×158; the template keeps the sample's `a1` layout. The generator
retains these conventions when artwork is rebuilt. This is a compatibility
candidate, pending a VitaShell installation and physical display check.

## Reproducing and testing

Generate from the supported private game inputs without `--trace-funcs`, then
build with `make RECOMP=1`. Keep the graphics settings and scene fixed when
comparing with the installed instrumented build. **L + R + Square still selects
the configured runtime experiment; it does not compare tracing on/off between
these executables.** Normal campaign play remains useful for bug reports.

The standalone linked-ARM comparison uses synthetic state and modeled firmware
copies. It checks guest state/memory equivalence, not Vita frame times:

```sh
python tools/test_arm_math_runtime.py \
  --baseline /path/to/instrumented/xita.elf \
  --candidate /path/to/gameplay/xita.elf \
  --functions f_000B77C0 xv_math_polygon_clip f_000B71C0 f_000B5B40 f_000B5F60 \
  --output-dir /tmp/xita-gameplay-comparison
```

Install the [gameplay VPK](https://github.com/Xita-Project/xita/releases/tag/dev-20260909-gameplay)
over the existing app in VitaShell. Copying the package over USB only stages it.
Keep existing game data, saves and settings; the asset update needs installation.
