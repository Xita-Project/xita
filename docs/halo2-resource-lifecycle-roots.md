# Halo 2 three-record resource callbacks

Native84 loads mainmenu data and reaches a missing original callback 115D50
in D4890, return D48B8. The original loop calls one field in each of three
resource records, stride 38h. Two neighboring functions independently walk the
same three records for other lifecycle stages. The extractor adds only those
three demonstrably called fields; it does not scan all pointer-looking words.

| Fingerprinted original walk | First field | Called fields |
| --- | --- | --- |
| D48A1..D48BE | 4674A4 | 4674A4, 4674DC, 467514 |
| D48D2..D48EE | 4674A8 | 4674A8, 4674E0, 467518 |
| D4DB7..D4DD0 | 4674B8 | 4674B8, 4674F0, 467528 |

All three loops skip null callbacks. Each nonzero target must be executable
.text; all nine observed targets pass. The original calls, ordering, argument
passing and state mutations remain generated from owned code. Metadata at the
start of each resource record and unreviewed function fields are excluded.

All 23 focused Python tests pass. Synthetic sentinels establish exact bounds,
null behavior, wrong-code/section rejection, fingerprint failure for each walk,
and exclusion of metadata, neighboring callback fields and a fourth record.
Regeneration discovers 11,760 candidate functions, 161,148 blocks, 1,121,393
instructions and the same 3,832 unsupported instruction occurrences. These
remain automatic discovery counts, not a compatibility percentage.

Native85 builds successfully with VitaSDK (-j4) and passes the 115D50 stop.
After real mainmenu data loading it stops at missing original descriptor
callback 1064E0 in B69D0, return B69ED. GET=PUT=03B94518. The last presented frame
is 198 and all pixels remain black. No menu or new draw state is claimed.
The stopped run and actual captures are archived under private/native-85-*.
Next: validate the lifecycle fields of the previously established descriptor
chain, preserving its original linking semantics and callback order.

| Private native85 artifact | SHA-256 |
| --- | --- |
| ELF | `26416ba6426408cf1954bdfc81ab9ba23148f6ee23b94f2b9104e24c82e5e934` |
| EBOOT | `fbae635127c455e394ee434d9b852f0d2dd68b6a32454052ccfef334e4a7d3c4` |
| VPK | `77ebc232f5d6bff4763af6365fe3501c35e65a95bd5f3ca0fb29a72018b452b2` |
| boot.log | `12f90035de202d5a6c537e2e595db8bbb972bfd3cc6a7cbcbcf52b58cf5bdf89` |
| channel-at-stop.json | `bff0e14833ddabb935fdee08ea5a233d2454b761e0aa9c090ca43de1c3ac1347` |
| last-presented-at-stop.bin | `3f93e75c077764ba797cf0701ff8a1cf3d5bfe386d98a015ba376985c825773a` |

Replay uses the same preserved/empty private-cache4 prerequisite described in
[map lifecycle discovery](halo2-map-lifecycle-roots.md), then
`python3 ../private/run_lab.py replay85 ../private/native-85-artifacts/halo2-boot.vpk`.
Press/release Enter/Start in :111 after the original cache copy completes.
Owned executable bytes, generated source and game-embedded packages remain
private and out of Git; packages must never be uploaded as distributable releases.
