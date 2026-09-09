# System Link over Vita ad-hoc (Phase 3 groundwork)

This is an **opt-in, hardware-unverified** transport. The default remains the
original in-memory Winsock HLE: no network modules, dialog or wireless calls.
Enable it with this line in `ux0:data/xita/xita.cfg` on **both** consoles:

```ini
XV_NET_ADHOC=1
XV_NET_LOG=1
```

Restart after changing settings. `XV_NET_ADHOC` unset, `0`, or any value other
than numeric `1` selects loopback. `env.txt` is loaded first, then `xita.cfg`;
the dashboard file takes precedence. Check both files for older overrides.
Logs go to `ux0:data/xita/xita.log`.

## Startup and SDK surface

After GXM initialization and before launching the guest thread, the runtime loads
`SCE_SYSMODULE_NET`, calls `sceNetInit` with a 1 MiB aligned pool, initializes
`sceNetCtlInit`, loads `SCE_SYSMODULE_PSPNET_ADHOC`, calls `sceNetAdhocInit`, then
`sceNetAdhocctlInit` with product ID `XITAADH01`. As in `tools/adhoctest`, a
NetCheck dialog in `SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN` connects group `XITA`.
The normal GXM display buffers render the dialog with `sceCommonDialogUpdate`.
The dialog has a 60-second SDK timeout and a 65-second application deadline.
The result and returned group are checked before Halo starts. Failure releases
owned networking resources and exits; relaunch to retry. Shutdown closes sockets
before terminating control, transport and Net and unloading owned modules.

This SDK has **no** `sceNetAdhocctlConnect` and no `psp2/net/adhoc.h`.
The exact PDP/PTP and control declarations are in `psp2/pspnet_adhoc.h` and
`psp2/pspnet_adhocctl.h`. Their import library is `ScePspnetAdhoc_stub`.
The RECOMP build also links `SceNet_stub`, `SceNetCtl_stub`, `SceSysmodule_stub`
and `SceCommonDialog_stub`. Native ad-hoc matching is not used: discovery travels
as Halo's own broadcast datagrams, and the SDK peer list supplies MAC addresses.

## HLE mapping

The guest handle remains `0x2000 + slot`, with 64 slots. Each slot has an ad-hoc
backend ID, pending-connect/error state and send/receive timeout settings in
addition to the existing queues and nonblocking flag.

The generated thunk names for sendto/recvfrom and getsockname/getpeername were
historically swapped. **Those bindings and stack argument counts are preserved.**
The table describes the actual operation and identifies the swapped C names.

| Guest operation / HLE entry | Ad-hoc implementation |
| --- | --- |
| `socket` / `ws_socket` | Allocate a table slot and its fixed receive storage. SDK endpoint creation is deferred until the port/destination is known. |
| UDP `bind` / `ws_bind` | `sceNetAdhocPdpCreate(local MAC, host-order port, 32768, 0)`; an explicit loopback bind stays in memory. |
| TCP `bind` / `ws_bind` | Remember the port for PTP open/listen. |
| UDP `connect` / `ws_connect` | Save default destination; create PDP endpoint for remote traffic. |
| TCP `connect` / `ws_connect` | `sceNetAdhocPtpOpen`, then nonblocking `sceNetAdhocPtpConnect`; select progresses pending connections. |
| `listen` / `ws_listen` | `sceNetAdhocPtpListen`, 16 KiB SDK buffer, backlog capped at 16. Local in-memory connects can also reach this listener. |
| `accept` / `ws_accept` | `sceNetAdhocPtpAccept`; select saves accepted handles in the existing pending queue. |
| UDP `sendto` / **`ws_recvfrom`** (`0x1B158C`) | `sceNetAdhocPdpSend` with resolved MAC or broadcast MAC. |
| UDP `recvfrom` / **`ws_sendto`** (`0x1B157D`) | `sceNetAdhocPdpRecv` into a fixed queue; return fake source IP and byte-swapped source port. |
| `send` / `ws_send` | UDP uses the connected destination through PDP; remote TCP uses `sceNetAdhocPtpSend`, returning partial writes. |
| `recv` / `ws_recv` | UDP consumes the datagram queue; TCP uses `sceNetAdhocPtpRecv` into the existing stream ring. `MSG_PEEK` leaves buffered data intact. |
| `select` / `ws_select` | Read: zero-timeout, nonblocking PDP/PTP receives (and accept on listeners), with results retained in queues. Write: progress connect, then `sceNetAdhocPollSocket` for `EV_SEND`. Exceptions: saved backend errors. |
| `ioctlsocket` / `ws_ioctlsocket` | `FIONBIO` sets the per-socket flag. `FIONREAD` polls and reports buffered bytes/next datagram length. |
| `setsockopt` / `ws_setsockopt` | Record `SO_SNDTIMEO`/`SO_RCVTIMEO` in milliseconds. Other options retain the existing success shim; no TCP option emulation. Broadcast is always permitted. |
| `getsockopt` / `ws_getsockopt` | Return saved timeouts and `SO_ERROR` (cleared on read); buffer-size queries retain the 16 KiB compatibility answer. |
| `getsockname` / **`ws_getpeername`** (`0x1B03F1`) | Return stored local port/IP. |
| `getpeername` / **`ws_getsockname`** (`0x1B03FC`) | Return stored peer port/IP. |
| `closesocket` / `ws_closesocket` | `sceNetAdhocPdpDelete` or `sceNetAdhocPtpClose`, then free slot storage. No blocking flush on close. |
| `WSAGetLastError` | Return translated Winsock error: would-block/timeout, address-in-use, disconnected, refused, etc.; unknown SDK errors become `WSAENETDOWN`. |
| `XNetGetTitleXnAddr` | `sceNetAdhocctlGetEtherAddr` at startup; write local fake IPv4 and MAC into a 36-byte XNADDR, returning existing Ethernet/static flags. |
| `XNetXnAddrToInAddr` | Learn/resolve XNADDR MAC to fake IPv4; preserve explicit `127.0.0.1`. XNKID is not used for routing. |
| `XNetCreateKey`, `XNetRandom` | Existing pseudorandom-byte shims; no SDK transport call. |
| `XNetRegisterKey`, `XNetUnregisterKey` | Existing success shims; no key exchange, encryption or session isolation. |

All Winsock operations above can serve remote sockets where applicable. The
**127.0.0.1 route is deliberately loopback-only**. TCP to the local fake IP also
uses the in-memory connection path. UDP to either local address stays in memory;
broadcast is sent wirelessly and copied to local sockets on that port. Local
server connections retain a 127.0.0.1 source, as required by Halo's local-server
check. Remote unicast does not get delivered to local sockets merely because
their port matches. With the flag off, all original address-agnostic loopback
routing and original XNet title address `10.0.0.1` remain in effect.

## Addresses, storage and timing

For MAC `aa:bb:cc:dd:xx:yy`, the fake IPv4 is `169.254.xx.yy`, where xx and yy
are the last two bytes interpreted as decimal octets. On the little-endian guest,
the raw integer is `0x0000fea9 | (xx << 16) | (yy << 24)`.
The XNADDR's `ina` is at offset 0 and its six-byte Ethernet address at offset 10;
the rest retains the existing zero-filled layout. Ports are stored in guest
network byte order and converted to host order for SDK calls.

A fixed 32-entry peer table is populated by `sceNetAdhocctlGetPeerList` at most
once per 500 ms while networking is serviced, by received PDP source MACs, by
accepted PTP MACs and by XNADDR translation. Each newly learned peer is logged
once. Entries remain for the process lifetime. Unknown destinations, invalid
MACs, full tables and duplicate fake IPs with different MACs fail translation;
there is no arbitrary routing fallback. Restart to clear the table. Collisions
in the last two MAC bytes require a future address-negotiation scheme.

`255.255.255.255`, the link-local /16 broadcast `169.254.255.255`, and the legacy title subnet's `10.0.0.255` map to
`ff:ff:ff:ff:ff:ff`. The rare MAC suffix `ff:ff` consequently cannot be used as an unambiguous
unicast destination in this groundwork; other `.255` addresses are valid within
the fake /16 subnet.

The application allocates receive storage only when creating/accepting sockets:
64 datagrams of up to 1536 bytes per UDP socket, or a 64 KiB TCP ring. The SDK
allocates its endpoint buffers at PDP/PTP creation. Receive polling reads at most
one packet/chunk per socket per pass; full queues stop draining. No per-packet
malloc/free occurs. The static Net pool costs 1 MiB even with the feature off.
New peers/endpoints are logged, while packet/readiness logs are suppressed in
ad-hoc mode. SDK errors are retained for guest error reporting.

All SDK data I/O uses timeout **0 plus `SCE_NET_ADHOC_F_NONBLOCK`** (timeout 0
alone would block). Blocking guest calls retry cooperatively through `xk_yield`,
respect configured socket deadlines, and cap an unspecified timeout at five
seconds. Nonblocking calls return immediately. Select uses the guest timeval
(including immediate zero), preserving the existing five-second cap for NULL.
The scheduler may resume a yielded fiber later than its deadline; no network
call itself blocks the game thread. Power ticks accompany peer polling.

## Validation and two-Vita procedure

The requested source generation and `make RECOMP=1 -j8` completed successfully,
including ELF conversion and VPK packaging with the new SDK imports. The host
transport tests also passed with ASan/UBSan (leak detection disabled under ptrace).

`tools/tests/net_hle.sh` compiles a host test against the installed SDK headers
with mocked transport functions and AddressSanitizer/UndefinedBehaviorSanitizer.
It checks default UDP routing with zero SDK calls, remote address/port mapping,
broadcast, select/peek packet retention, timeout bounds, partial PTP writes,
listener acceptance, local TCP source identity, XNADDR round trips and close.
In a ptrace-based sandbox, use `ASAN_OPTIONS=detect_leaks=0 tools/tests/net_hle.sh`
because LeakSanitizer cannot operate there. These tests do not simulate firmware.

1. Install the same Xita development VPK on both Vitas using the
   [installation guide](installing.md). Both need their supported Halo image
   and the same map; start with Blood Gulch. Source builders can instead use
   the [build guide](building.md) and install that build on both devices.
2. First remove `XV_NET_ADHOC` on one Vita and verify the usual single-Vita
   multiplayer lobby and force-start workflow still works. This is the hardware
   regression baseline. Exit the app.
3. On both Vitas, set `XV_NET_ADHOC=1` and `XV_NET_LOG=1` in `xita.cfg`.
   For the real join/start test remove `XV_FORCE_START=1` and any scripted second
   controller settings. Enable Wi-Fi, disable airplane mode, and keep devices
   nearby. No router is required. Quit the standalone adhoctest app if running.
4. Launch Xita on both and allow the connection dialogs to complete. Check both
   logs for `ad-hoc XITA ready` and different local `169.254.x.y` addresses.
   A failed/cancelled dialog exits; restart both to retry.
5. On the **host**, enter multiplayer **System Link** and create a Blood Gulch
   Slayer game. On the **guest**, enter System Link, wait for the host's game to
   appear, select it and join. Verify both players appear in both lobbies.
6. Start normally on the host. Verify both load Blood Gulch, can move and see
   each other, shoot, respawn, and finish/leave a match. Record discovery/join
   latency, visible stalls and disconnect behavior. Lobby success is milestone
   1 evidence; a complete match is separate evidence.
7. Copy `ux0:data/xita/xita.log` from **both** devices before relaunching. Compare
   peer MAC/fake-IP entries and PDP/PTP endpoint ports. If discovery fails, run
   `tools/adhoctest` on both separately and save both `adhoc.log` files to isolate
   firmware/group connectivity from Halo traffic.

Two physical Vitas are now available for testing (September 8). Their availability
does not establish a successful link. Connection-dialog rendering, firmware port
acceptance, PDP discovery, PTP establishment, Halo lobby/join/start compatibility,
MTU/packet sizes, Wi-Fi power behavior, loss/latency and disconnect recovery all
remain untested on hardware. Oversized datagrams report errors; fragmentation
above the existing 1536-byte HLE limit is not implemented. Peer eviction/rejoin,
XNet security/key exchange, reliable flush-on-close and campaign co-op are later
work. A successful link or mock test does **not** establish that two Vitas can
join a Halo lobby yet.
