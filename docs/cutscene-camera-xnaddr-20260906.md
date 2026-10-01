# Cutscene camera and the Halo 3925 network address ABI

The camera control state was being corrupted before campaign loading. Halo
passes `0x2FA310` to `XNetGetTitleXnAddr`. Its 12-byte address record ends
immediately before the camera-state pointer at `0x2FA31C`. Our adapter used
the later 36-byte XNADDR layout, clearing 24 bytes beyond the buffer and
writing the MAC address at offset 10. The MAC's last four bytes changed the
camera pointer from `0x8006BA98` to `0x563412F2`.

The invalid pointer lands on the runtime's unmapped-memory fallback page.
That page is outside the checkpoint region. Cinematics therefore set their
camera flag there, but restoring a checkpoint cannot restore the flag.
The director's own initialization at `0x120470` reads the surviving flag and
reapplies the cinematic camera after a skip.

Halo's `cinematic_skip_start` saves a checkpoint before playing the cinematic.
Skipping restores that checkpoint; it does not fast-forward the cinematic
script. With the correct pointer, the original game's recovery code can
restore the camera state without a timer or a forced camera-mode change.

The address adapter now uses the ABI from this executable:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | Record length, 12 |
| 2 | 6 | Ethernet MAC address |
| 8 | 4 | IPv4 address |

The local original `default.xbe` is the reference: `0x1AF488` jumps to
`0x1B2352`, which writes exactly these fields and returns 2 (Ethernet).
The address conversion function at `0x1AF471` jumps to `0x1B3067` and compares
the MAC at offset 2. Both adapter functions now use the same layout and
translate copies across guest page boundaries. Existing loopback and opt-in
ad-hoc routing behavior is retained. `XNetCreateKey` was also checked against
the original: its 8-byte ID and 16-byte key sizes were already correct.

Validation:

- A regression test with a camera pointer immediately after the address
  reproduces the overwrite with the old adapter.
- `tools/tests/net_hle.sh` passes AddressSanitizer and UndefinedBehaviorSanitizer.
  It checks both transports, all field alignments across noncontiguous pages,
  adjacent-byte preservation, and the existing socket behavior.
- The Vita game build passes. Read-only inspection in Vita3K confirms the
  camera pointer remains `0x8006BA98` after main-menu initialization. The
  ordinary Campaign menu still resumes the cryo checkpoint in first person.

- Restart Level followed by Cross during the opening space shot now restores
  the cryo first-person view. The checkpoint read restores camera control 0;
  the old build restored control 1 from the unmapped fallback page.

The old opt-in `XV_CAM_FIX` timed camera override has been removed; it could
interrupt normal cinematics and was based on an incorrect skip model.
Temporary diagnostic edits to generated code and file I/O have been restored.
A second Restart Level / opening-skip cycle also restores first person and the
look tutorial. The clean build also passes resume and opening-skip, and the full
natural opening reaches the cryo tutorial with camera control 0. A subsequent
[fresh Keyes skip](bridge-materials-20260906.md) also returns camera control 0
and the first-person director, with working look and movement. Hardware
validation remains pending.
