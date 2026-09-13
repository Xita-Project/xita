# Vita storage anomaly and read-only inspection — September 12, 2026

The [executable readback evidence](hardware-20260912-guest-phases.md#usb-readback-anomaly)
shows unrelated JSON inside the former EBOOT and later changes to a retained
file that was not intentionally rewritten. Its first 96 KiB matches bytes from
the new EBOOT. The new launch EBOOT itself matches the build after remount.
This suggests overlapping storage or inconsistent allocation; it does not
identify a failing card, adapter, USB driver or filesystem implementation.

Writes stopped. The entire Xita save/cache directory was copied read-only to
the computer: **55 files, 664,118,691 bytes**, all hash-verified. Configuration,
logs and both unexpected EBOOT versions are also preserved. USB is unmounted.
Do not delete `app/XITA00001/eboot.before-20260912.bin` as cleanup: it is not a
trusted backup, and freeing its allocation could affect another file.

## Why the ordinary check did not finish

UDisks ran a read-only check, but the installed checker rejected the volume with
`unsupported FAT count: 2`. This is a checker limitation, not proof that two
FATs are themselves invalid. Direct raw-device reads require administrator access
in this session; no repair, format, permission change or raw write was performed.

The [Microsoft exFAT specification](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification)
allows one or two FATs. The main boot sector selects the active FAT and matching
allocation bitmap. The inactive copies are stale by definition, so differences
between copies alone must not be labeled corruption. A contiguous stream's
`NoFatChain` flag also means its FAT entries are not authoritative.

## Prepared diagnostic

[`tools/audit_vita_storage.py`](../tools/audit_vita_storage.py) opens a stable
image or unmounted volume **read-only** and examines requested files. It validates
the main boot checksum, bounds, file-entry checksums and active chains, selects
the proper allocation bitmap, and reports shared clusters between selected files
or referenced clusters marked free in that bitmap. It honors contiguous streams.
Paths use exact on-disk spelling; it does not approximate the exFAT up-case table.

For an existing, stable volume image on the computer:

```sh
python3 tools/audit_vita_storage.py /path/to/vita-volume.img \
  app/XITA00001/eboot.bin app/XITA00001/eboot.before-20260912.bin \
  > /path/on/computer/vita-allocation-report.json
```

`--offset` specifies a byte offset when the image includes a partition table.
A volume device can be supplied only when the operator has read access and has
confirmed its current identity and that it is unmounted. Do not assume that a
previous `/dev/sdX` name still identifies the Vita after reconnecting it.

Exit 0 means no findings **among the selected allocations**; 1 means findings;
2 means inspection stopped or could not read the source. A clean result does
not certify the rest of the volume. This tool does not repair metadata, traverse
every file, verify file contents, check backup boot recovery, or implement TexFAT
transactions. Live changes would make its output unreliable. It has not yet
inspected this card's raw metadata because administrator read access is absent.

Run its host checks with `python3 tools/test_vita_storage.py`. Sixteen synthetic
cases cover active second-FAT selection with a deliberately stale first bitmap,
one-FAT volumes, overlapping allocations, active free bits, contiguous files,
cycles/excess/short chains, invalid clusters, checksums, paths, offsets,
truncation and unchanged source-image hashes after CLI execution. Error-output
tests also cover a zero root-chain link, a broken selected-file chain, and
source-open/boot failures.

September 13 follow-up: the first hardware run stopped with
`cluster out of range: 0x0`. The original CLI printed this only to stderr,
leaving the redirected report empty; the location of that zero is therefore
not yet known. The updated CLI emits a JSON report even when inspection stops,
including the traversal stage, allocation, last FAT link, and corresponding
values in each FAT. It does not follow the inactive FAT as a recovery fallback.

For a diagnostic rerun, append `--capture-metadata` to the command. This embeds
up to 8 MiB of the raw metadata reads with offsets and hashes in the computer's
JSON report. On an allocation failure it can also preserve a bounded raw preview
of the first cluster, explicitly marked unvalidated. This allows offline
inspection without treating the preview as a complete directory. The report may
contain directory names and a small preview of file contents; keep it private.
No source writes are added, and neither
an error nor a difference between FAT copies by itself identifies the cause of
the earlier executable-content damage.

## September 13 captured allocation result

The rerun supplied 1,123,744 bytes of metadata in 56 reads, with no capture
omissions. All captured hashes and overlapping byte ranges agree. The main boot
and examined directory-entry checksums validate. The active FAT is 0, clusters
are 32,768 bytes, and the volume flags are 2 (dirty).

The stop is specifically in `app/XITA00001/eboot.bin`. Its noncontiguous stream
declares 31,120,122 bytes, requiring 950 clusters. The captured active FAT follows
928 clusters, reaches cluster **1,409,407**, and then points to zero. That cluster
is also the first cluster of the retained `eboot.before-20260912.bin`, whose
stream declares a contiguous allocation of 950 clusters. The active allocation
bitmap marks this shared cluster free. The first four clusters of the retained
file are marked free; the captured new-file prefix includes the first of them.

This is an observed allocation inconsistency, rather than just a difference
between stale FAT copies. A second small decoder, independent of the audit
implementation, confirms the stream flags, lengths, shared cluster, zero link
and free bitmap bit directly from the captured bytes. The inactive FAT happens
to point from 1,409,407 to 1,409,408; it is not used as a recovery chain and does
not establish that the inactive state is correct.

No device was mounted or written during the offline analysis. The capture is a
sequence of reads, not an atomic whole-volume snapshot. It does not identify
whether the underlying cause is media, adapter, Vita USB handling, a filesystem
implementation, or some combination. The earlier executable hash match after
remount is insufficient to certify the currently captured allocation.

Preserved private evidence under the existing backup directory:

`usb-install/raw-audit-20260913/`

- `captured-report.json`, SHA-256
  `8eee909e8af038f7e65db8332b751b945e6ebd48916b26d350441115e52c9aba`.
- `offline-analysis.json`, partial cluster lists, and `independent-decode.json`.

Keep gameplay, installation and cleanup writes paused. A full card backup and
read-only inspection through a direct card reader are the next recovery steps;
do not switch FATs, clear dirty flags, or delete the retained file as a shortcut.
The computer initially had about 20 GiB free. The user subsequently freed about
660 GiB, sufficient for an uncompressed image of the 255,865,126,912-byte volume.

The next storage step is read-only allocation inspection or a stable image with
appropriate read access, followed by a recovery plan based on those results.
Do not use repeated app installs or deletion of the retained file as a repair.

## Full-copy attempt and USB transport error

The user ran a read-only `dd` image copy with exclusive destination creation and
final synchronization. It stopped after **557,842,432 bytes (532 MiB)** with an
input/output error. This is a partial image, not a complete backup. At 07:47:14
the kernel logged a high-speed USB reset, SCSI read errors, and the device going
offline/changed. Another reset appears at 07:48:32. This identifies a USB-path
interruption during the failure; it does not prove bad microSD sectors or
establish the cause of the earlier allocation damage.

The original partial image is preserved, SHA-256
`9f5da4eeb3ca99003ba37ee1674a283c1291af9c22cb63daaaa6c622d099d446`.
All 54 earlier captured metadata ranges within it match byte-for-byte; two
captured ranges lie beyond the partial image's extent. The kernel log, hashes
and partial-copy status are saved under:

`/home/birchwoodgod/xita-backups/vita-recovery-20260913`

GNU ddrescue 1.30 is staged privately from the configured Manjaro repository's
`ddrescue-1.30-2-x86_64` package. The repository SHA-256 and packager signature
validate against the installed distribution keyrings. No system package was
installed. A separate reflink copy, `vita-rescue.img`, and `vita-rescue.map`
preserve the successful prefix and mark the remainder untried. The original
partial image is not the recovery output.

The [GNU ddrescue manual](https://www.gnu.org/software/ddrescue/manual/ddrescue_manual.html)
describes persistent progress maps and `--try-again` for revisiting regions left
untrimmed after a device stops responding. A synthetic read-failure test verifies
that our conservative first-pass configuration stops, preserves the known
prefix, and resumes to an exact complete copy. Without `--try-again`, skipping
trim/sweep/scrape can leave prior failures unresolved even when a later run exits
zero; completion must therefore also check the map and expected image length.

The private `resume-backup.py` verifies the input size, saved boot identity and
unmounted state; it writes only the pre-created regular image/map on the computer.
It limits read errors and time without progress, performs no scraping/retry
passes, preserves the original partial image, and checks map completion. It has
not yet resumed physical recovery. Prefer a direct card reader to isolate the
Vita USB path, or a different known-good cable/port if no reader is available.
Recheck device identity before running; do not attempt filesystem repairs yet.
