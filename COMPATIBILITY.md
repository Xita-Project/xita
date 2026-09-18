# Game compatibility

[README](README.md) · [Roadmap](ROADMAP.md) · [Halo level details](docs/halo-ce.md)

Updated **September 18, 2026** for **0.2.0-test.2**. Halo CE is the only title
with physical Vita gameplay confirmed.
Each game needs a separate port; Xita does not run arbitrary Xbox executables.

## Titles

| Game | Tested executable | Real Vita | Vita3K |
| --- | --- | --- | --- |
| Halo: Combat Evolved (Xbox) | Tested 3925-era executable; alternate versions unvalidated | **Ingame** — performance and GPU stability remain open | **Ingame** — useful for rendering and behavior checks |
| Halo 2 (Xbox) | Experimental 5849 profile | **Hardware testing pending** — bundled runtime selectable from Xita | **Experimental startup/rendering** — intro and rendering milestones; not general gameplay validation |
| Other original Xbox titles | None | **Untested** | **Untested** |

**Ingame** means gameplay is reachable with known problems. **Playable** is
reserved for representative play at acceptable speed and stability, including
completion where applicable. No title currently meets that release gate.

## Halo: tested features

| Feature | Evidence and limits |
| --- | --- |
| Main menu, profiles and dashboard | Existing profiles and solo Split Screen work on hardware. Fresh-install Default/Inverted failures are reproduced and corrected in host/emulator checks; [physical confirmation pending](docs/builtin-profiles-20260909.md). |
| Blood Gulch | Latest user report: around **12 FPS across the valley**, with **20 FPS** in some caves/bases. These are scene observations, not a matched build comparison. Historical September 7 sampled hardware session: **11.00 FPS at 640×360**, 256 texture cap. User confirms driving, shooting and looking around. Sixty-frame averages span **8.1–13.0 FPS** after the initial loading window. |
| Battle Creek | Tested on hardware and emulator; see the earlier [480p report](docs/hardware-20260905-battlecreek-480p.md). No matched test of the latest candidate yet. |
| Vehicles and variants | User confirms Warthog, Ghost and Scorpion operation. Driving performance and long-run stability need further testing. |
| Rendering | User confirms major improvements to sky, decals, baked lighting and active camouflage. Whole-game rendering correctness is not established. |
| External tester follow-up | September 9: flashlight bug, one stretched shadow, and a jump when leaving stick center. Exact build/settings unknown; [reproduction and performance review](docs/tester-feedback-20260909.md). |
| Campaign | The Pillar of Autumn has been played through the Keyes section in prior builds. AI, cinematic camera recovery and later missions need further hardware validation. |
| Checkpoints and resume | In-game Revert to Last Save is user-confirmed. A later menu-resume correction passes emulator restart testing; hardware menu-resume remains unverified. |
| GPU stability | Recent user sessions report crashes resolved. Long sessions, weapon effects, rocket self-death and vehicles remain part of tester coverage; stability is not guaranteed. |
| Audio | In-game sounds work for the user; crackly main-menu music is reported. Page-read and output-buffer fixes pass host tests and were installed September 8; audible improvement on Vita is unverified. |
| Video | Bink intro/attract movies skipped. In-engine cinematics are separate. |
| Networking | Solo matches work. Ad-hoc/System Link between physical Vitas remains experimental and unverified. |

The first performance milestone is **sustained 20 FPS**, then 30 FPS. Neither
has been verified. Historical peaks around 20–22 FPS are scene-dependent, not
representative averages. Keep resolution, settings and workload fixed when
comparing changes. Emulator performance does not establish hardware speed.

The [September 9 gameplay candidate](docs/gameplay-build-20260909.md) removes
development tracing. Its host and 640 linked-ARM checks pass; hardware speed
and GPU stability remain unverified. The installed revision-2 LiveArea still
shows a blank/plain background; revision 3 awaits a VitaShell installation and
physical display check.

The [latest measured hardware report](docs/hardware-20260907-frame-constants.md)
covers the previous constant-buffer build. The [September 8 USB update](docs/hardware-20260908-weapon-menu.md)
installs dashboard triple-buffer control, the deferred visibility-read comparison,
and [weapon stencil, lobby and audio corrections](docs/weapon-menu-20260908.md).
Installation is verified; hardware gameplay, music quality and performance checks
for this update are pending. Emulator results do not establish hardware correctness.

## Supporting another game

The [modular profile/library foundation](docs/modular-architecture.md) is implemented
and locally validated for Halo 3925. It does not add another supported game.

Each title needs an executable/version profile, symbol identification,
compatible kernel/graphics/audio calls, shader translation and memory testing.
Halo-specific hooks must not be applied to other games. The future dashboard
game selector will choose installed, separately recompiled ports.

## Reporting a test

Include:

1. Xita build identifier and game/executable version.
2. Vita, PS TV or Vita3K; map, resolution and graphics settings.
3. Actions taken, time played, FPS and steps to reproduce any issue.
4. A short, reviewed diagnostic excerpt when useful.

Logs can contain shader definitions, memory details and local paths. Do not
attach game code, maps, firmware, SDK components, private saves or raw crash
dumps to a public report.

## Game selection and build identity

The dashboard's **Select Game** page offers Halo CE and **Halo 2 / Experimental**.
Both games launch from the same Xita installation. Halo 2 uses `ux0:data/xita-halo2/`, preserving CE's data and saves.
The selector does not convert or install a game. Missing installations are
reported before launch. [Halo 2 setup and limits](docs/halo2-hardware.md).

Report the version and revision shown on the dashboard/performance overlay,
map, graphics settings and reproduction steps. The dated reports below document
older builds; they do not supersede this release's [test status](docs/combined-games-20260918.md).
