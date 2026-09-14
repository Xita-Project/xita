# Halo 2 per-map lifecycle discovery

Native83 leaves the original movie on Start and loads mainmenu data from the
valid original cache. Its next missing target is 11CC10, called through the
record at 440E28 by 137CA0, return 137CDC. The original eight-byte callback sets
a game-owned initialization flag and returns. No replacement or forced return
is introduced.

The same 68-record table used by the startup initializer has four additional
lifecycle fields. Exact original forward and reverse walks establish each bound:

| Caller walk | Direction | First field | Last field | Nonzero slots |
| --- | --- | --- | --- | --- |
| 137CC5..137CE7 | forward | 440DE0 | 44174C | 54 |
| 137D0D..137D25 | reverse | 441750 | 440DE4 | 54 |
| 137D7B..137D97 | forward | 440DE8 | 441754 | 21 |
| 137DA1..137DB9 | reverse | 441758 | 440DEC | 21 |

Every walk has stride 24h and skips null callbacks. The extractor validates all
four complete walk fingerprints, then reads only these four fields in each
record and requires each nonzero target to be executable .text. There are 94
unique roots across these fields. It does not classify neighboring scalars or
other descriptor fields as code. Original call ordering, state writes and
runtime dispatch remain unchanged.

Synthetic tests cover first/last entries of all four fields, null skipping,
duplicates, non-code targets, wrong sections, each fingerprint failure, and
adjacent fields/next-record sentinels that must never be inspected. All 22 focused
Python tests pass. Regeneration discovers 11,742 candidate functions, 160,954
basic blocks and 1,118,936 instructions; 3,832 unsupported instruction occurrences
remain. These are automatic discovery metrics, not validated game compatibility.
Owned bytes and generated code remain outside Git, under private/map-lifecycle.

Native84 built successfully with VitaSDK (-j4), formats the fresh private cache,
loads actual mainmenu data after Start and passes the 11CC10 stop. The next
missing original callback is 115D50 in D4890, return D48B8. This is a nested
resource-lifecycle table, to be audited next. GET=PUT=03BA2FF4; the last presented
frame is 233 and remains black. No visible menu is established. The emulator is
stopped and exact captures are in private/native-84-view.

| Private native84 artifact | SHA-256 |
| --- | --- |
| ELF | `04077912e2d8d4a471ee63106703fa17d99e9986f8128d54e864004bd14fb7bf` |
| EBOOT | `f5ece37892f17c7884dfb6d1e6a5ca205780f6176faf2379e2e144def2d8885e` |
| VPK | `fc32c15f78cf80943e1e2b8fda448d88737e3e5f098ab8444d24e4a0bf55f4d2` |
| boot.log | `6d50190a89348d39ae5cd24d61c977d80959d88f841a12b06d5225d7cf05cf7f` |
| channel-at-stop.json | `efb463c86ebb1d054873992121632841cf31432cedcce5905fe9bd3d6244202c` |
| last-presented-at-stop.bin | `d5a9945e45d5eed722a712dfccb6ebb4ea43597e4f44398c682bcc0d7d2f72fa` |

The private package embeds owned image/code and must never be uploaded as a
distributable release. Replay in the existing isolated lab requires preserving
its previous cache4 directory and providing an empty private directory for the
original formatter (the populated raw/namespace limitation remains):
`python3 ../private/run_lab.py replay84 ../private/native-84-artifacts/halo2-boot.vpk`.
After cache002.map reaches the owned map size, press/release Enter/Start in :111.
An early Start press can be ignored while the original cache copy is pending.
