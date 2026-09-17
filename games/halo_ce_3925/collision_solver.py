"""Signatures for the optional captured-collision solver experiment.

The full direct-call closure and entry caller are checked, including the
solver's switch table. Runtime admission separately checks memory ownership.
Neither check establishes atomicity of the surrounding object transaction.
"""
import hashlib

SPANS = (
    (0x170C10, 2088, "d1f9921df56c0d3ea92aec7d4a11442f4c786b5fecb6091f2c9a2aa45721d0ce"),
    (0x170980, 75, "5aa765c516cf5cacfa83e66687c0268bbc8d7f9a9f9e3a6cf42b5aa71685ebc8"),
    (0x1709D0, 123, "3ba6257642f2dd28e8ab14d3c0d260b3f4d756359bea74d2d972b7e809455857"),
    (0xB2D80, 359, "a0c4a42ebee95fa2047cf165e2a2138cf986829cead38cb41e8af48753038792"),
    (0xB1680, 304, "65a9a032e28b1330cc04d88b0e1c8608734d22b95a2c879b29f73647110adef8"),
    (0x864C0, 598, "7f5f2c05932c11df16531569ecda2860d0e8466fc68bf4669cbbd4bffa7e3d2c"),
    (0x85D10, 398, "bfe37e5b3bc0c67251f7b21dbc141ec0552552bb1b8bf7efca4f4adfef0ba2f6"),
    (0x11120, 87, "50b02b90d894fb6baeacd1a91d33a424d9e45121f15737daee6e252032098bbc"),
    (0x85A00, 777, "b6e99ce8c26b85b050cab7274b599685581d0314d50f07c0d4b138dfb51c9471"),
    (0x111A0, 39, "00d28ec5ba33b4a179b3aed8eee773171f3efa0bb22811a4e335f06ff8d32ec3"),
    (0x85720, 729, "130313c9f9f0d03c2017537575259653d72b34b10826376de9a3c9a23b202446"),
    (0x172BF0, 268, "01ad0630d9394f18cf0e963a3d323029a5f40f491437255d0a398b23131d6aeb"),
)


def matches(image):
    return all(hashlib.sha256(image.bytes_at(a, n) or b"").hexdigest() == digest
               for a, n, digest in SPANS)


ENTRY = [
    "#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_SOLVER_EXPERIMENT)",
    "    extern int xv_object_solver_begin(xctx *);",
    "    extern void xv_object_solver_end(int *);",
    "    int xv_object_solver_token_ __attribute__((cleanup(xv_object_solver_end))) = xv_object_solver_begin(c);",
    "#endif",
]
