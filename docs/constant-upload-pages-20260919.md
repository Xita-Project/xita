# Shader constants crossing guest pages

`D3DDevice_SetVertexShaderConstant` previously translated only the first byte
of each 16-byte constant and copied the entire row from that host pointer.
Adjacent guest pages can map to separate host allocations. A row crossing their
boundary therefore read unrelated bytes instead of the next guest page. The
optional histogram preview had the same assumption.

Both reads now use the existing `x_guest_read` helper. Single-page reads retain
its inline copy; crossing reads translate each page. Register clipping,
non-finite normalization, dirty-range union and guest return behavior are
unchanged. This is a correctness fix, not an established performance improvement
or a diagnosis of a particular reported visual glitch.

The production upload test now also extracts the actual page-read helpers.
It checks independently mapped pages with poison gaps, every start offset from
4081 through 4097, guest-address wrap, and histogram preview on/off. The original
contiguous-copy implementation fails the new constant-value assertion.

Validation: 596 upload cases pass under ASan/UBSan, and the existing 8,192
constant-tracking sequences pass. The runtime translation unit also compiles
with VitaSDK for Cortex-A9. The change is saved for the next build; perf.38
remains installed and contains the earlier UV-cache update.
