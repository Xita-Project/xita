# Halo 2 font-cache rebuild and file lifetime

Native66 reaches the original disc-error screen after the font-cache rebuild
fails. Shared `open_common` creates an object with one reference and creates a
handle, which adds another, but never releases the creator reference. `NtClose`
therefore leaves the object and its delete-on-close request alive. The original
cache deletion sequence is followed by CREATE_NEW collisions (`C0000035`).
The original font initializer `121B00` fails; caller `121FC0` sets the error flag
at `12202E`. No game error flag or branch is patched.

The balanced-lifetime option releases the creator reference after successful
handle creation. Final close now executes the existing file destructor. An
exhausted handle table returns `STATUS_TOO_MANY_OPENED_FILES`, preserves the
output handle, and releases the failed open's object/descriptor. The option is
default off and enabled only by the isolated Halo 2 boot target; CE behavior is
unchanged. It must be selected before opening title files. This fixes ownership,
not general NT sharing, deletion-pending namespace, access validation, or pending
I/O semantics.

Microsoft's [NtClose contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntclose)
requires the last handles and outstanding object references to be released before
object destruction. The [file disposition contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information)
describes deferred deletion on close. The test links the actual shared NT APIs,
handle table, and POSIX file adapter against synthetic guest inputs and a unique
private temporary directory. It verifies default behavior, enabled last-close
cleanup, duplicate handles delaying deletion, clear-disposition, delete/recreate,
directory cleanup, missing opens, handle exhaustion, ABI and output preservation.
All 19 host executables plus the timed case pass; the lifecycle test also passes
ASan/UBSan with leak detection.

Native67 confirms the original code deletes and rebuilds the cached fonts and
advances to opening/reading `d:\bink\intro_60.bik` (13,310,144 bytes). The owned
source font hashes remain unchanged. The prior private lab save tree was cloned
before the run to `private/native67-before-save`.

The new stop is a null indirect call through `[484B28]` in the movie pixel
conversion function `3E9C70`, return `3EA026`. Its preceding DirectSoundCreate
failure is correctly checked by the original sound helper; the trap is not an
audio-object dereference. The original Bink feature probe toggles EFLAGS.ID,
which the current runtime fails to preserve; whether that explains the converter
selection is the next CPU-state investigation. The unavailable-audio diagnostic
remains explicit. The displayed output is black: no decoded movie frame or menu
has been presented.

| Private native67 artifact | SHA-256 |
| --- | --- |
| ELF | `9175bd20231405e070064941117f51ce22398932acd6d521b469a058bbb77f96` |
| EBOOT | `c920021d5499518121004a56feb52370310a3c81d96b2b3cccf2149878c2dd20` |
| VPK | `1f04729340ea2127cfdf81618a58429584eaf890b5a53f4474bd7dc9cb78bad5` |
| Boot trace | `eb2e0124432336bb2167d8ef7a9845fcabd9cdb5e5ed7406414e9d0da8347407` |
| Last presented buffer | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Replay: `python3 ../private/run_lab.py replay67 ../private/native-67-artifacts/halo2-boot.vpk`.
The generated source is still `online-interfaces/generated`. The VPK embeds
owned game image/code and must not be distributed or uploaded as a release.
Assets, private lab saves, generated source and captures remain outside Git.
