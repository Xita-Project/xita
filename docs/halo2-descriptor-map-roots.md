# Halo 2 descriptor map setup and cleanup

Native85 reaches original callback 1064E0 through descriptor 468248+18 in
B69D0, return B69ED, after loading mainmenu content. The paired cleanup function
B6AB0 uses descriptor+1C. Both walks start from 4E0330, follow descriptor+C4,
and skip null callbacks. Their complete walk fingerprints are checked.

The extractor reuses the original descriptor-link algorithm already validated
for startup. It does not substitute recursive traversal or ordinary deduplication:
shared/self children and link overwrites retain their original semantics. Only
fields 18/1C of the established chain nodes are inspected. Metadata and neighboring
fields are excluded. The owned 17-node chain yields three unique targets,
including the previously translated original return-only callback 175F40; only
1064E0 and 106500 are new functions. Calls, order and state writes remain original.

All 24 focused Python tests pass. New synthetic fixtures preserve self/shared-child
linking, nulls and duplicate targets, verify first/last callback failures, reject
non-executable targets and initial links, test both caller fingerprint failures,
and use unvisited-data sentinels. The input image bytes remain unchanged.
Regeneration reports 11,762 candidate functions, 161,159 blocks and 1,121,460 instructions,
with 3,832 unsupported occurrences unchanged. These are automatic discovery counts,
not gameplay or menu compatibility.

Native86 executes the original setup callback and finishes the 59,670,016-byte
mainmenu cache copy. Without injected input, it reaches a null virtual call at
21E3E8 in 21E3B0, return 21E3EA, after the movie phase. Static inspection identifies
the selected object at sound-state+2AB4; the diagnostic DirectSound creation failure
is a plausible cause and requires read-only caller/state validation next. This is
not another missing callback that can be fixed by adding a discovery root.
The final scanout is frame 557, with every pixel FF000000. No visible menu is claimed.

Private evidence: `../private/native-86-artifacts`, `native-86-view`, and
`native-milestone-86.json`. Frozen generated source is
`../private/descriptor-map-lifecycle/generated`. SHA-256 values:

- ELF: `f9bcb8f3c68ef5bb44c59c1a64effa1a86b114937510c5008b097f9e924d645d`
- EBOOT: `b23399d66bd475a866cc645d392e1f4fe18419ea937ce2b12076e19400578119`
- VPK: `ea8f7540b8202eee356180ce8ebddfe3a614774916f94cc14f5d656a7c2d1120`
- Trace: `0deb9f1e501361e61aae9060f2c8aeb7edea936266f64267ee15d89f6cdd1d1a`
- Channel JSON: `7b4cfd10f90539a1dc2c8989c32e1095593658e105ed88d3f35c4a9c2d548b39`
- Frame: `e9a77359c987d6322d057c67a83a6367f7e8e61ed16cb35e76a89b8c56bee49d`

After stopping only the private Halo 2 emulator and preserving its populated cache4
by renaming it to an unused private path, create an empty cache4 directory. Replay
from `../private` with `python3 run_lab.py REPLAY_NAME native-86-artifacts/halo2-boot.vpk`.
Allow the original formatter and map copy to finish; no Start input is required.
This fresh-volume diagnostic retains the populated raw-volume rejection described
in [cache diagnostics](halo2-exit-cache-diagnostics.md).
Owned executable bytes, generated code, map data, caches and diagnostic packages
remain private and outside Git. Game-embedded packages must never be uploaded
as distributable releases.
