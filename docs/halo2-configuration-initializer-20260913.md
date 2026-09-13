# Native 43: network configuration initializer

The existing unavailable-audio diagnostic now executes the original
`network_configuration.dat` default initializer through record `467140`.
The only additional root comes from the observed pointer path
`[477058] -> 467140`, callback at record offset `10h -> 662E0`.
That ten-byte body calls `66330`, returns AL=1 and uses RET4. The existing
whole-XBE revision check applies before reading the record.

Native attempt 43 advances through further game memory allocation and stops
at an undiscovered arena-allocation method `124700`, called at `14B4C2`
through vtable `453498` slot zero, requesting `2210h` bytes. This is another
original-code boundary, not a graphics instruction or rendered output.
The neighboring two-slot table is allocate `124700`, free `72C70`; following
bytes are string data. This milestone has not added those targets yet.

All 21 Python regressions pass. The native private build uses four jobs,
guest O0/runtime O1, with the explicit `--audio-unavailable` profile. There is
still no menu, geometry or audio. The normal profile's audio-hardware stop
remains unchanged. The emulator was captured, archived and stopped.

| Native 43 audio-error diagnostic artifact | SHA-256 |
| --- | --- |
| ELF | `fc307b229c742d83a343e4d35909043ae02425df351e3ce9b10cbd58c92754ab` |
| EBOOT | `0bb0bbe8ef614e36f6a64c00ba154eb656abb442a110a099e86adfb9ceef3c5c` |
| Boot trace | `dc166ef5fbbd66bea9be392276b8831926b3944dd13d3c2f6b60842afdf68cfc` |

Private evidence: `native-43-artifacts`, `native-43-view`,
`native43-vtable-audit.txt`, `native-milestone-43.json`, and
`game-init-regressions.log`. Owned generated code, captures and game-embedded
packages remain private and must not be distributed as releases.
