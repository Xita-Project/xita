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

The next storage step is read-only allocation inspection or a stable image with
appropriate read access, followed by a recovery plan based on those results.
Do not use repeated app installs or deletion of the retained file as a repair.
