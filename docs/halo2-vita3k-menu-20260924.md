# Halo 2: main menu in Vita3K from a package with no lab settings (Sept 24 2026)

Branch `work/halo2-vita3k-20260924` (worktree
`~/xita-backups/2026-09-24-halo2-vita3k/source`). It was cut from the unified source at `ea21f84`
and merges the Halo 2 host-harness branch (`06922d2`). This work did not change Halo CE's code
path, did not touch Codex's stage, and did not use the physical Vita.

## Result

Halo 2 boots through its normal XBE startup in Vita3K. It plays the intro and shows the game's own
title ("PRESS START TO CONTINUE"), CHOOSE PROFILE and main menu (CAMPAIGN / XBOX LIVE / SPLIT SCREEN
/ SYSTEM LINK / SETTINGS) over the animated city backdrop. The D-pad moves the highlight. A opens
the highlighted entry (SETTINGS screen, CAMPAIGN OPTIONS pop-up) and B goes back. Every screen comes
from the game's own UI code, drawn by the NV2A pushbuffer consumer and the GXM menu backend. None
is a replacement screen.

| run | build | settings | outcome |
|---|---|---|---|
| `h2v1-c1`..`c3` | `6f0551d` standalone | lab `env.txt` + lab shaders | title -> profile -> menu, Up/Down; 0 error lines |
| `h2v1r-n2`..`n4` | `ea40725` `MENU_RUNTIME_DEFAULTS=1` | **no env.txt, no ux0 shaders** (whole `ux0:data/xita` moved aside) | title -> profile -> menu, Up/Down, A/B (n3, n4); 0 error lines |
| `h2v1-noenv` | `6f0551d`, historical defaults | no env.txt | black screen, never reaches the title |
| `h2v1-gxmonly` | `6f0551d`, historical defaults | env.txt = `XV_MENU_GXM=1` only | intro runs, then stalls at `[h2/menu-gxm] ready` (below) |

"0 error lines" means no `h2/blocked`, strict stop, fatal, abort or exception in `boot.log`. The
two failing runs also report 0: their stall is silent, so check the screenshots and not only that
count.

Each run takes 112-130 s: about 80 s to the title, then the key presses. The emulator draws about 5 FPS in
software GL. That is an emulator/host number, not a Vita number.

## First blocker, and the fix

1. **The build was broken on the current source.** Shared recompiler/runtime changes made for CE
   broke the Halo 2 target:
   - Compile: the generated `xv_recomp_protos.h` used `XV_HLE_PROXY` without defining it when
     object jobs are off. Fixed in `c3099c3`: same macro text, defined before the `#ifdef`, so CE's
     expansion is unchanged.
   - Link: about 5,000 undefined `xv_hle_timing` / `xv_hle_time_add` / ... references. These are
     CE's instrumentation, defined in CE's `xd3d.c`, which Halo 2 does not link. Fixed in `6f0551d`
     (`games/halo2_5849/hle_timing.c`, timing off).
2. **The menu depended on the lab's env.txt.** With the historical defaults, the intro runs, then
   `[h2/menu-gxm] ready`, then a `[xk] yield storm` and no further frames:
   - Thread 8 spins in `f_0012B450`, called from `f_0022324C`, waiting for the engine frame counter
     at `0x485AB0`.
   - Only the flip-completion vblank advances that counter, and the menu submits no flips.
   - `XV_MENU_VBLANK=1` supplies the free-running vblank (`xd3d_vblank_kick` /
     `vblank_pace_in_render` in `host_channel_runtime.c`). It also routes the menu audio stream's
     unsupported Process call to a DSERR the game handles, where the historical default is a hard
     `fail()`.

   The lab set this in `ux0:data/xita/env.txt`, so a package installed anywhere else would stall at
   the menu. `ea40725` adds `MENU_RUNTIME_DEFAULTS=1` (Halo 2 Makefile only): it compiles in
   `XV_MENU_VBLANK=1 XV_MENU_YIELD=0 XV_MENU_THREADS=3` as defaults, and env.txt still overrides
   each knob. The default remains 0, so builds without it keep their historical values.
   `XV_MENU_DETAIL` (lab 300) only controls how many draws get logged. `XV_SPIN_BT` is diagnostic.

## Reproduce

Paths: `S=~/xita-backups/2026-09-24-halo2-vita3k/source`, `P=.../private` (never committed),
`LAB=~/xita-backups/2026-09-12-halo2-initial-profile/private` (shared H2 Vita3K lab, display :111).

```sh
# codegen (~15 s): 21,110 functions
cd $S && $LAB/venv/bin/python games/halo2_5849/prepare_boot.py ~/games/halo2/default.xbe \
    --out $P/<stage>/boot --host-channel --audio-host
# build: mp328's frozen flags + BUILD_REVISION=<HEAD> + extras (private/build_stage.py)
python3 $P/build_stage.py h2v1r MENU_RUNTIME_DEFAULTS=1 MENU_GXM_DEFAULT=1 \
    MENU_SHADERS=$LAB/vita3k/ux0/data/xita/shaders           # standalone hardware candidate
python3 $P/build_stage.py h2v1b BUNDLED=1 MENU_RUNTIME_DEFAULTS=1 MENU_GXM_DEFAULT=1 \
    MENU_SHADERS=$LAB/vita3k/ux0/data/xita/shaders           # for the combined package
# private X display for Vita3K (never the desktop): Xvfb unpacked from xorg-server-xvfb-21.1.24-1
$P/xvfb/usr/bin/Xvfb :111 -screen 0 1280x800x24 -nolisten tcp &
# Vita3K cold launch (vkey.py/vk drive only :111)
sh $P/noenv_run2.sh h2v1r <label>   # ux0:data/xita moved aside and restored; screenshots + logs in $P/runs/<label>
sh $P/strip.sh $P/runs/<label>      # contact sheet strip.png
# game selector in Vita3K (a CE build without XV_THREAD_PAGE_TABLE; see below)
sh $P/selector_up.sh <combined.vpk> <label>; $P/vk key|combo ...; sh $P/selector_down.sh <label>
# harness: x86 and Pi (bare = package files only)
python3 tools/h2_host_build.py --stage $P/h2v1r --out $P/objs-x86-h2v1r
python3 tools/h2_host_build.py --stage $P/h2v1r --out $P/objs-arm-h2v1r --static \
    --cc ~/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc
H2_HOST_BASE=$P/hostrun H2_HOST_APP0=$P/hostrun/app0-h2v1r H2_HOST_BARE=1 H2_DRIVE=1 \
    H2_HOST_BIN=$P/objs-x86-h2v1r/harness sh tools/h2_host_run.sh <tag> 600
PI_APP0=app0-h2v1r PI_HARNESS=$P/objs-arm-h2v1r/harness H2_HOST_BARE=1 H2_DRIVE=1 \
    sh tools/h2_pi_run.sh <tag> 1200
```

`MENU_SHADERS` packs the 120 offline-compiled `h2menu_*.gxp` into the VPK (`app0:`). Without it,
the GXM menu backend has no programs and every menu draw takes the much slower software path.
The lab installer `$LAB/run_lab.py` now installs the VPK's `h2menu_*.gxp` as well (a lab-script
fix, not committed; run `h2v1r-n1` predates it and is marked invalid).

## Pi / x86 harness (ARM correctness, not Vita timing)

Every run used `H2_DRIVE=1` (title -> profile -> SPLIT SCREEN -> lobby -> START GAME -> cyclotron
match) and null GXM. "Bare" (`H2_HOST_BARE=1`, `e153521`) means no env.txt and no shaders link:
package files only, like a fresh device.

| run | harness / app0 | settings | result |
|---|---|---|---|
| x86 `x1` | h2v1 (`6f0551d`) | lab env.txt | menus, in match at 64 s; 900 s, 49,320 flips, 0 error lines |
| Pi 4 `h2v1-p1` | h2v1, static armhf, cores 2-3 | lab env.txt | menus, in match at 288 s; 2,400 s, 22,840 flips, 0 error lines, no core |
| x86 `x2bare` | h2v1r (`ea40725`) | bare | menus, in match at 68 s; 600 s, 29,460 flips, 0 error lines |
| Pi 4 `h2v1r-p2bare` | h2v1r, static armhf, cores 2-3 | bare | menus, in match at 270 s; 1,200 s, 10,720 flips, 0 error lines, no core |

- In the bare runs, the menu-audio DSERR path (`[h2/audio-menu]`) runs from the compiled-in
  `XV_MENU_VBLANK` default, and every menu program loads from app0 (no "no compiled" lines).
- Run folders: `$P/hostrun/runs/<tag>` (x86) and `~/xita-h2/runs/<tag>` on the Pi.
- The harness boot label reads "Vdevelopment / unknown" because it does not include the Vita build
  header; the objdir (`$P/objs-{x86,arm}-<stage>`) identifies the build.
- The periodic `[xv/x86] preempt ... spin fn` dumps in the Pi `stderr.log` come from
  `XV_SPIN_BT=1` in the lab env.txt. They are diagnostic and appear only in the non-bare runs.
- No native replacements were added for Halo 2 in this work. The ARM-relevant changes are the
  `XV_HLE_TIMED` path with timing off, the `XV_HLE_PROXY` placement and the compiled-in menu
  defaults, and all of them run in these ARM runs.

## Game selector (combined package)

`tools/package_vpk.py` builds the combined package from a CE package plus a Halo 2 VPK built with
`BUNDLED=1`: `--root` is an unpacked CE package, `--eboot game-a.self`, `--sfo`, `--launcher eboot.bin`,
and `--halo2-package`.
- `sel1`: Codex's perf208c CE parts + Halo 2 `h2v1b`. Vita3K runs the launcher, which relaunches
  `app0:game-a.self --xita-slot=0`. CE then aborts in Vita3K on its first TPIDRURW write
  (`XV_THREAD_PAGE_TABLE=1`, `runtime/xv_thread_bind.c`; dynarmic: "Unhandled CP15 MCR c13,c0,2").
  This is an emulator limitation that affects every current CE hardware build, not Halo 2.
- `sel2`: a Vita3K-only CE stand-in. It is built from a private copy of Codex's build-x87 tree with
  `XV_THREAD_PAGE_TABLE=0` and this Halo 2 (`private/ce-vita3k`); never deploy it. The full chain works:
  1. The dashboard lists "HALO 2 / EXPERIMENTAL - INSTALLED / EXPERIMENTAL HARDWARE TEST".
  2. Select Game -> Halo 2 -> Launch relaunches `app0:eboot.bin --xita-game=halo2`, then
     `app0:halo2-a.self --xita-slot=0`.
  3. Halo 2 plays its intro, then title -> profile -> main menu. Up/Down, A (CAMPAIGN OPTIONS) and
     B work. The menu programs load from `app0:halo2/`, none missing, 0 error lines.
  4. Holding Start + Select logs `[h2/frontend] returning to Xita dashboard` and relaunches
     `game-a.self --xita-dashboard`; the dashboard comes back with Halo 2 still selected.

  Screenshots are in `private/runs/sel2` (`strip.png`). Halo 2's own settings come only from
  `ux0:data/xita-halo2/env.txt`, which the lab does not have, so this ran on the compiled-in defaults.

## Hardware candidate (not yet tested on a Vita)

Two private packages are built; neither has been installed or run on a Vita.
- **Combined (recommended):** `private/combined/xita-perf208c-h2ea40725.vpk`,
  sha256 `51f989b52c2c03d5a9e8daa5ea9de64c1390b3ae4115f25ba6de16123f3e6eff`.
  - It is Codex's `xita-perf208c.vpk` (sha256 `b2eefab0...`) with only `halo2-a.self` and
    `boot-halo2.txt` replaced. The Halo 2 runtime is `h2v1b` (build label `77d84a7`; its Halo 2 code is
    identical to `ea40725`), with `BUNDLED=1 MENU_GXM_DEFAULT=1 MENU_RUNTIME_DEFAULTS=1`.
  - CE's `game-a.self`, launcher, param.sfo, every asset and both update contracts are byte-identical
    to perf208c. On a Vita that has perf208c installed, this is therefore an executable-only Halo 2
    update:
    `vita_remote.py --config <remote-client.json> update <vpk> --game halo2 --apply`
    (see `docs/combined-games-20260918.md`).
  - If CE has moved on by then, rebuild against the installed CE package so the contracts match:
    unpack it, then run `package_vpk.py --root <unpacked> --eboot <unpacked>/game-a.self --sfo
    <unpacked>/sce_sys/param.sfo --launcher <unpacked>/eboot.bin --halo2-package
    private/h2v1b/build/halo2-boot.vpk`.
- **Standalone:** `private/h2v1r/build/halo2-boot.vpk` (XH2B00001, sha256 `a81eba77...`), a VitaShell
  install, which reads `ux0:data/xita/env.txt`. This is the build the n2-n4 Vita3K runs used.
- Before testing: delete or empty any leftover `ux0:data/xita-halo2/env.txt` (bundled mode) on the
  device. Its values override the compiled-in defaults.
- Report the build label (`[h2/boot] Xita ... / 77d84a7`), the screen reached and
  `ux0:data/xita-halo2/boot.log` (see `docs/halo2-hardware.md`).
- Not tested on hardware: speed, GPU behaviour, the Start+Select return on the real launcher, and
  the menu with real audio output.

## Limitations

- Vita3K: about 5 FPS in software GL. Inputs are sampled once per frame, so a press shorter than
  a frame can be missed and a 0.25 s hold can repeat once (runs c2/c3/n3 stepped twice on the last
  Down). Menu audio is silent, via the DSERR path.
- The small text blocks on the SETTINGS screen look like the game's decorative background text, but
  they were not compared with a reference capture.
- Neither Pi nor Vita3K results say anything about Vita hardware speed or GPU behaviour.
