# September 14: updater permissions and remote comparisons

The original updater VPK was installed and its runtime identified as
`e1c6e95867db1843cc0d95e437f557a4b7944dd8dca91bb7f449da8b886dd987`.
Wi-Fi transfers verified their incoming bytes, but the helper returned to slot
0 with the staged file still present. The initial host check reported success
because an identical runtime hash matched. That acknowledgement was incorrect.
The corrected client requires the expected hash, a changed confirmed slot and
empty staging state.

Repackaging only the helper without `vita-make-fself -s` was necessary but did
not resolve the physical write failure. A subsequent USB log capture recorded:

```text
update failed: open inactive app slot for writing (errno 13); confirmed slot preserved
```

The [VitaSDK app-manager header](https://github.com/vitasdk/vita-headers/blob/5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb/include/psp2/appmgr.h)
documents that unmounting `app0:` enables writes to the installed app directory.
[VitaShell initialization](https://github.com/TheOfficialFloW/VitaShell/blob/81af70971ba18b8ce86215b04180f1e3d21cdfc9/init.c)
does this, and its [restart command](https://github.com/TheOfficialFloW/VitaShell/blob/81af70971ba18b8ce86215b04180f1e3d21cdfc9/settings.c)
later uses `sceAppMgrLoadExec("app0:eboot.bin", ...)`. Xita now releases the mount
only after verifying a confirmed slot and an incoming candidate. Subsequent
helper reads use the fixed backing directory and verify its contract before
writing. No mount changes occur in the game or rendering loop.

## Validation and transfer

- Normal fixed-path transaction tests pass: interrupted uploads, corruption,
  failed destination writes, install, confirmation, failed-boot fallback and rollback.
- `MOUNT_TEST=1 python3 tools/test_update.py` exercises the Vita branch with an
  unmount failure followed by successful removal of the `app0:` alias. All later
  transactions must work through the backing directory, without touching the
  confirmed executable when an operation fails.
- Production HTTP and client tests pass normally and under ASan/UBSan. They
  reject false update acknowledgements and overlapping benchmarks/transfers.
- Guest-owner benchmark admission, cancellation and restoration tests pass.
- The native candidate builds and reaches the dashboard in private Vita3K.
- A real 31,174,294-byte loopback upload then applied through the revised helper,
  restarted, and confirmed slot 1 with the exact expected runtime hash and no
  staged update. This validates the emulator lifecycle; its filesystem does
  not reproduce the physical Vita's app-directory permission check.

The new helper, contract, fallback boot record and candidate in slot B were
copied directly through the user's authorized USB connection. All five files
were hashed again after a read-only remount, and the volume was safely unmounted.
The original slot A and user settings retained their hashes during that USB
transfer. The user then launched Xita, and the paired service confirmed the
expected runtime in slot B. A subsequent real Wi-Fi upload of the same candidate
applied through the new helper, restarted automatically, and confirmed slot A
with the expected hash and empty staging state. The identical candidate was
intentional: slot switching must be verified even when hashes are equal.
The revised launcher-log endpoint also works on hardware. Halo subsequently
reached its normal main menu through remote dashboard input.

An update from live solo Blood Gulch subsequently confirmed diagnostic runtime
`ca79ff5918b30611edda75548c47c3d84752910ec55f495a7000c38543bf1529`
in slot B, with empty staging state. The dashboard and Halo menu rendered.
This establishes a physical gameplay-to-updater handoff, not long-running
recovery reliability or an FPS improvement.

The next capture exposed persistent controller status `0x00010000` after
relaunch. [VitaSDK identifies this as `SCE_CTRL_INTERCEPTED`](https://github.com/vitasdk/vita-headers/blob/5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb/include/psp2common/ctrl.h),
not a held gameplay button. The remote layer had treated every nonzero bit as
physical input and rejected injected controls. Its correction preserves this
status bit, physical button/stick priority and input expiry; host tests pass
normally and with ASan/UBSan. It does not take control of the system shell.

The corrected diagnostic (`e1ebb007f713bf4c95c4d2e7c63274143e40d0ec19393d1463957b29fa4e8dea`)
uploaded and verified, but the next restart did **not** return a confirmed boot
within 180 seconds. The Vita still responded to ping while the paired HTTP
service refused connections. Its installation and current screen are
unconfirmed. The prior slot protocol preserves a fallback, but recovery from
this particular stop has not yet been observed. Neither a GPU crash nor an
SD-card fault can be diagnosed from that network symptom alone.

| Component | SHA-256 |
| --- | --- |
| Runtime | `7787ebbcccc938dc56bbdb34ed0b6d35099042ec168e17a1d07f50c34f4401c1` |
| Launcher | `a80548cc5df4876c1d75d274183c6c1ac32014d8ec2df8b8ed492575c0e6327e` |
| Package contract | `6aabd8df1179fcaf1c3b61f4c926663c729d229a30f249c9f6e602270ea081c5` |

Private evidence is under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z`.
No game assets, pairing keys or device logs are committed.

## Performance work enabled

The build adds explicit remote benchmark selection and a fixed launcher-log
download. The host can choose one existing experiment per off/on/off trial
without changing saved settings or rebuilding just to change the selector.
Network requests are consumed by the guest owner; they do not run guest math
or modify rendering state on the network thread.

The frame cap was raised from 20 to 30 through the dashboard; the remaining
graphics settings were preserved. This is measurement headroom, not a measured
FPS gain. The preceding physical object-basis trials averaged approximately
11.75 FPS with or without that helper, so no useful improvement was established.
The next planned comparison is the optional native model-palette batch, followed
by further scene/object profiling if the whole-frame benefit is small.

A first explicit model-palette trial completed all three phases in Vita3K, but
the host downloaded its log before the final file bytes were visible and
correctly refused to report success. Vita3K's `sceIoSyncByFd` is unimplemented.
The host now polls only the appended log tail after measurement until the
restoration record is readable, then downloads the full evidence. A repeated
native trial passes with the requested model-palette tag and all camera checks;
its approximately 20 FPS values reflect the emulator cap, not hardware speed.
This host-only fix changes no executable or measured phase behavior.
