# Remote testing on a Vita

Xita's optional LAN test service lets a maintainer see the running dashboard or
game, send controller input, collect logs, and repeat the existing off/on/off
benchmark without someone operating the Vita. USB can still supply power and
carry package transfers. Live commands use the same local Wi-Fi network as the
computer; VitaShell USB transfer mode is a separate workflow.

The service is disabled by default and requires a one-time setup with Xita
running in the foreground. The [integrated updater](integrated-updater.md) can
install compatible runtime builds into an inactive slot and restart Xita.
Changed packaged assets still require a VPK installation through VitaShell.
Campaign navigation and vehicles can be operated through the controls, but
there is no deterministic campaign/vehicle replay or recovery from a system
or GPU-driver freeze.

## One-time setup

Install the build containing this feature. Connect the Vita to the same trusted
LAN as the computer and obtain its IP address from the Vita's network settings.
Generate a private pairing outside the repository:

```sh
python3 tools/vita_remote.py pair /private/path/vita-pairing --host 192.168.1.50
```

Replace that example address with the Vita's address. Copy only `remote.key`
from that directory to `ux0:data/xita/remote.key`. Keep `remote-client.json` on
the computer. Add these settings to `ux0:data/xita/xita.cfg` and restart Xita:

```ini
XV_REMOTE_TEST=1
XV_REMOTE_PORT=8080
XV_NET_ADHOC=0
```

The existing ad hoc transport owns the networking stack in ad hoc mode, so this
service refuses to start alongside it. Normal solo gameplay still works.
The dashboard can remain enabled; its controls and screen are also accessible.
No plugin, FTP server or forwarding of a port on the router is required.

Pairing uses a randomly generated 128-bit key. Requests require that key, and the
service has no arbitrary upload, file-write or shell endpoint. HTTP is unencrypted
and intended for a trusted private LAN only. Keep `remote.key` and
`remote-client.json` out of logs, Git and shared tester archives. The normal
configuration logger never sees the key because it is a separate file.

## Operating it

Every command below uses the same private pairing:

```sh
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json status
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json screen /private/path/dashboard.ppm
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json pad cross
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json pad --rx 180 --duration 1
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json log /private/path/run.log
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json launcher-log /private/path/launcher.log
```

Supported buttons are `select`, `start`, directions, `l`, `r`, `triangle`,
`circle`, `cross`, and `square`. Multiple button names form a chord. Sticks
accept 0–255 with 128 centered. A `pad` command holds for 0.45 seconds by default;
longer holds renew short input leases. Every lease expires within two seconds,
even if the computer disconnects. Physical button or stick input takes priority.
`release` sends a neutral state immediately. These are normal controller events;
they do not patch the game's memory or skip its menus.

Screenshots use PPM without additional Python packages. A `.png` destination
uses Pillow on the computer. Each screenshot copies one completed framebuffer
before its display slot can be reused. The service never reads a GPU buffer in
flight and adds no `sceGxmFinish` calls. It refuses screenshots and bulk log
reads during a measured benchmark. Status reads remain small and available.

To keep the display active and prevent automatic suspend for up to an hour:

```sh
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json lease 3600
```

Renew the lease during a longer session; `lease 0` stops renewing the idle timer.
While the lease is active, the service also renews the display dimming/off timers.
Leave the Vita charging for unattended tests. Set `XV_REMOTE_TEST=0` and restart
to disable the service entirely.

## Repeated performance measurements

Navigate to a loaded first-person scene using screenshots and remote controls,
stop moving, then run:

```sh
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json benchmark /private/path/test-001 --runs 3
```

This starts each trial with the existing **L + R + Square** chord and waits for
its completion. It captures before/after screens and complete logs between
trials, and saves `result.json`. The enabled benchmark selector determines the
candidate; see [native math comparisons](native-math-benchmark-20260913.md).
The script checks for one new result per trial and a passing camera check.
Timeouts, cancelled tests, missing results and moved cameras are failures, not
successful measurements. Partial evidence is retained if a connection fails.

New builds also accept an explicit test without changing `xita.cfg`:

```sh
python3 tools/vita_remote.py --config /private/path/vita-pairing/remote-client.json benchmark /private/path/palette-001 --kind model-palette --runs 3
```

Available names are `object-basis`, `model-palette`, `vertex-worker`,
`vertex-references`, `native-bounds`, `vertex-copy`, `draw-scan`, `flare`,
`resolution`, `early-visibility`, `point-math`, `texture-state`, `matrix-neon`,
`object-scan`, `hle-dispatch`, `flare-query-overlap`, `guest-affinity`,
`snapshot-worker`, `guest-phases`, and `prep-bundle`. Resolution runs 544/360/544;
the others run off/on/off. Each restores
the prior override and resolution. The network thread only publishes a request;
the guest input owner checks first-person control and starts it at the existing
frame boundary. Menus, overlapping tests, incomplete uploads and pending updates
reject new requests. A native helper must be compiled into that build to select
it. The runner also verifies that the result belongs to the requested test.

`guest-phases` is a [bounded profiling capture](guest-phase-timing-20260912.md#bounded-remote-capture-september-14-follow-up),
with timing on only in the middle arm. It requires compiled phase scopes and
configured timing off. The result is marked diagnostic: its FPS values measure
profiling overhead, not an optimization gain. Already-open parent functions are
absent from the captured child timings. No saved settings are edited.

`texture-state` compares uncached and [cached mesh texture bindings](texture-state-cache-20260914.md).
The cache defaults off; the comparison preserves draw order and invalidates
state at clears, UI replay and scene transitions.

`point-math` compares the original and [native point-transform routine](native-point-transform-20260914.md)
at the current resolution, with other math and workers unchanged.

`early-visibility` is an experiment, disabled outside its comparison. It
publishes exact visibility results at the world scene's fragment fence when
an upscale follows. Geometry, textures, UI and display ownership still wait
for their ordinary final completion. Native 544p and frames without queries
use the existing fence. See the [September 14 rendering results](hardware-20260914-rendering.md).

`launcher-log` downloads only `ux0:data/xita/update/launcher.log` through the same
authenticated, bounded log reader. It cannot select arbitrary paths. Both log
endpoints and screen/update operations are excluded while a test is pending or
running. This permits diagnosing a failed update after the old build returns,
provided that old build already contains the launcher-log endpoint.

Keep graphics settings, clocks, phase instrumentation and remote status polling
the same in every arm. Results still reflect live game simulation; repeat trials
and examine their logs rather than treating one stationary view as campaign FPS.
The default timeout is 180 seconds per trial. The service adds about 3 MiB of
buffers while enabled and a sleeping network thread, so it is not a claim of
zero instrumentation overhead. No socket operations run on the guest or render
threads. Captures and downloads have costs and belong outside timed intervals.

## Validation and limits

The [integrated updater](integrated-updater.md) adds compatible executable
uploads, an Update dashboard page, two-slot rollback and post-restart build
identification. It requires one new VPK installation; the original remote-test
build below cannot receive executable uploads. Its protocol remains private
and opt-in, with fixed update paths and no writes to game data or settings.

`python3 tools/test_remote.py` exercises the production server through actual
host loopback sockets. `SANITIZE=1` adds ASan/UBSan. Cases include disabled/missing
credentials, authentication and malformed framing, bounded values, input expiry,
physical priority, screenshot colors and lifetime, benchmark transfer exclusion,
log chunks, capture timeout recovery, pairing permissions and the Python client.
`make -C recomp/host test-input` also checks that remote chords pass through the
real Vita input/benchmark path.

The native build passes. In an isolated Vita3K run, authenticated network controls
launched the dashboard, navigated the normal solo multiplayer menus and loaded
Blood Gulch. The Python runner completed three object-basis off/on/off trials,
with all camera checks passing and the configured helper restored each time.
It downloaded the logs and before/after screens without local keyboard input.
Actual requests for a screenshot or log during measurement returned HTTP 409.
Remote walking, turning and firing also responded after the benchmark.

This was a 20 FPS capped emulator functionality check, not evidence of Vita
performance or test-service overhead. The subsequent
[physical Vita run](hardware-20260913-remote.md) confirms connectivity, launch,
menus, stick controls, captures, log downloads and three automatic comparisons.
Long-running idle behavior and isolated test-service overhead remain unmeasured.
The emulator was stopped and its prior executable/settings restored afterward.

The private candidate is `xita-remote-test-20260913-6eff881.vpk`:

- EBOOT SHA-256: `6eff881f24081a8de6f3bb843bf61622b9e8d25e78f5a52f79b2d631bf3a2f46`
- VPK SHA-256: `80bcdb2e6308af1f17dc1d35e4e33371cd4305150d6a869f97065787dd0b2444`

Archive integrity passes; only `eboot.bin` differs from the preceding native-math
benchmark package. Validation receipts and captures are private under
`2026-09-13-worker-sizing/validation/remote-test`. The VPK is also copied to
`ux0:VPK/xita-remote-test-20260913-6eff881.vpk`, with a private pairing key and
remote-test settings prepared. Package, key and configuration hashes were checked
again after a read-only USB remount, then the volume was safely unmounted.
The user subsequently installed the package and supplied the Wi-Fi address;
the running service's build stamp matches this candidate. Existing graphics and
benchmark settings and the prior configuration backup are preserved. No fresh
installed executable hash was read over Wi-Fi. Integrated installation remains
a separate development task.

Platform references: [VitaSDK network initialization sample](https://github.com/vitasdk/samples/blob/master/net_http/src/main.c),
[socket API](https://docs.vitasdk.org/group__SceNetUser.html), and the local SDK's
`psp2/kernel/processmgr.h` automatic-suspend tick contract. The transport supplies
explicit peer-address storage for accept, matching both firmware and Vita3K's
[socket implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/net/src/posixsocket.cpp).

The `prep-bundle` comparison enables native matrix NEON, empty-object scanning,
and texture-binding reuse together in its middle arm. It holds resolution and
the existing indexed-vertex, upload-worker and flare settings constant. The
outer arms disable only those three candidates; completion, cancellation and
lost view restore each member’s own configured value. Both native helpers must
be compiled and native math must be enabled. This tests the combined change;
it does not attribute any gain to an individual member. See the
[bundle experiment](optimization-bundle-20260914.md).
