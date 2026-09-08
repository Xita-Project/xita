# Bridge route materials and Keyes skip — September 6, 2026

A fresh Heroic a10 campaign was followed from the cryo exit through containment
and crossfire to the bridge. Its captured draws exposed sixteen missing
canonical vertex/fragment pairings, represented by 52 raw captured pairs.
The existing combiner generator produces eighteen additional fragment assets,
including cube and 2D variants of two glass-reflection programs. Existing
fragment sources remain byte-identical.

The added programs cover VS24/34/47/57 glass and model materials, VS12/60
multi-texture passes, and VS13/39 screen/depth passes. One VS39 definition reads
texture alpha with its texture mode set to NONE; this retains the generator's
existing PROJECT2D interpretation and is not a newly proven NV2A mode behavior.
No global blending, depth, or combiner-generator behavior changes.

All eighteen assets compile with zero failures. Runtime lookup assertions
reproduce missing entries against the preceding table and pass with the new
table. The shader package, HUD routing and actual cache/retention tests pass,
including ASan/UBSan, 4,644 C/Python identity checks and 2,097 source-equivalence
cases. The native build passes. The subsequent cafeteria gameplay capture
checks 598 draws with zero data mutations and no logged storage drops. It
exercises 103 raw pairings, with two further missing effect pairings preserved
for follow-up; this does not visually validate every added bridge program.

The fresh Keyes cinematic starts with director `0x120A90` and camera control 1.
Cross skips it into the original post-cinematic pistol-handoff sequence. After
that sequence, control is 0 and the director is `0x11E750`. Looking rotates the
view and walking changes its position. The camera pointer remains `0x8006BA98`.
This validates the existing XNet address-layout correction on a fresh later
cinematic, in addition to the opening skips tested earlier. The skip-entry
checkpoint, video and logs are preserved. Hardware camera validation remains
pending. The user subsequently reported AI not firing after receiving the
pistol; camera recovery is not evidence that this separate gameplay issue is
fixed.

Evidence: `/tmp/xita-bridge-skip-evidence`, with a durable copy in the packaged
September 6 bridge candidate under `/home/birchwoodgod/xita-backups/`.
