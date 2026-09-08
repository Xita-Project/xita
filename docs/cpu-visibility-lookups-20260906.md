# Visibility result lookup — September 6, 2026

The visibility-query implementation searched all preceding result slots whenever
Halo ended a query or read its result. Reusing IDs 0 through 383 in order costs
147,840 slot visits for one issue/read cycle, even though these IDs normally
already occupy the matching array entries.

Issue and read now first check the ID's own slot, including its bounds, occupied
flag and stored ID. A match avoids the linear search. A miss retains the existing
search and first-empty allocation policy, so arbitrary 32-bit IDs and different
issue orders remain valid. The matching 384-ID case needs 768 direct slot checks
per cycle. This describes lookup work, not total query cost or a Vita FPS gain.
GPU counters, frame fences, result generations and atomic publication are unchanged.

The real HLE/recorder test now checks all 512 IDs in sequential and permuted
orders, including collisions with another ID in the direct slot, pool exhaustion,
pending outputs, repeated generations and 200,000 asynchronous handoffs across
the direct and fallback paths. Native compilation and ASan/UBSan pass. ARM
disassembly confirms a successful checked lookup bypasses the search loop while
retaining memory barriers for result publication. The native candidate resumes
the cryo checkpoint in Vita3K with the room, glass, technician and light effects
visible. The fixed view retains 115 flare quads and rejects 134 per frame, matching
the preceding candidate, with no logged draw-storage drops. Vita timing remains
unmeasured.
