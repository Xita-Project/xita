# Checkpoint file-size recovery — September 6, 2026

Halo's saved checkpoint header was erased by its own initialization routine.
Guest function `0x313D0` checks that `savegame.bin` is `0x380000` bytes. When the
length differs, it zeroes the first `0x4000` bytes and requests the expected
length. The save writer emits a `0x345000`-byte payload and a 332-byte header.
The old adapter left the file at the shorter physical length, so a later open
cleared that header again. The opt-in write trace identifies `0x314A8` as the
caller of the clearing write, including after Save and Quit.

Two fixes are needed:

- `NtSetInformationFile` must pass a normal 64-bit size across the ARM ABI.
  `LI64(info)` has an `aligned(1)` typedef so guest loads tolerate x86 alignment.
  Vita GCC 15.2 propagates that alignment into argument placement: it sends the
  size in `r1/r2`, while `xk_os_truncate` expects `r2/r3`. Recorded values were
  native-pointer bits followed by zero instead of the requested size. A cast
  or ordinary local variable still produces the wrong call. Constructing the
  value from two 32-bit guest words uses the correct argument registers and
  also translates each guest word's page separately.
- Checkpoint growth now extends the physical file with one final zero byte.
  Its length therefore survives process restarts, and Halo's existing length
  query sees the expected size. Existing data is preserved and an unsuccessful
  extension reports an error. Other files retain their previous logical-size
  behavior; large disposable map caches are not physically preallocated.

Validation so far:

- Host file-I/O tests cover full 64-bit values, an unaligned information buffer,
  a value spanning two noncontiguous guest pages, error propagation and IOSB.
  AddressSanitizer and UndefinedBehaviorSanitizer pass.
- The same file-I/O test compiled with the Vita toolchain passes in Vita3K,
  exercising the actual ARM caller/callee boundary. The old call fails
  the first reservation assertion in the same native test.
- The actual Vita file adapter over host files verifies persistent checkpoint
  length across reopen and simulated process restart, header preservation,
  zero-filled tail, failed writes, read-only errors and unchanged map-cache
  behavior. This test also passes sanitizers.
- The game build requests the correct 3,670,016-byte checkpoint length. After
  a natural a10 intro, Save and Quit preserves the checkpoint header. Following
  a full app restart, the ordinary Campaign menu recognizes the game in progress.
  Selecting a10 and the saved Easy difficulty loads the cryo checkpoint directly,
  returns the first-person camera (`0x11E750`, control 0), and accepts look input.
  No global signing override or direct checkpoint-load shortcut was used.

No hardware save has been modified. This prevents new header loss; it does not
claim to repair an already erased checkpoint or resolve profile-signature
compatibility. General physical shrinking and full allocation-information
semantics are outside this correction.

Candidate and evidence: `/home/birchwoodgod/xita-backups/2026-09-06-003039-cpu-read-callback-save-local/`.
Hardware resume validation and older damaged saves remain open.

The subsequent [offline recovery check](checkpoint-recovery-20260906.md) restores
one erased hardware profile from an exactly matching cache, including the profile
checksum that the raw cache lacks. It resumes the later corridor checkpoint in
Vita3K. Original backups and hardware files remain unchanged.
