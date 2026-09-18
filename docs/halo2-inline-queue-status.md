# Halo 2 inline queue-status reads

Native79 passes the three original inline graphics-status reads in `12D0A0`
after a real Start-key press in the movie path. It reaches a new strict stop at
class 62 surface source-offset setup, subchannel 3 / method 0308 / value 038E8000.
The game has allocated a 1,228,864-byte contiguous buffer while preserving its
display image. No framebuffer copy is executed yet. The last presented image
and actual desktop capture remain black; no main menu is visible.

The H2 host-channel profile admits only instructions 12D0D5, 12D0DB and 12D0E3,
respectively MOV from PUT, CMP against GET, and a conditional MOV from PGRAPH
busy. Their 26-byte enclosing sequence and the whole owned XBE are fingerprinted.
The lowering also checks opcode, operand widths/registers/addressing and segment.
It reuses existing scalar MOV/CMP bus emission and preserves the original branch,
register and flag behavior. Only these three reads change in the regenerated
source (`code_033.c`); all candidate/unsupported counts remain unchanged.
Shared/default and the plain graphics profile retain their existing behavior.

The values come from the actual command consumer. Pending packets report busy
and unequal GET/PUT; a fully consumed queue reports equal pointers and idle.
No constant success or fake idle value is substituted. Unknown bus addresses
still stop. Host tests check both pending and completed queue states, and
synthetic instruction tests check precise admission and shape rejection.
All 23 host executables plus timed/active cases and six focused Python tests pass.

Private native79 terminal GET=03B70934, PUT=03B70978, two packet words remaining;
submitter 3FAC58. The last display header is `{960,544,3840,108}` with all 522240
pixels FF000000. The captured copy sequence follows AvGetSavedDataAddress and
MmAllocateContiguousMemoryEx(0012C040), returning 8347C000. The next task is to
validate the exact source/destination surface and blit contract, then implement
the actual bounded copy with preservation/rejection tests.

| Private artifact | SHA-256 |
| --- | --- |
| halo2-boot.elf | `4a7949f43f32d869811b2d9906721594786921a3ed45b70446d870dd5ce2c30f` |
| eboot.bin | `96e5169b64cf01245871d1315b44a47e6227728d0b37076444d8c0c475a1426f` |
| halo2-boot.vpk | `33a0bd2ad367855f3b96c4d8fe75a9639c0b17c77412999ac98b0cfdaa637fc1` |
| boot.log | `483ed5e19ba8190bd1018f22c70c0c4cdcb3a2c2f898f95bc85ce7b2f0a02070` |
| channel-at-stop.json | `d47fc00dd242efb6b77c021c4c385116df9c7df55a152977c2de57b17c8da922` |
| last-presented-at-stop.bin | `1b88f915e2cdd04be1a1d686ad2c64364bb002986bc3d6af6df7e84fe8cc6ed0` |

Replay from the isolated source directory:

```sh
python3 ../private/run_lab.py replay79 ../private/native-79-artifacts/halo2-boot.vpk
```

Allow the movie loop and map-header reads to begin, then press/release Enter
(mapped to Start) in the own :111 window. The captured run pressed it after
roughly 8 and 40 seconds; input timing changes frame/command counts. The private
helper `../private/press_start.py` sends only that key to display :111.
Generation uses the unchanged owned XBE and
`prepare_boot.py --host-channel --audio-unavailable`, frozen at
`private/inline-queue-status/generated`. Build selection remains HOST_CHANNEL=1,
QUAD_RENDER=1 with the private original shader contract. The game-embedded VPK
must remain private and must not be uploaded as a distributable release.
