"""Sampled collision attribution within the existing object callback hold."""
import hashlib

# Stable IDs shared with the runtime report. Full function spans (including
# alignment where present) qualify the diagnostic without changing game code.
SPANS = (
    (0x478D0, 1424, "52d878ae121efb31dda8a5612528210985a8bb21f4be2684e629832820c05b88"),
    (0x49600, 5104, "fc3870d76c5a5e0e9dc0c4966ef73bc051b73de466f4e41273cef8864365b43b"),
    (0x172BF0, 496, "d8625558bc0ee82a6883763e0b31fb9a40eefedc8668d21f2dfa37e0d6987241"),
    (0x171F10, 672, "39eb1cabc77888d1f879bc5980471d399fbec45bf5727078deaebd44d10d144e"),
    (0x170C10, 2088, "d1f9921df56c0d3ea92aec7d4a11442f4c786b5fecb6091f2c9a2aa45721d0ce"),
    (0x1721B0, 2624, "0fe1f1b9913c1865701b9a103beeee2584678d46d8794c9f3fe8cb93f108e437"),
    (0x88110, 122, "04539a46b497608427862944a0e3369648e9e6f8d4d9f5de9fd5bc974286119f"),
    (0x868F0, 273, "c09a6bd0479334c5b029bbcdb021322e4200dde15ce3eb53ce65385251fd4dc1"),
    # Includes the switch table and padding through the next function boundary.
    (0x1716F0, 1024, "fb7cc5aadaff2af622d2a8e9a6039faa45004e2a4f62d4e7dc6aa321271ebcbb"),
)


def entry(image, address):
    for index, (pc, size, digest) in enumerate(SPANS):
        if address == pc and hashlib.sha256(image.bytes_at(pc, size) or b"").hexdigest() == digest:
            return [
                "#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_HOLD_PROFILE)",
                "    extern unsigned xv_object_hold_children_enabled;",
                "    extern unsigned xv_object_motion_begin(xctx *, unsigned);",
                "    extern void xv_object_motion_end(unsigned *);",
                "    unsigned motion_sample_ __attribute__((cleanup(xv_object_motion_end))) =",
                f"        xv_object_hold_children_enabled ? xv_object_motion_begin(c, {index}u) : 0;",
                "#endif",
            ]
    return []
