"""Invalidate owned query generations before Halo map/BSP retirement begins."""
import hashlib

ENTRIES = {
    0x58440: '3924ff434ffec9f8ce90fe82c10ad0c8d906da3ad177e495fd4e24e77df2a24a',
    0x58CD0: 'a9f13e55c80e9521bdf84c9387d687d15c8435557200eb1988d4b0471ae55234',
}


def entry(image, address):
    digest = ENTRIES.get(address)
    if digest is None or hashlib.sha256(image.bytes_at(address, 16) or b'').hexdigest() != digest:
        return []
    return ['#ifdef XV_TYPED_CLUSTER_QUERY',
            '    /* Cancel query publication before any original retirement call. */',
            '    { extern void xv_cluster_runtime_invalidate(unsigned);',
            f'      xv_cluster_runtime_invalidate(0x{address:X}u); }}', '#endif']
