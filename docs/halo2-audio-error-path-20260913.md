# Explicit audio-driver-unavailable diagnostic

Native attempt 41 confirms that the original game continues early startup after a
failed DirectSoundCreate. [Native87](halo2-menu-audio-dependency.md) now proves that
later map setup unconditionally dereferences an uninitialized sound object. This
diagnostic cannot currently reach the main menu and does not implement audio. The ordinary
host-channel profile still executes original DirectSound and stops at MCPX
hardware in native 40. Only the new explicit `--audio-unavailable` option
selects a separate profile that returns `DSERR_NODRIVER` at the pinned public
creation boundary. No successful sound object, hardware state, packet, callback
or audio completion is fabricated.

## Proven boundary and failure path

The public wrapper occupies `37D797..37D7DE`, 71 bytes, SHA-256
`937701608e296f3edcb1b3b77221d14015ee71e5b094256ba8fe1f50b76d7a0c`.
It accepts three stack arguments: device GUID, output interface pointer,
aggregation pointer; returns HRESULT in EAX and uses RET12. The owned wrapper
only writes the output after success. On success the returned interface is
the internal object plus eight bytes, not an arbitrary CE object layout.

Actual caller `21E516` passes `{0, audio_state+2AB0, 0}`. The new diagnostic
validates all stack/output pages and requires both optional pointers to be zero.
It leaves every guest byte untouched, preserves all CPU/FP/control state except
EAX/ESP, and returns `88780078`.
[Microsoft's DirectSoundCreate documentation](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mt708921(v=vs.85))
lists the unavailable-driver error; the value is independently defined by
[Wine's DirectSound header](https://github.com/wine-mirror/wine/blob/master/include/dsound.h).
Those sources corroborate the error meaning/value; the stack and output
contract above comes from this owned Xbox executable.

The game's `21E51B` tests the HRESULT. Failure branches to `21EACC`, calls
cleanup `21EAE0` and returns false. Caller `125540` checks this at `125545` and
skips remaining audio initialization on false. Its enclosing caller at `12070`
does not require a successful audio result and returns true at `12075`.
Native 41 records the failure from caller `21E51B` with output `71426C`, then
reaches game initialization at `137C20`. Its first callback at `137C86`, slot
`440DD8`, dispatches the undiscovered function `8D690`; that is the new explicit
stop. No additional drawing, presentation, or menu occurs.

## Reproduction and limits

```sh
python games/halo2_5849/prepare_boot.py /path/to/default.xbe \
  --host-channel --audio-unavailable --out /private/halo2-audio-error
```

Build with the existing Halo 2 Makefile, `HOST_CHANNEL=1`, and this output's
generated/image paths. The option is rejected without `--host-channel`.
Removing it restores original sound creation. Separate whole-image and exact
creation-boundary fingerprints apply. Keep native 40 as the normal-profile
baseline; native 41 is explicitly an error-path diagnostic.

All 15 host executables pass. The new error-path test checks unchanged guest
memory, full CPU state, noncontiguous pages, unsupported parameters and
incomplete stack/output spans; it also passes ASan/UBSan. All 20 Python tests
pass, including default-profile isolation, the additional boundary fingerprint,
whole-image rejection and prior compiler regressions.

Private evidence: `sound-public-audit.txt`, `native-41-artifacts`,
`native-41-view`, `native-milestone-41.json`, and `audio-unavailable-*.log`.
The earlier native 40 ELF/EBOOT/VPK/trace remain archived unchanged.

| Native 41 diagnostic artifact | SHA-256 |
| --- | --- |
| ELF | `724027d40b3767ce12cc5c1caeba81163559863a711a87bbab332b43438befb3` |
| EBOOT | `18d505cb505b8a65c521a03285dc83a11d3cd24b359af3c472dd5ad55fb0fa04` |
| Boot trace | `32e7f60949f3965fcde6129ac0e3c531a9e25c095d4e05dd0d65a6834069f72b` |

These diagnostic packages contain owned game code/image, must remain private,
and must not be uploaded as distributable releases. This milestone is not a
working audio backend or a playable Halo 2 port.
