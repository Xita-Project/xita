/* Xbox SHA-1 exports used by XAPI's original checkpoint-signing code.
 * The guest owns the 116-byte context: 24 reserved bytes followed by the SHA
 * state, bit count and block buffer. No host pointers live in guest memory. */
#include "xk.h"
#include <stdlib.h>

typedef struct {
    uint32_t reserved[6], h[5], bits_lo, bits_hi;
    uint8_t block[64];
} sha_ctx;
_Static_assert(sizeof(sha_ctx) == 116, "Xbox SHA context layout");

static uint32_t rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }
static void sha_block(sha_ctx *s, const uint8_t *p)
{
    uint32_t w[80];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) |
               ((uint32_t)p[4*i+2] << 8) | p[4*i+3];
    for (unsigned i = 16; i < 80; ++i) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (unsigned i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha_init(sha_ctx *s)
{
    memset(s, 0, sizeof *s);
    s->h[0] = 0x67452301; s->h[1] = 0xefcdab89; s->h[2] = 0x98badcfe;
    s->h[3] = 0x10325476; s->h[4] = 0xc3d2e1f0;
}
static void sha_update(sha_ctx *s, const uint8_t *p, uint32_t n)
{
    unsigned used = (s->bits_lo >> 3) & 63;
    uint32_t old = s->bits_lo;
    s->bits_lo += n << 3;
    s->bits_hi += (n >> 29) + (s->bits_lo < old);
    while (n) {
        unsigned take = 64 - used; if (take > n) take = n;
        memcpy(s->block + used, p, take); p += take; n -= take; used += take;
        if (used == 64) { sha_block(s, s->block); used = 0; }
    }
}
static void sha_final(sha_ctx *s, uint8_t digest[20])
{
    uint8_t end[72] = {0x80};
    unsigned used = (s->bits_lo >> 3) & 63, padding = used < 56 ? 56 - used : 120 - used;
    uint64_t bits = ((uint64_t)s->bits_hi << 32) | s->bits_lo;
    for (unsigned i = 0; i < 8; ++i) end[padding + i] = (uint8_t)(bits >> (56 - 8*i));
    sha_update(s, end, padding + 8);
    for (unsigned i = 0; i < 20; ++i) digest[i] = (uint8_t)(s->h[i/4] >> (24 - 8*(i%4)));
}
void xk_XcSHAInit(xctx *c)
{
    sha_ctx s; sha_init(&s); x_guest_write(X_ARG(0), &s, sizeof s); X_RET(1);
}
void xk_XcSHAUpdate(xctx *c)
{
    uint32_t ctx = X_ARG(0), p = X_ARG(1), n = X_ARG(2);
    sha_ctx s; x_guest_read(&s, ctx, sizeof s);
    while (n) {
        uint32_t take = 4096 - (p & 4095); if (take > n) take = n;
        sha_update(&s, X_G(p), take); p += take; n -= take;
    }
    x_guest_write(ctx, &s, sizeof s); X_RET(3);
}
void xk_XcSHAFinal(xctx *c)
{
    sha_ctx s; uint8_t digest[20]; uint32_t ctx = X_ARG(0);
    x_guest_read(&s, ctx, sizeof s); sha_final(&s, digest);
    x_guest_write(ctx, &s, sizeof s); x_guest_write(X_ARG(1), digest, sizeof digest); X_RET(2);
}

static uint32_t signature_key, hd_key;

/* Halo 3925's 104-byte game variants use XAPI's flags=0 title HMAC. Keep this
 * bounded repair separate from the still-experimental profile/checkpoint signing
 * path. The old Begin stub returned INVALID_HANDLE and left the digest as stack
 * garbage; 0x2F800 then silently loaded default Slayer under the custom name. */
static int saved_record_signature(uint32_t data, unsigned length, uint8_t digest[20])
{
    if (!signature_key || (length != 48 && length != 104)) return 0;
    uint8_t key[16], pad[64], record[104]; sha_ctx s;
    x_guest_read(key, signature_key, sizeof key);
    x_guest_read(record, data, length);
    for (unsigned i = 0; i < 64; ++i) pad[i] = (i < 16 ? key[i] : 0) ^ 0x36;
    sha_init(&s); sha_update(&s, pad, 64); sha_update(&s, record, length); sha_final(&s, digest);
    for (unsigned i = 0; i < 64; ++i) pad[i] = (i < 16 ? key[i] : 0) ^ 0x5c;
    sha_init(&s); sha_update(&s, pad, 64); sha_update(&s, digest, 20); sha_final(&s, digest);
    return 1;
}

/* The generated Default/Inverted cache records have no user name, the -1
 * profile identifier, kind 1 and matching preset/inversion bytes. Recognize
 * only these built-in preferences; named profiles and checkpoints retain
 * their existing signing path. */
static int builtin_profile(uint32_t data)
{
    uint8_t record[48]; x_guest_read(record, data, sizeof record);
    for (unsigned i = 0; i < sizeof record; ++i)
        if (i != 24 && i != 25 && i != 26 && i != 27 && i != 42 && i != 43 && record[i])
            return -1;
    if (record[24] != 255 || record[25] != 255 || record[26] != 1 ||
        record[27] > 1 || record[42] < 1 || record[42] > 10 || record[43] != record[27])
        return -1;
    return record[27];
}

int xk_builtin_profile_recover(uint32_t data, unsigned preset)
{
    uint8_t digest[20], old[20];
    if (preset > 1 || builtin_profile(data) != (int)preset ||
        !saved_record_signature(data, 48, digest)) return 0;
    x_guest_read(old, data + 48, sizeof old);
    if (!memcmp(old, digest, sizeof old)) return 0;
    /* Only called for the two generated cache files. Repair the guest copy,
     * including old stack-residue signatures; never rewrite the disk file. */
    x_guest_write(data + 48, digest, sizeof digest);
    return 1;
}

/* stdcall void HaloSignSavedRecord(data, uint16 length, digest), 0x2D120.
 * Other records retain the original XAPI call sequence, including its
 * unchanged output when Begin fails in unsigned-profile compatibility mode. */
void xv_hle_HaloSignSavedRecord(xctx *c)
{
    uint32_t data = X_ARG(0), len = (uint16_t)X_ARG(1), out = X_ARG(2);
    uint8_t digest[20];
    if ((len == 104 || (len == 48 && builtin_profile(data) >= 0)) &&
        saved_record_signature(data, len, digest)) {
        x_guest_write(out, digest, sizeof digest);
        c->r[0] = 0; X_RET(3);
    }
    X_PUSH32(c->r[6]);
    X_PUSH32(0); X_PUSH32(0x2D128u); xv_call(c, 0x15BA5u);
    c->r[6] = c->r[0];
    if (c->r[6] != 0xFFFFFFFFu) {
        X_PUSH32(len); X_PUSH32(data); X_PUSH32(c->r[6]);
        X_PUSH32(0x2D140u); xv_call(c, 0x15BE3u);
        X_PUSH32(out); X_PUSH32(c->r[6]);
        X_PUSH32(0x2D14Bu); xv_call(c, 0x15BFDu);
    }
    c->r[6] = X_POP32(); X_RET(3);
}

int xk_variant_recover_unsigned(uint32_t data)
{
    uint8_t record[124], digest[20];
    x_guest_read(record, data, sizeof record);
    /* Only recognize the two observed old-stub signatures: all zero, or the
     * zero DWORD followed by a leftover Xbox save path in generated playlists.
     * Other signatures, including damaged signed variants, retain validation. */
    static const uint8_t zero[20];
    int empty = !memcmp(record + 104, zero, 20);
    int old_path = !memcmp(record + 104, zero, 4) &&
        (record[108] == 'u' || record[108] == 'z') &&
        record[109] == ':' && record[110] == '\\';
    for (unsigned i = 111; old_path && i < 124; ++i)
        if (record[i] < 32 || record[i] > 126) old_path = 0;
    if (!empty && !old_path) return 0;
    /* Basic record bounds before recovering an unsigned preferences file. */
    uint32_t kind, teams, weapons, vehicles; float health;
    memcpy(&kind, record + 24, 4); memcpy(&teams, record + 28, 4);
    memcpy(&health, record + 60, 4); memcpy(&weapons, record + 68, 4);
    memcpy(&vehicles, record + 72, 4);
    int terminated = 0;
    for (unsigned i = 0; i < 24; i += 2)
        if (!record[i] && !record[i+1]) terminated = 1;
    if (!terminated || kind < 1 || kind > 5 || teams > 1 || weapons > 10 ||
        vehicles > 4 || !(health > 0 && health <= 16)) return 0;
    if (!saved_record_signature(data, 104, digest)) return 0;
    x_guest_write(data + 104, digest, sizeof digest);
    return 1; /* guest copy only; disk file and gameplay settings are unchanged */
}

uint32_t xk_crypto_export(unsigned ordinal)
{
    return ordinal == 325 ? signature_key : ordinal == 323 ? hd_key : 0;
}
void xk_crypto_init(uint32_t image_base, uint32_t image_size)
{
    /* Retail title-key derivation: HMAC-SHA1(certificate key, XBE signature key),
     * truncated to 16 bytes. Public format reference: PMStanley/xbox-save-sig.
     * XboxHDKey is a stable virtual-console key (zero), not a physical Xbox's
     * EEPROM key; non-roamable saves from a physical Xbox are not supported. */
    static const uint8_t certificate_key[16] = {
        0x5c,0x07,0x33,0xae,0x04,0x01,0xf7,0xe8,0xba,0x79,0x93,0xfd,0xcd,0x2f,0x1f,0xe0
    };
    uint8_t key[16] = {0}, digest[20], pad[64]; sha_ctx s;
    uint32_t cert = image_size >= 0x11c ? X_M32(image_base + 0x118) : 0;
    if (cert < image_base || (uint64_t)cert + 0xd0 > (uint64_t)image_base + image_size) {
        XK_LOG("crypto: XBE certificate unavailable; save signing disabled\n");
        return;
    }
    x_guest_read(key, cert + 0xc0, sizeof key);
    for (unsigned i = 0; i < 64; ++i) pad[i] = (i < 16 ? certificate_key[i] : 0) ^ 0x36;
    sha_init(&s); sha_update(&s, pad, 64); sha_update(&s, key, 16); sha_final(&s, digest);
    for (unsigned i = 0; i < 64; ++i) pad[i] = (i < 16 ? certificate_key[i] : 0) ^ 0x5c;
    sha_init(&s); sha_update(&s, pad, 64); sha_update(&s, digest, 20); sha_final(&s, digest);
    signature_key = xk_kalloc(16); hd_key = xk_kalloc(16);
    if (!signature_key || !hd_key) { XK_LOG("crypto: cannot allocate signing keys\n"); abort(); }
    x_guest_write(signature_key, digest, 16);
    memset(key, 0, sizeof key); x_guest_write(hd_key, key, sizeof key);
}
