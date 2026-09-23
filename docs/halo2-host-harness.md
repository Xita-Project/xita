# Halo 2 headless host harness (x86 Linux and the Raspberry Pi 4 bench)

Status 2026-09-23: Halo 2 (XDK 5849) boots, reaches the title, is driven through the menus into a split-screen
Slayer match on Ivory Tower (cyclotron.map), plays it to the end and returns to the post-game menus, headless,
on x86-64 Linux and on the Raspberry Pi 4 (static armhf, pinned to cores 2-3). No strict stop in any run with
the current codegen: x86 15 min (x3), Pi 40 min (p1). Numbers here are structure and CPU split, never Vita
frame times: there is no GPU in this path and neither host is a Vita.

## What it is

Halo 2 has its own runtime (`games/halo2_5849`: `boot.c`, the host-channel NV2A pushbuffer consumer, the
pass/GXM backends, the audio worker with the GP DSP interpreter), so the Halo CE harness (`recomp/host`,
softgfx over xd3d) does not apply. The H2 harness compiles the stage's own units unchanged against:

- `games/halo2_5849/host/psp2_host.c`: every `sce*` call the target makes, on Linux. Memblocks = mmap,
  threads/semaphores/mutexes = pthreads, `sceIo` = POSIX with Vita paths verbatim, a 60 Hz vblank from the
  monotonic clock, a pad from `XV_PAD` / `XV_PAD_FILE`, an audio port paced at real time, and a **null GXM**:
  every call succeeds, the real `.gxp` parameter tables are parsed (FindParameterByName / GetResourceIndex /
  GetArraySize are exact), uniform writes land in scratch, scene notifications complete at once. The CPU side
  of the renderer (vertex fetch, texture locate/hash/decode/upload, render-target landing, uniform packing)
  runs in full; no pixels are produced, so GPU readbacks see cleared/uploaded data and visibility queries 0.
- `recomp/kernel/xk_os_host.c` instead of `xk_os_vita.c` (POSIX files, ucontext fibers); `runtime/xv_cpu.c`
  is dropped (nothing in the H2 target calls it).
- `games/halo2_5849/host/sampler.c`: the CE harness's in-process sampler (`XV_HOST_SAMPLE=<file>`), dumped
  every 60 flips from `sceDisplaySetFrameBuf`.
- `boot.c` change: the FPSCR inline asm is ARM-only; x86 gets `psp2_host.c`'s fenv emulation.

## Prerequisites (all private, never committed)

- A stage made by `prepare_boot.py` with the **current** recompiler. The mp292 codegen that mp328 links predates
  d0b7e63 (Sept 17 15:37), which made the recompiler emit stamped `X_W*` stores for the texture cache's
  "no write since last read" skip: with mp292 code guest stores stamp nothing, and the first guest-written
  texture (the attract movie, 75 s idle at the title) stops the run with
  `[h2/blocked] texture source changed without a tracked write address=01087000 bytes=1228800 fn=003FAC30`.
  The Vita3K lab never hit it only because its driver pressed Start before the attract movie.
  Stage used here: `private/h2host1` (codegen 15 s, image byte-identical to mp292's):

      venv=/home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/venv/bin/python3
      $venv games/halo2_5849/prepare_boot.py /home/birchwoodgod/games/halo2/default.xbe \
          --out ../private/h2host1/boot --host-channel --audio-host
      # build-args.json: mp328's with GENERATED/BUILD/IMAGE pointing at h2host1

- Run base `private/hostrun/` (x86) and `~/xita-h2/` (Pi), same shape: `app0-mp328/` (the stage VPK unpacked
  without eboot/sce_sys), `save-base/` (the Vita3K lab's save tree), `xita-base/env.txt` (the lab's
  `XV_MENU_*` knobs) and `xita-base/shaders/` (the lab's compiled `h2menu_*.gxp`), `game/` on the Pi (4.4 GB).
  The run script empties `save/cache4` per run as the lab's `preserve_fresh_cache.py` does: the game re-formats
  the n: partition every boot and the runtime refuses raw access to a populated one
  (`[h2/blocked] raw access to mounted/populated cache4 requires a general FATX driver`, then the boot loops in
  map-probe states 17-19 with no n: link).

## Build

    cd <H2 worktree>
    # x86-64 (~3 min at -j3)
    python3 tools/h2_host_build.py --stage ../private/h2host1 --out <objdir-x86> --jobs 3
    # Pi: static armhf, Vita CPU flags kept (-mthumb -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard), ~3 min
    python3 tools/h2_host_build.py --stage ../private/h2host1 --out <objdir-arm> --jobs 3 --static \
        --cc ~/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc

Units and flags come from the stage Makefile's `make -n -B` (feature defines, GUEST_OPT, the 23 `--wrap`s),
every compile runs under `nice -n 10`, objects rebuild from their `.d` files, `--runtime-only` compiles just
the non-generated units. The last Pi binary is kept as `private/hostrun/bin/harness-armhf-h2host1-fd74e13`.

## Run

    # x86: run dir private/hostrun/runs/<tag>/ (boot.log in ux0:data/xita-halo2/, stderr.log, driver.log)
    H2_DRIVE=1 H2_HOST_BIN=<objdir-x86>/harness tools/h2_host_run.sh <tag> 900 [K=V ...]
    # Pi (ssh alias pi): ~/xita-h2/runs/<tag>/, taskset -c 2,3 for the save copy, the driver and the harness,
    # so the Halo CE bench (~/xita, cores 0-1) runs at the same time. Builds on the Pi, if any: -j2, same pinning.
    H2_DRIVE=1 PI_HARNESS=<objdir-arm>/harness tools/h2_pi_run.sh <tag> 2400 [K=V ...]
    # profile: add XV_HOST_SAMPLE=samples.txt, then (static ARM binary: base 0x10000 from nm, cross tools)
    python3 tools/host_profile.py samples.txt <harness> --from-frame 2100 \
        --nm ~/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-nm

`H2_DRIVE=1` runs `tools/h2_menu_driver.py`: it waits for `[h2/menu-gxm] ready` (title up; the idle counter
that starts the attract movie at 75 s starts here), then presses Start, A, A, A, A through the save tree's
remembered highlights (profile Default, SPLIT SCREEN, lobby profile, START GAME), repeats A if no map load
follows, and reports LEVEL at cyclotron.map + in-match windows (180 clears per 60 flips). Waits count flips, so
the same plan works at x86 and Pi speed. Other knobs: `XV_PAD="<flip>:<btn>[*hold],t<sec>:<btn>..."`,
`XV_PAD_FILE`, `XV_HOST_STATUS`, `XV_HOST_SHOT=<n>` (PPM every n flips; mostly black: no GPU),
`XV_HOST_CONSOLE=1` (echo sceClibPrintf), `H2_HOST_KEEP_CACHE4=1`.

## Results

| | x86-64 (x3) | Pi 4, cores 2-3 (p1) |
|---|---|---|
| title (`menu-gxm ready`) | flip ~540, 18 s | flip ~540, 41 s |
| Start / START GAME pressed | 30 s / 53 s | 99 s / 260 s |
| in match (cyclotron) | flip 2230, 65 s | flip 1980, 323 s |
| in-match rate | 37-60 flips/s (vblank-capped) | 8.8-9.1 flips/s |
| match end -> post-game menus | flip ~15300 | flip ~9100 (~13 min in match) |
| run length, stops | 15 min, 0 | 40 min, 0 (no crash, no hang) |

In-match 60-flip window on the Pi (serial 4140, 6.7 s wall): methods 2.27 M, clears 180, GXM draws 12,240
(the lab's Vita3K window: 2.27 M, 180, 12,240 - same workload); submit 3.7 s, of which the GXM CPU path
2.76 s (texture acquire/hash 1.45 s over 154 MB hashed, flush 0.18, open 0.13); pass consumers 3.56 s;
flip wait 0.5 s. Audio: 137 grains in 6.7 s = 44% of real time, compute 6.45 s, GP DSP 103.6 M instructions
(~60 ns each on the A72), every grain misses its deadline; the audio thread holds a core at ~96%, the owner
(guest + consumer) ~75-80%.

The audio worker keeps one grain outstanding by design (polls GetRestSample until the port drains, then mixes
and submits), so every submit meets an empty port; `[host] ... empty_submits` counts that gap, not underruns.

## CPU profile (Pi, static armhf, in-match)

Run p2 (`XV_HOST_SAMPLE=samples.txt`, 93 in-match 60-flip windows from flip 2100; samples kept in
`private/hostrun/pi-runs/p2/`). ITIMER_PROF delivery on the Pi under-samples one thread against the other, so
compare percentages within a thread, not across threads.

Owner thread (guest scheduler + host-channel consumer + GXM CPU path), 45,144 samples:

| share | where |
|---|---|
| ~27% | texture binds (`menu_texture_acquire`): three-colour DXT scan `block_needs_decode` 8.8% + `image_needs_decode` 4.5%, linear 512-entry key search (`memcmp`, ~6.8% incl. glibc internals), `content_hash` 2.4%, `menu_texture_acquire` 2.5%, `get_texture` 1.7% |
| ~11% | vertex fetch for GXM draws: `decode_attr` 9.1%, `rd_u32` 1.8% |
| 6.4% | `hash_bytes`: the DSP engine's full-state FNV hash, on the owner |
| 5.1% / 2.9% / 1.2% | `render_body` (menu_gxm per-draw), `geometry_method`, `clear_surface` |
| 4.8% | `__udivmoddi4` (64-bit division; ARM32 has none): `software_flip`, `h2_nv2a_advance_us`, `x_div_32` hold most call sites |
| 3.7% | `memcpy` |
| 4.0% / 1.8% | guest runtime `x_guest_checked_pointer(_write)`, the per-function `xv_trace_func` hook |
| 4.6% | all recompiled guest code (`f_*`) together; top single function 0.3% |

Other threads, 104,321 samples: the GP DSP56300 interpreter (`dsp56k_execute_instruction` 14.0%, `frame`
7.9%, `emu_pm_5` 6.9%, `dsp_postexecute_update_pc` 6.7%, `emu_calc_ea` 6.2%, `read_memory_p` 5.7%, ...), HRTF
0.9%. It needs ~2.3x one A72 core for real-time audio (see Results).

x86 (x4, dynamic PIE; for comparison): libc mem* 17.5%, `image_needs_decode` 14.8%, `decode_attr` 7.9%,
`hash_bytes` 6.0%, `block_needs_decode` 5.1%, `clear_surface` 4.7%, `content_hash` 3.4%.

Leads (not changed here; renderer/audio behaviour needs a Vita check):
1. `menu_texture_acquire` runs `image_needs_decode` (a scan of every DXT3/5 block) **before** the page-epoch
   cache check, so every bind of a DXT3/5 texture re-reads its whole source even when the stamps prove it
   unchanged. Keep the verdict in the cache entry and recompute it only on a re-hash.
2. The cache entry is found by a linear `memcmp` over all 512 entries per bind (~35k binds per 60 flips): a
   small hash index on the key removes it.
3. `h2_audio_backend_fixed_commit_ready` (called by guest code) runs `h2_dsp_snapshot`, which FNV-hashes all
   ~45 KB of DSP memory under both audio locks only to read `state.fault`; read the fault flag directly.
4. 64-bit divisions in per-flip/per-access paths (`h2_nv2a_advance_us`, `software_flip`) cost ~5% on ARM32.
5. Audio: the DSP interpreter is the audio wall on ARM (~60 ns per GP instruction, 44% of real time on one A72
   core); the Vita's A9 at 444 MHz is slower still.

## Open items

- No current stop or hang: x86 x3 (15 min), Pi p1 (40 min, the first 7 minutes alongside the CE soak on cores
  0-1) and p2 (15 min, profiled) all ran title -> match -> post-game menus with 0 `[h2/blocked]` lines.
  For a hang on the Pi: `ssh pi`, `pgrep -x harness`, `gdb -p <pid>` (runtime units carry -g; guest code is -g0,
  `f_XXXXXXXX` frames still name the guest function).
- Only the split-screen Slayer path is driven (the lab save's remembered highlights). A campaign plan needs a
  different driver sequence and the campaign maps (already on the Pi under ~/xita-h2/game).
- The null GXM hides GPU cost entirely and returns empty visibility queries/readbacks; game logic that depends
  on those (occlusion-driven effects) takes its "nothing visible" branch.
- Stage `private/h2host1` carries the current codegen; a Vita VPK of it was not built here (the harness only
  needs the objects; `app0-mp328/` supplies the shaders and contracts).
