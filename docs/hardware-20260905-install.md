# September 5: dashboard installation failure

The user reported an immediate VitaShell installation error and reconnected USB.
The exposed volume contains screenshots through 08:10:08; no newer error screenshot
was found, so the numeric error code remains unknown. Direct inspection nonetheless
identified a damaged package on the card.

## Evidence

`ux0:xita-20260905-dashboard.vpk` retained its expected size of 13,224,813 bytes,
but its SHA-256 was
`47ebed7a8327105c1499cbcab480b32522d6859f905c0b493926dbba203e87af`,
instead of the verified local build's
`8b20f495ff5a522680e6e88c79f21b49a3f0e7514c107a06c610394a5c98db9c`.
An uncached `dd iflag=direct` read reproduced the bad hash.

There were 65,930 differing bytes, within these 512-byte-aligned ranges:

- `[393216, 393728)` inside compressed executable data.
- `[12812288, 12878848)` across shader data and ZIP entry headers.

Reading all archive entries produced 88 failures: `eboot.bin` failed CRC, and
87 shader entries failed decompression or local-header validation. This is a
concrete installation blocker, independent of dashboard runtime behavior.
The first damaged sector starts with the exact 35 bytes of
`VitaShell/recent/xita-20260905-dashboard.vpk.lnk`. This establishes an overwrite
with other filesystem data; it does not establish the underlying driver, caching
or allocation fault. The original copy had passed buffered verification before
USB hand-off, so future transfers also need direct reads and remount verification.

During the initial inspection, the installed executable matched the working CPU-monitor build:
`794468c616699a1d868d757b0b9dd99662479bb0a362f4b03cec227915f27fc1`.
There is approximately 145.7 GB free on the volume. The local dashboard archive
has valid CRCs and the same metadata/assets as the installed build; only its
executable differs. No rebuild or graphics change is needed to replace the bad copy.

## Recovery

Evidence is retained at
`/home/birchwoodgod/xita-backups/2026-09-05-152544-install-error/`, including the
damaged VPK, byte differences, failed-entry list, current configuration/logs and
save/cache hashes. One cache map differs from the previous backup and was copied
before any replacement; it was not repaired or replaced.

The VPK was copied again via a temporary file, flushed with `fsync`, renamed into
place and its containing directory flushed. A direct read matched the expected
SHA-256. After unmounting and remounting, another direct read matched, and all
592 archive entries passed CRC checks. However, the separate configuration
integrity check failed: the 205-byte `data/xita/xita.cfg` now contained bytes from
offset 13,139,968 inside the VPK. All 53 save/cache files and the installed
executable still matched their pre-transfer hashes. **The USB replacement is not
ready to install.** Package verification alone is insufficient for this failure.

Further USB allocation/writes were stopped. The configuration was restored from
backup by writing exactly 205 bytes in place, without truncation or a temporary
file. A direct read confirmed its original SHA-256,
`1a503db8f8f5f388d54f08212bb37ba3c4190cdb027882aa1a7cc7baf63cb3f7`.
Because the files appear to overlap in the USB-exported filesystem view, restoring
the configuration may invalidate the replacement VPK. Do not use that USB copy.
The volume was safely unmounted. The user explicitly preferred USB over the
proposed FTP transfer, so recovery continued over USB using the existing app file.

The read-only filesystem check could not run: `fsck.exfat` rejects the USB-exported
volume's reported FAT count of two. No filesystem repair was attempted. That tool
limitation does not by itself establish filesystem damage.

`replacement.json` records the failed package replacement and configuration
restoration. The on-card VPK must not be installed or used as a verified backup.
No filesystem repair was made; the underlying allocation problem remains unknown.

## USB update without allocating a new file

All 592 installed app files matched the working CPU-monitor archive before the
update. Only the executable needs to change for the dashboard. The uncompressed
dashboard executable is larger than the installed one, so it cannot be overwritten
without extending its allocation.

The same dashboard VELF was packaged locally with `vita-make-fself -s -c`.
This uses VitaSDK's supported compressed SELF format and retains the safe app
permissions. See the [VitaSDK packer source](https://github.com/vitasdk/vita-toolchain/blob/master/src/vita-make-fself/vita-make-fself.c).
The resulting 11,410,501-byte SELF was padded with zeros to the existing
32,918,474-byte executable length; its `self_filesize` header field was updated
to that actual length. All three decompressed program segments and their program
headers were checked against the original dashboard SELF and are identical.
There is no game-code, asset or rendering change in this repackaging.

The exact padded executable boots the dashboard and reaches Halo's main menu in
the isolated Vita3K instance. This verifies loading and the dashboard hand-off in
the emulator; it does not establish hardware startup or performance.

After backing up the installed executable and checking the config and all 53
save/cache files again, `app/XITA00001/eboot.bin` was overwritten through `r+b`
without truncation, renaming, allocating a temporary file or changing file size.
Writes were flushed with `fsync`; a direct read matched the candidate's SHA-256:
`b1832e3c5357789cf98636ede95249b7eef88814576601c29265054d2f79f1ce`.

After unmounting and remounting read-only, direct reads again confirmed the
executable and configuration hashes. All 591 other app files matched the dashboard
archive; all 53 save/cache files, the config, log, backed-up VitaShell metadata and
five screenshots matched their pre-transfer hashes. The volume was then safely
unmounted. This update is ready for the user to launch from the existing bubble.

The backup directory contains `eboot-before.bin`, `eboot-dashboard-usb.bin` and
`usb-in-place.json`, which records the executable hashes and verification results.
The previous executable can be restored by the same exact-length in-place method.
Hardware launch remains to test; use the existing Xita bubble, not the damaged VPK.
