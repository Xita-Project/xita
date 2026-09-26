# Xita Dashboard

The embedded dashboard now uses five main sections: Launch Game, Games, Settings,
Updates and About / License. Settings contains Graphics, Audio, Controls,
Display and Performance. Game selection, settings persistence, build version
and offline license reading remain available. Tester builds use the outbound
[release updater](../docs/tester-updates.md); developer builds retain paired
computer updates.

## Integrated startup dashboard

`make RECOMP=1` from the repository root now embeds a simple launcher in Xita
(`XITA00001`). Its prominent **Launch Game** button opens Halo's normal menu.
Graphics, Performance, Audio, Controls and Display pages edit the supported runtime settings:
texture detail (64/128/256/512 existing-mip cap), filtering (Game/Point/Linear),
mip smoothing (Auto/Off), render resolution (960x544, 848x480, 704x400 or 640x360),
material detail, lens-flare glow, cosmetic particles, impact-decal lifetime and budget,
20/25/30 FPS limits, 444/500 MHz CPU requests, extended compressed textures, master volume, look sensitivity,
deadzone, inverted look, response curve, touch shortcuts, FPS overlay and CPU meter.

The integrated game also has an [in-game graphics panel](../docs/in-game-settings.md),
opened with **Select + Circle**. It shares the dashboard's settings and marks
options that require relaunching Xita.

The September 8 local candidate adds **Triple buffering** in Graphics and an
**About / License** page. Triple buffering defaults off, saves `XV_TRIPLE_BUFFER`,
and can also change through the in-game panel. On permits frame preparation ahead
of the GPU using the existing three slots; a slot still cannot be reused before
GPU completion. It is experimental, may add input delay, and has no verified
hardware FPS benefit yet.

About / License gives a short GPL-3.0-only summary. Cross opens the full notice
and GPL, Up/Down scrolls, Left/Right changes pages, and Circle goes back.
`tools/embed_license.py` generates the embedded text from the actual root
`LICENSE` and `NOTICE`; both files and third-party notices accompany the VPK.
The new quality controls default to Original, decal limits and frame caps to Off,
and extended compression to Off. 500 MHz requires a supporting overclock plugin;
the runtime falls back to a 444 MHz request when 500 is rejected or not reported.
See [settings and implementation details](../docs/quality-options-20260906.md).

Cross enters/selects, Up/Down navigates, Left/Right changes a setting, and Circle
returns to the sidebar. Controller navigation is supported in this embedded mode.
Graphics contains twelve visual controls, including effects, frame limit,
triple buffering and
extended compression. Five rows are visible at once; Up/Down scrolls the list,
with a position indicator and scrollbar. CPU clock remains under Performance;
the FPS overlay and CPU meter remain under Display. Existing setting values are
preserved when moving between sections.
The consolidated menu passed host navigation/persistence checks and an isolated
Vita3K launch check, then was installed over USB September 6 at 14:29–14:30 CDT.
Direct read and a fresh read-only mount verified the executable and all 657
other checked files unchanged. That earlier update changed menu organization only;
the September 8 additions are a separate local candidate.

Each edit saves only its own key. Unknown keys, other diagnostic settings, duplicate
keys, whitespace and comments survive. A failed save rolls the edited value back.
The runtime reloads settings before starting the game threads; `xita.cfg` takes
precedence over the `env.txt` fallback. No game save is touched by the dashboard.
`XV_DASHBOARD=0` is available for automated runs that need direct boot.

This mode draws into a temporary cached CPU canvas (about 2 MiB), then copies the
completed frame into the existing inactive CDRAM display buffer before any GPU scene
or guest/render worker is started. Each CPU-rendered frame waits for the display
switch before reusing the former front buffer. The final front/back indices carry
into GXM rendering. Dashboard state is freed on return; there is no dashboard
worker or dashboard rendering during gameplay. The cached canvas is freed before
game startup. Drawing directly into uncached display memory caused the reported
3 fps hardware dashboard; the September 5 hardware log measures 19.7–19.9 fps
with cached drawing. See the [hardware report](../docs/hardware-20260905-bloodgulch-480p.md).

The API enables this mode with `xv_dash_config.simple_launcher = 1`. Its result
has `game_id = "haloce"`, an empty map and no save selection. It checks the game
image and UI map under the configured data root before allowing launch. The
content-browser API and standalone producer below remain separate legacy modes;
the integrated dashboard does not promise map/save hand-off or checkpoint repair.

Host tests cover one-button launch, reopening persisted values, selective edits,
write failure, blocked launch and framebuffer padding. Emulator checks cover
navigation, saved settings reaching game startup, and the transition to Halo.
Dashboard scanout and input work on hardware. The cached drawing change and
expanded graphics settings pass host and emulator checks; the latest hardware
run confirms the faster dashboard and 480p/Low settings reaching gameplay.

## Standalone prototype

A native, software-rendered first dashboard milestone: an animated green home
screen, installed Halo: Combat Evolved maps and saves, a disabled Halo 2 entry,
settings, the default control mapping, and project credits. No game assets or
recompiled game code are included. The original bitmap font is embedded in
`font.h`; `python3 gen_font.py` regenerates it from the original glyph patterns.
Lowercase input is displayed with uppercase glyphs.

Build from the repository root:

```sh
export VITASDK=$HOME/vitasdk PATH=$HOME/vitasdk/bin:$PATH
make -C dashboard
make -C dashboard host-test
```

Install `dashboard/xita_dash.vpk` with VitaShell. Its title is **Xita Dashboard**,
its title ID is **XITADASH1**, and its icon is the repository's `sce_sys/icon0.png`.
The host target needs GCC and libm, but no VitaSDK. It writes three 960x544 P6
images to `dashboard/out/frame1.ppm` through `frame3.ppm`. Temporary empty files
simulate content presence for tests; they contain no game data and are deleted
on success. Tests also exercise launch results, blocked launches, settings
round-tripping, callback errors, navigation and framebuffer padding.

Cross confirms; Circle or Left goes back. Up/Down and the left stick move the
selection, with a deadzone and delayed repeat. Right also confirms. Touching a
left-hand menu pill selects its page; Cross enters it. Touching a content row
selects it; Cross confirms it. Lists scroll in seven-row pages as the selection
moves. Rear touch and the right stick are carried by the input contract for
future controls, but have no dashboard action yet.

Halo is launchable only when both `ux0:data/xita/halo_image.bin` and
`ux0:data/xita/haloce/maps/ui.map` exist as regular files. The ten known campaign
maps and thirteen known multiplayer maps are listed only if their `.map` files
exist. The Saves page lists regular filenames directly under
`ux0:data/xita/save/`, sorted by name; directories are not traversed. Long names
are shortened only for display. Names containing line breaks or longer than
255 bytes cannot be represented in the launch contract and are not offered.
Discovery checks presence, not map validity or Xbox build compatibility.

The Settings page immediately saves each confirmed edit to `xita.cfg`, using a
sibling temporary file and rename. It preserves unknown keys, comment lines,
inline comments, and existing whitespace; it updates all occurrences of a
known key and appends missing known keys. Failed writes are shown on screen and
the edited value is restored. Defaults match the current runtime: FPS, BC mip
chains and profiler off; vblank counter 60 Hz. Rates cycle through 60, 120, 250,
500 and 1000. Other settings remain available through the existing config file.

## Embedding in the runtime

Compile `xv_dash.c` into the caller and link libm. Include `xv_dash.h`; the only
exported function is:

```c
int xv_dash_run(const xv_dash_config *cfg, xv_dash_result *out);
```

The caller owns the framebuffer throughout the call and after return. Supply a
writable ABGR8888 buffer (32-bit words `0xAABBGGRR`), dimensions at least 960x544,
and **pitch in pixels**, at least the width. The UI occupies a 960x544 canvas at
the top left; the entire visible surface is cleared, and pitch padding is
untouched. The buffer must hold `pitch * height` words. Dimensions are limited
to 4096 and pitch to 8192. No display or GXM initialization, sceCtrl calls, or
Vita-specific dependencies occur in the module.

Two synchronous callbacks receive `cfg.userdata`:

- `poll(userdata, input)` fills a zero-initialized button/stick/touch sample.
  Button flags describe held buttons, sticks use signed -128..127 coordinates,
  and touch points use dashboard pixels. Return 0 for input, a positive value
  to cancel, or a negative value for failure.
- `present(userdata, framebuffer)` presents the completed frame and paces the
  loop at 60 Hz. It can replace `framebuffer->pixels` with the next writable
  caller-owned buffer of the same dimensions/pitch. The next buffer must be
  safe for CPU writes when the callback returns. Return 0 on success, negative
  on failure. A GXM caller is responsible for synchronizing CPU writes and
  scanout/GPU access. Animation advances by a fixed 1/60 second per frame.

`data_root = NULL` selects `ux0:data/xita`; host tests can override it. The
module allocates its own state and save-name list once during discovery (no
per-frame allocation), precomputes mesh segments, and frees all state on every
exit. Only immutable tables have static storage. The function returns 0 for a
selection, 1 for caller cancellation, or -1 on error. `out` is cleared on
entry. Successful results use `game_id = "haloce"` and one of campaign,
multiplayer or settings. `map` is a map stem, a save filename, or empty for
settings; `is_save` distinguishes a save from a campaign map. The global
Settings page edits dashboard settings; **Game settings** in Halo's submenu
returns a settings-mode hand-off to the caller.

After the function returns, the runtime may reclaim its framebuffer and pass
the selection into its own engine startup. This module never retains the
configuration, callbacks or framebuffer beyond the call.

## Standalone hand-off

The wrapper allocates two 960x544 CDRAM buffers, with 256 KiB aligned bases and
allocation lengths (2 MiB each). It uses `sceDisplaySetFrameBuf` followed by
`sceDisplayWaitVblankStart` before making the former scanout buffer writable.
It samples `sceCtrlPeekBufferPositive` in `SCE_CTRL_MODE_ANALOG_WIDE` mode and
both panels with `sceTouchPeek`, scaling each panel's reported active area into
the 960x544 canvas.

On selection it writes `ux0:data/xita/launch.cfg` through a temporary file:

```ini
GAME=haloce
MODE=campaign
MAP=a10
IS_SAVE=0
```

`MODE` is `campaign`, `multiplayer`, or `settings`. `MAP` contains the selected
map stem or save filename; it is empty for settings. `IS_SAVE` is an additional
0/1 key preserving the library result's distinction for saves. It then calls
**`sceAppMgrLaunchAppByUri(0x20000, "psgm:play?titleid=XITA00001")`**. Both this
function and `sceAppMgrLaunchAppByName2` exist in the installed VitaSDK's
`psp2/appmgr.h`; the URI function was selected, using the documented flag that
launches the app rather than opening LiveArea. Failure writes
`ux0:data/xita/dashboard.log` and shows a red screen for three seconds.

The runtime must already be installed. The current runtime does **not** read
`launch.cfg`: this milestone supplies the producer and launch request, but
honoring a selected map/save/mode requires a later runtime integration. The
standalone launcher cannot make the current runtime consume that selection.
The integrated startup mode above launches the normal game menu directly.

Real hardware still needs to verify scanout timing/tearing, software rendering
cost at 60 Hz, stick repeat and touch calibration on both panels, memory-card
config replacement/error handling, app-manager hand-off and display cleanup.
Actual map/save startup additionally needs the runtime consumer described above.

## Game selection and version

The embedded dashboard offers **Select Game**. Select Halo CE or Halo 2, then
return to **Launch Game**. The choice is saved in `selected-game.txt`; settings
and game saves are not moved. Halo 2 requires its separate experimental VPK and
owned data: [setup](../docs/halo2-hardware.md). CE graphics settings currently
apply to CE only. Close Halo 2 and reopen Xita to switch back.

The footer and CE performance overlay show `V0.2.0-test.1 / <revision>`.
A `+` after the revision indicates a build from modified tracked source. The
remote status endpoint reports the same version and revision. Include these
values with screenshots and logs. Version metadata comes from `version.json`
and `tools/gen_build_version.py` and is embedded during compilation.
