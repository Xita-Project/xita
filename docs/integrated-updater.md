# Integrated development updater

The updater adds an **Update** page to the existing Xita dashboard. Install the
new VPK once through VitaShell to add its stable launcher and initial runtime.
After that, a paired computer can send compatible code builds over Wi-Fi,
restart Xita and check that the expected executable reached the dashboard.
This is a private development workflow, not an internet release downloader.

## Upload and install

Use the existing [remote testing](remote-testing.md) pairing and settings. The
service remains opt-in, requires the pairing key and uses the same configured
port. Nothing needs to be added to taiHEN and no second bubble is installed.

```sh
python3 tools/vita_remote.py --config /private/path/remote-client.json update-status
python3 tools/vita_remote.py --config /private/path/remote-client.json update /private/path/xita.vpk --apply
```

The host extracts only the runtime from the VPK, compares the package's asset
contract with the installed launcher, uploads bounded chunks, and asks the Vita
to read back and verify the complete SHA-256. With `--apply`, it then requests a
restart and waits for a dashboard acknowledgement reporting the exact runtime
hash. An upload receipt alone is not an installed-build confirmation.

Omit `--apply` to stage the verified build for the dashboard's **Install received
update** action. **Restore previous build**, or the command below, requests the
previous confirmed executable and restarts Xita:

```sh
python3 tools/vita_remote.py --config /private/path/remote-client.json rollback
```

Updates restart the current game. Benchmark measurements exclude update
operations. A dropped transfer leaves an incomplete staging file; rerunning the
upload starts a fresh transfer. It cannot replace the running executable.

The initial VPK and ordinary candidates are built with `make RECOMP=1`. The
packager takes the runtime from `build/eboot.bin`, installs it as `game-a.self`,
and puts the small updater launcher in the VPK's `eboot.bin`. Consequently,
hashing installed `eboot.bin` now identifies the **launcher**, not the current
Halo runtime. Use `update-status`'s `boot_sha256` and `boot_slot` after startup.

## Slot ownership and recovery

The stable launcher is never overwritten by this protocol. It manages
`app0:game-a.self` and `app0:game-b.self`; only the inactive slot receives an
update. The active slot remains intact while the helper copies, flushes and
verifies the candidate. Small checksummed records track generation, executable
size/hash, package contract and pending/attempted/confirmed state.

During gameplay, the recording owner drains its published frames at a present
boundary and parks without yielding the guest token. The main thread stops the
render pump, closes the network service, drains/detaches the display and hands
control to the stable launcher using `sceAppMgrLoadExec`. The network thread
never stops a guest fiber or changes GXM objects itself. A busy or stalled game
must reach this boundary before an in-process update can be applied.

Before launching a candidate, the helper marks it attempted. The new runtime
confirms only after rendering the dashboard and, when remote testing is
configured, successfully starting that service. If confirmation never happens,
the next bubble launch selects the previous confirmed slot. Explicit rollback
also remains available after a successful startup.

This does not keep a supervisor running alongside Halo. A full system freeze,
power loss or storage failure can still need local recovery. Checksums and an
intact active slot improve recovery; they do not make filesystem writes immune
to device failure. Dashboard confirmation establishes startup, not campaign
stability or a performance improvement.

## Package compatibility

The package contract covers the launcher, shaders, SFO, artwork, licenses and
other packaged assets. It deliberately excludes the runtime and its initial
boot record, so ordinary code builds can share one installed asset set. Both
the host and device reject a mismatched contract. A changed launcher, shader,
package layout or other asset requires a fresh VPK installation.

The first implementation targets the existing `XITA00001` Halo CE application.
It does not update Halo 2's separate application, game maps, saves, user settings
or firmware. The endpoint accepts fixed operations and fixed file paths; it is
not an arbitrary file server or remote shell. Pairing authenticates the sender;
the SHA-256 and package contract detect corruption/compatibility problems, not
publisher identity. Keep the existing private-LAN boundary.

## Validation

`tools/test_update.py` compares SHA-256 against Python's implementation at block
and padding boundaries, then exercises real files for interrupted uploads,
offset/size rejection, damaged content/metadata, failed destination writes,
two-slot installation, missing acknowledgement, confirmed startup and rollback.
`tools/test_remote.py` drives the production HTTP server and Python uploader
through real sockets, including authentication, truncated bodies, benchmark
exclusion and immediate port reuse after restart; it also passes ASan/UBSan.
Package tests verify that runtime changes retain compatibility while launcher
changes invalidate it. Dashboard tests cover both actions and an unavailable
updater, alongside the existing settings and graphics-panel checks.

The September 14 native package builds successfully. In Vita3K, a real 31 MB
runtime upload from the dashboard verified, switched slots, restarted and
acknowledged the exact new executable hash. Normal menu navigation loaded Blood
Gulch. A subsequent gameplay update drained the recording and rendering owners
and called the launcher, but **Vita3K crashed during its own process teardown**:
the host backtrace ends in `free(MemState&, unsigned int)` from a `ThreadState`
destructor. This is not a passing automatic gameplay restart test. Reopening the
emulator applied the preserved candidate; an automatic dashboard rollback then
returned to the previous confirmed slot. The two slots held the same final
runtime in that rollback test, so the evidence establishes slot/generation
selection, not a comparison of different game behavior.

Native package validation, screenshots, crash backtraces and transfer receipts
are preserved privately under
`2026-09-13-worker-sizing/validation/integrated-updater`. Runtime SHA-256:
`e1c6e95867db1843cc0d95e437f557a4b7944dd8dca91bb7f449da8b886dd987`.
Physical Vita upload, gameplay restart and rollback still need validation after
the one-time bootstrap VPK installation. No FPS improvement is claimed for the
updater itself.

The [VitaSDK app-manager API](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/appmgr.h)
documents the SELF handoff and its `app0:` location requirement.

## Bootstrap transfer

On September 14, `xita-updater-20260914-e1c6e95.vpk` (13,433,619 bytes) was
copied to `ux0:VPK/` over USB. Its SHA-256 and ZIP contents were checked again
after a read-only remount, then the volume was safely unmounted. The installed
executable, settings and pairing file retained their pre-transfer hashes. This
is a verified **package transfer**, not installation: open that VPK in VitaShell
once, launch Xita and leave it on the dashboard for the first hardware update
check. Package SHA-256:
`ea15bffde007e900917521424c04e5e5a97981c8997522d02866effca9fc472f`.
