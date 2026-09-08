"""Version-specific dependency guards shared by the emitter and staged updater."""
import hashlib

# Filled from the unmodified Halo 3925 XBE, not the generated C or map data.
IMAGE_SHA256 = "4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae"
ENTRY = 0x60460
ENTRY_HOOK = "    { extern int xv_flare_defer(xctx *); if (xv_flare_defer(c)) return; }"
# Values match xk_flare.h. Collection waits for an active drain but allows pending
# metadata to coexist with new records; the identity-write guard is separate.
BARRIERS = {0x5FE30: 2, 0x5FE80: 6, 0x5FF86: 3, 0x60560: 1,
            0x606B0: 2, 0x80570: 4, 0xEC680: 4}


def matches_image(image):
    return hashlib.sha256(image.data).hexdigest() == IMAGE_SHA256


def barrier_line(address):
    return ("    { extern void xv_flare_barrier(unsigned); "
            f"xv_flare_barrier({BARRIERS[address]}u); }}")
