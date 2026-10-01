# Recovering the earlier hardware checkpoint — September 6, 2026

The September 5 Keyes/combat backup has an erased profile save header and an
intact cached checkpoint. Both files contain exactly `0x345000` bytes; every byte
from `0x4000` to the end matches. The corrected file-size adapter prevents future
header erasure, but does not reconstruct the data already lost in old saves.

Copying the cached header and padding to `0x380000` was insufficient. The menu
read the file but treated it as a new game. The cache has raw game state; a profile
save additionally needs the checksum at offset `0x148`. Halo's profile reader
(`0x31700`) temporarily clears that word, checks all `0x345000` state bytes, then
compares against its saved value. The CRC routine (`0xA9630`, table built by
`0xA95F0`) uses polynomial `EDB88320`, initial `FFFFFFFF` and no final inversion.
This matches `zlib.crc32(state_with_zero_checksum) ^ 0xFFFFFFFF`. The calculation
also matches the checksum of the known-good profile saved by the native game.

After rebuilding that checksum, the ordinary Campaign menu identifies a game
in progress. Selecting a10 and its saved Normal difficulty resumes the corridor
combat checkpoint after Keyes, with the pistol, corpse and nearby enemies visible.
The director reports first-person control (`0x11E750`, control 0); look and reload
input work. A diagnostic frame checks 337 draws with no data mutations. Save and
Quit writes a valid checksum and keeps the reserved `0x380000` file length.
A full application restart, followed by the ordinary Campaign/profile/a10/Normal
menu path, again restores the corridor checkpoint in first person. This validates
resume beyond the earlier cryo-only check without a forced load shortcut.

`tools/recover_halo_checkpoint.py` makes this a reproducible offline repair. It
requires a complete known-size profile with its entire first 16 KiB erased,
matching remaining checkpoint bytes, the supported game layout/build, and zero
padding beyond the state. It refuses unrelated, truncated, partially erased or
intact saves. It creates a new output file exclusively, so it cannot overwrite
either input or an existing output. Five guard tests pass, including an output
symlink to an input. The tool's output matches the manually reconstructed file
accepted by the native game.

```sh
python3 tools/recover_halo_checkpoint.py damaged-profile.bin matching-cache.bin recovered-profile.bin
python3 tools/tests/checkpoint_recovery.py
```

Only an independent emulator directory was changed. Original backup hashes remain
unchanged, and no save or executable has been installed on the Vita during this
work. This does not establish that every older save can be recovered, nor does
resuming after Keyes exercise a fresh skip of the later Keyes cinematic. Hardware
resume and that cinematic transition still need validation.
