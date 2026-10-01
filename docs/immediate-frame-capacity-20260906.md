# Immediate frame storage — September 6, 2026

The combined rendering fixes restore flare submissions that previously never
reached GXM. A full natural opening cinematic then exhausted the shared 64 KiB
immediate-vertex/constant-attribute pool. The trace logged up to 960 rejected
immediate submissions in one sampled frame after retaining 65,520 bytes.
These were separate from the already corrected index-buffer shortage.

The pool now holds 256 KiB per frame, adding 384 KiB across the two frames.
Immediate vertices and persistent attribute snapshots use one bounded,
16-byte-aligned copy helper. Requested bytes are counted before rejection and
reported alongside retained bytes. Neither source pointers nor the preceding
frame's data are reused prematurely; the diagnostic does not silently enlarge
the pool at runtime.

The real allocation regression retains another 960 four-vertex, 40-byte flare
quads after the observed 65,520-byte prefix. It checks their contents after
caller scratch changes, the preceding frame's attribute values, alignment,
overflow rejection and requested-byte accounting. A 64 KiB configuration fails
the retained-flare assertion. The 256 KiB configuration passes host and
ASan/UBSan checks, visibility regressions, and the native Vita build.

With clipping still disabled, a fresh complete natural opening cinematic
returns to the cryo tutorial in Vita3K with no dropped draws or missing constants.
Sampled peaks are 219,840 immediate bytes and 1,753 of 2,048 commands. Hardware
validation remains pending. The separate [flare rejection step](cpu-flare-draws-20260906.md)
reduces unnecessary submissions rather than relying on this larger pool alone.

Evidence: `/home/birchwoodgod/xita-backups/2026-09-06-043235-immediate-capacity-local/`.
