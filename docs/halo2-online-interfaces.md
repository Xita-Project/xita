# Halo 2 static online-state interfaces

Native59 reaches `73062`, which invokes virtual slot `+1C` on object `47708C`.
The pinned image binds it to table `450B68`, whose slot selects original method
`73360`. This is a discovery stop, not an original game assertion.

The original update at `59949..59972` calls `73040` on both `47708C` and
`47712C`, then calls `737B0` on the latter. These two static objects bind to
`450B68` (eight slots) and `450B44` (nine slots). The first table is preceded
by the string `online tasks`; the second ends at string data. The complete
bounded span is `450B44..450B88`. Each of its 17 entries targets executable
`.text`, and preparation checks both object bindings and the 41-byte original
call sequence fingerprint
`c68f2b75408f155325c537d83f024b3ff580f096399c7606ca83a739654555a1`.

Preparation includes the original methods as discovery roots; it does not
replace their calls, alter state, provide an online service, or report success.
The shared discovery rules and Halo CE remain unchanged. Synthetic tests
check every slot, a changed second-object binding, an invalid final target,
and a mismatched caller fingerprint. All 14 callback-root tests pass.
Owned disassembly and generated sources stay private.

Native60 executes both interfaces and reaches a new original graphics
submission. The strict consumer stops at `1AF0 = 00000000`, source/GET
`03B43E54`, PUT `03B43F30`, invoked by the original kick at `3FAC58`.
Two real clears have completed (921,600 cumulative pixels) and one semaphore
release wrote value 5. The rejected packet also contains subsequent words at
`1AF4..1AFC`; they have not been accepted or executed. The decoded channel is
complete and parses. No main-menu map load, geometry or menu is observed.
The first scanout is still black with SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
The video records startup and the terminal return to the Vita3K library;
`native-60-view/window-2.5.png` already includes the library UI.

| Private native60 artifact | SHA-256 |
| --- | --- |
| ELF | `1d289029c2e9f88d0628afe4d67baddc649aa5f246d3498942be249d493b64fb` |
| EBOOT | `d34731891a45958b12a8ac1a9ba4b42da179245274f7b83027d2162bab8f6d1c` |
| VPK | `ae0ff77e614721db63c8a25948074fcb2baa2433cd87c81e1f06c8a84d2f0854` |
| Boot trace | `ace6242f8821f6674b450f646d002544419d50e4d9d5a5136c81017c0bc5db81` |
| Decoded channel | `9ed4954a080ef12318618211037a09257e172139cd44ed4e87b6b58833f89435` |

The private generated source is frozen in `online-interfaces/generated`.
Replay the exact diagnostic package from the source directory with
`python3 ../private/run_lab.py replay60 ../private/native-60-artifacts/halo2-boot.vpk`.
It uses the explicit unavailable-audio and limited native FP diagnostics.
The package embeds owned game image/code and must not be uploaded as a
release or distributed. The ordinary audio path still stops at Xbox APU
hardware; the limited FP bridge does not emulate isolated x87/SSE exceptions.
