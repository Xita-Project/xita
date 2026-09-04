# Xita AdHoc Test

Standalone C diagnostic for two homebrew PS Vitas. Contains no game code or data.
Builds `xita_adhoctest.vpk`, title **Xita AdHoc Test**, title ID **XITAADH01**.
Hardware behavior has not been tested; a successful build is not evidence of a
working wireless connection.

## Build

From the repository root:

```sh
export VITASDK=$HOME/vitasdk PATH=$HOME/vitasdk/bin:$PATH
cd tools/adhoctest
make
```

`make clean` removes generated files. The Makefile follows `../shadercomp/Makefile`:
GCC, `-mthumb`, `-Wl,-q`, `vita-elf-create`, `vita-make-fself -s`,
`vita-mksfoex`, then `vita-pack-vpk`. It also enables C11 and treats warnings as
errors. The installed linker script supports `__sce_headroom`; reserving 64 KiB
prevents `vita-elf-create` import metadata from overlapping the data segment.
Only installed SDK stub archives are linked. NetCheck uses `SceCommonDialog_stub`;
there is no separate `SceNetCheckDialog_stub` archive in this SDK.

`sce_sys/` describes the package metadata. `vita-mksfoex` generates
`build/sce_sys/param.sfo`; the VPK contains it at `sce_sys/param.sfo` alongside
`eboot.bin`. No icon or LiveArea artwork is required.

## Run on two Vitas

1. Copy the VPK to each Vita and install with VitaShell. Enable Wi-Fi and disable
   airplane mode on both; keep the devices near each other.
2. Launch **Xita AdHoc Test** on both. The app initializes the stacks and requests
   PSP ad-hoc connection to group `XITA` with product ID `XITAADH01`. Allow the
   system connection dialog to complete. Cross is reserved for exit even during
   that dialog. The app aborts the connection attempt after 65 seconds.
3. Both apps send a MAC discovery beacon every 500 ms on PDP port 31000. Matching
   diagnostics use port 31001. Discovery times out after 90 seconds.
4. After learning a peer, each Vita waits one second, then sends 100 64-byte
   probes, one every 100 ms. Both echo incoming probes. Wait for the PDP summary
   and the PTP result; PTP uses port 31002 on the listener and 31003 on the client.
   The lower MAC listens, so no host selection is necessary.
5. Leave both apps open until both have finished. Cross exits and releases owned
   resources. To retry or replace a disconnected peer, exit and relaunch on both.
   This test selects the first valid peer; use exactly two devices.

The framebuffer has an original 5x7 font in 8x8 cells, a scrolling call log, and
four persistent result lines. Local and peer MAC addresses appear in the log.
Network initialization, matching, PDP/PTP calls and cleanup log signed return
values and hexadecimal codes. Nonblocking `WOULD_BLOCK` responses are expected.
Matching callback counts, the latest raw event type and IP address are diagnostic
only. Callback threads use atomic snapshots and never write the log or screen.

Everything logged is appended to `ux0:data/xita/adhoc.log`; runs do not truncate
it. Copy this file from **both** devices for comparison. A file-open/write failure
is shown on screen. Logs can grow while the app remains open. Power ticks run
once a second during waits, tests and the final echo service. UI/control polling
continues throughout; network operations are nonblocking.

## Measurements and limits

Each probe contains a magic value, message kind, sequence 0–99, a 64-bit
microsecond timestamp, a run identifier and patterned payload, in explicit
little-endian fields. Echoes preserve the sender's timestamp and run identifier.
RTT uses only the originating Vita's process clock; clock synchronization is not
required. Duplicate, malformed, wrong-peer, wrong-run and corrupt echoes do not
count. Two seconds after the final attempt, measurements freeze. Later packets
are still serviced but do not change the summary.

The summary separates local send failures from successful sends without a valid
echo. The latter is **round-trip loss**, not a claim about one-way loss. Total
missed attempts are also shown. RTT min/avg/max are in microseconds; no replies
means RTT is unavailable. Logging, framebuffer work and the 10 ms service cadence
contribute to measured RTT: this is an instrumented connectivity test, not a raw
radio latency benchmark.

PTP allows 30 seconds to connect, then 15 seconds for the 4 KiB exchange and flush.
Partial sends/receives are accumulated. The client checks the returned payload;
the listener checks the request and flushes the echoed bytes before closing.
Only the client's PASS confirms the full echo arrived. PTP failure does not
prevent inspection of PDP results. PDP echo service runs during PTP and after
completion, until Cross exits. Relaunch is required to rerun the tests.

The 1 MiB Net pool, 128 KiB matching pool, packet tracking, and two 4 KiB stream
buffers belong to the application. There is no application allocation in the
packet loop. Socket receive buffers are requested at creation (32 KiB PDP,
16 KiB PTP); SDK internals manage those allocations. GXM is initialized only for
the system dialog; app text is CPU-blitted into a CDRAM framebuffer. The simple
single-buffer display can tear.

## Exact SDK choices and missing APIs

These choices were checked against the installed Vita SDK headers and archives:

- `psp2/net/adhoc.h` does **not** exist. PDP/PTP declarations are in
  `psp2/pspnet_adhoc.h`, control declarations in `psp2/pspnet_adhocctl.h`, both
  using `ScePspnetAdhoc_stub` and `SCE_SYSMODULE_PSPNET_ADHOC`.
- `sceNetCtlAdhocInit`, `sceNetAdhocctlConnect`, `sceNetAdhocctlCreate` and
  `sceNetAdhocctlJoin` are **not declared** in these SDK headers. The available
  path is `sceNetAdhocctlInit` followed by `sceNetCheckDialogInit` with
  `SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN` and a `SceNetAdhocctlGroupName`.
  There is no invented context-create function. Dialog success and the returned
  group parameter are checked before traffic starts.
- `psp2/net/adhoc_matching.h` **does exist**. Matching is initialized, created in
  `SCE_NET_ADHOC_MATCHING_MODE_P2P`, and started. However, its callback and target
  functions use `SceNetInAddr`, not MAC addresses, and this header supplies no
  named callback event constants. The app does not guess event numbers or treat
  IP bytes as MAC bytes. It additionally uses PDP beacons to discover transport
  MACs. Matching may fail in PSP ad-hoc mode; those failures are logged and PDP
  testing continues. Whether the two API families interoperate needs hardware
  verification; native matching success is not required for this MAC test.
- `scePowerSetConfigurationMode(int)` exists in `psp2/power.h`, but its mode
  semantics are undocumented there. The app uses the declared
  `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT)` instead of inventing a Wi-Fi
  mode value. This prevents ordinary idle power transitions; whether firmware
  keeps Wi-Fi awake throughout a prolonged session needs hardware verification.

## Runtime XNet/Winsock mapping

The Phase 3 runtime will need guest socket/port-to-PDP/PTP tables, XNet address and
session-to-MAC translation, broadcast/lobby discovery, error and nonblocking
semantics, bounded runtime-owned buffers, power servicing and disconnect/rejoin
handling. This tool does not implement XNet key exchange or a game lobby.
Native matching's IP-address API needs separate validation before using it as the
runtime discovery layer.

Header functions used by this test, with their prospective roles:

| Function | Role |
| --- | --- |
| `sceSysmoduleLoadModule` / `sceSysmoduleUnloadModule` | Load/release Net, PSP ad-hoc and matching modules. |
| `sceNetInit` | Start networking with a runtime-owned pool. |
| `sceNetTerm` | Release Net after dependent transports stop. |
| `sceNetCtlInit` | Initialize network control. |
| `sceNetCtlTerm` | Release control; its return type is void. |
| `sceNetCtlAdhocDisconnect` | Request departure from the ad-hoc network. |
| `sceNetAdhocInit` | Initialize MAC-based PDP/PTP transport. |
| `sceNetAdhocTerm` | Release PDP/PTP transport. |
| `sceNetAdhocctlInit` | Establish the shared product identity. |
| `sceNetAdhocctlTerm` | Release ad-hoc control identity. |
| `sceNetAdhocctlGetEtherAddr` | Obtain the local MAC for XNet address translation. |
| `sceNetAdhocctlGetParameter` | Verify connected group and inspect channel. |
| `sceNetEtherNtostr` | Format MAC addresses for diagnostics. |
| `sceNetCheckDialogParamInit` | Initialize SDK-versioned connection parameters. |
| `sceNetCheckDialogInit` | Start group connection through the exposed SDK path. |
| `sceNetCheckDialogGetStatus` | Poll connection progress. |
| `sceNetCheckDialogGetResult` | Check connection outcome. |
| `sceNetCheckDialogAbort` | Cancel a timed-out or user-aborted connection. |
| `sceNetCheckDialogTerm` | Release the connection dialog. |
| `sceNetAdhocMatchingInit` | Supply matching's application-owned pool. |
| `sceNetAdhocMatchingCreate` | Create a P2P discovery context. |
| `sceNetAdhocMatchingStart` | Start hello traffic and callbacks. |
| `sceNetAdhocMatchingStop` | Stop callbacks before releasing state. |
| `sceNetAdhocMatchingDelete` | Destroy the matching context. |
| `sceNetAdhocMatchingTerm` | Release matching's pool usage. |
| `sceNetAdhocPdpCreate` | Implement datagram socket creation and local port binding. |
| `sceNetAdhocPdpSend` | Implement MAC-addressed sendto and broadcast. |
| `sceNetAdhocPdpRecv` | Implement recvfrom with source MAC/port. |
| `sceNetAdhocPdpDelete` | Close a datagram socket. |
| `sceNetAdhocPtpListen` | Bind/listen for a stream connection. |
| `sceNetAdhocPtpAccept` | Accept a stream peer and inspect its MAC. |
| `sceNetAdhocPtpOpen` | Allocate a stream endpoint with local/remote addresses. |
| `sceNetAdhocPtpConnect` | Establish the outgoing stream. |
| `sceNetAdhocPtpSend` | Send stream bytes with partial-write handling. |
| `sceNetAdhocPtpRecv` | Receive stream bytes with partial-read handling. |
| `sceNetAdhocPtpFlush` | Complete queued stream delivery before closing. |
| `sceNetAdhocPtpClose` | Close connected and listening stream endpoints. |
| `sceKernelGetProcessTimeWide` | Supply local timing and timeout deadlines. |
| `sceKernelPowerTick` | Service idle power prevention throughout a session. |

`screen.c` also uses `sceCommonDialogConfigParamInit`,
`sceCommonDialogSetConfigParam`, and `sceCommonDialogUpdate` to configure/render
the connection dialog, with GXM initialization, memory mapping and a sync object.
The renderer, controller and append-only file logging are standalone diagnostic
support, not Winsock mappings.
