# Halo 2 XPP hardware boundary

Native attempt 38 reaches XPP device initialization and stops on the missing
kernel implementation `IoCreateDevice`, called at `0x004089E7` for the original
device driver. No controller driver, menu or game frame is claimed.

Preparation now includes the exact six-descriptor initialization walk observed
at `0x00408C72`: slots `0x004086D4..0x004086EC`, callback at descriptor +4. The
walk's 36 bytes are fingerprinted and every non-null descriptor/callback is
validated in the XPP section. The four distinct initialization roots are
`0x0040975D`, `0x00408940`, `0x00408A4C`, and `0x00409347`. Synthetic tests cover
duplicate roots, skipped null descriptors, invalid descriptors/targets and a
changed caller fingerprint.

The host graphics shutdown also restores its saved modeled PMC engine and
interrupt masks. The original helper saved `0x01110000` before enabling engines;
leaving all gates enabled was an incomplete retirement. Native attempt 38 now
records `0x01110000` both before the first constructor and before the second,
with restoration at `0x003FE64F`. Saved fixed PFB geometry is checked before any
retirement mutation. All 13 host tests and runtime ASan/UBSan pass, including
invalid saved masks and initialization after retirement.

The subsequent original USB path would access OHCI at `0xFED00000..0xFED01000`.
The H2 diagnostic pointer checker now rejects that range explicitly to prevent
unmapped hardware from aliasing the trash page. Attempt 38 stops earlier at
`IoCreateDevice`, so it does not validate an actual OHCI access. No USB register
model or fake kernel device was added. The next task is an H2-only controller
API boundary backed by real Vita input, with original game callers retained.

| Native attempt 38 artifact | SHA-256 |
| --- | --- |
| ELF | `d0d529f918b3a21ed92e8947ad73c6e89995f0a77d193201c32711bdc77ea450` |
| EBOOT | `5132ca677d833dab0f5e9bce26ca9e27c35956aa646d7c7a7257870d3493caea` |
| boot.log | `8df7033b9a9956ff22f114cfae64bdf2d979efd7529e1e28d8c3a52127732d3b` |

Only the first black initialization framebuffer has been presented. The native
logs/captures and game-embedded package remain private and outside Git.
