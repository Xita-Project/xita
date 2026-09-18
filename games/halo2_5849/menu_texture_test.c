#include "menu_texture.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t A(uint32_t p){return p>>24;} static uint8_t R(uint32_t p){return (p>>16)&0xFF;}
static uint8_t G(uint32_t p){return (p>>8)&0xFF;} static uint8_t B(uint32_t p){return p&0xFF;}

static void test_dxt1_solid(void)
{
    /* c0=white(0xFFFF) c1=black(0x0000), all indices 0 -> all white */
    uint8_t blk[8] = {0xFF,0xFF, 0x00,0x00, 0,0,0,0};
    uint32_t out[16];
    menu_dxt1_block(blk, out, 4);
    for (unsigned i=0;i<16;++i){ assert(R(out[i])==255&&G(out[i])==255&&B(out[i])==255&&A(out[i])==255); }
}
static void test_dxt1_indices(void)
{
    /* c0=red(0xF800) c1=blue(0x001F); index 1 everywhere (bits=0x55555555) -> blue */
    uint8_t blk[8] = {0x00,0xF8, 0x1F,0x00, 0x55,0x55,0x55,0x55};
    uint32_t out[16];
    menu_dxt1_block(blk, out, 4);
    for (unsigned i=0;i<16;++i){ assert(B(out[i])==255 && R(out[i])==0); }
}
static void test_dxt3_alpha(void)
{
    /* alpha bytes: 0x0F,0x00... -> texel0 alpha=15->255, texel1 alpha=0 */
    uint8_t blk[16];
    for(int i=0;i<8;++i) blk[i]=0;
    blk[0]=0x0F;   /* texel0 nib=0xF, texel1 nib=0x0 */
    blk[8]=0xFF; blk[9]=0xFF; blk[10]=0; blk[11]=0; blk[12]=0;blk[13]=0;blk[14]=0;blk[15]=0; /* white color */
    uint32_t out[16];
    menu_dxt3_block(blk, out, 4);
    assert(A(out[0])==255 && R(out[0])==255);   /* texel0 opaque white */
    assert(A(out[1])==0);                        /* texel1 transparent */
}
static void test_dxt5_alpha(void)
{
    /* a0=255 > a1=0: 8-entry ramp; index bits: texel0 -> 0 (255), texel1 -> 1 (0), texel2 -> 2 (219) */
    uint8_t blk[16] = {0};
    blk[0] = 255; blk[1] = 0;
    blk[2] = (uint8_t)((1u << 3) | (2u << 6)); /* texel1 idx1, texel2 idx2 (low 2 bits of idx2) */
    blk[3] = 0;                                 /* idx2 high bit 0 */
    blk[8] = 0xFF; blk[9] = 0xFF;               /* white colour, indices 0 */
    uint32_t out[16];
    menu_dxt5_block(blk, out, 4);
    assert(A(out[0]) == 255 && R(out[0]) == 255);
    assert(A(out[1]) == 0);
    assert(A(out[2]) == (6 * 255 + 1 * 0) / 7);   /* ramp entry 2 */
}
static void test_a8_and_a4r4g4b4_layouts(void)
{
    /* A8 texel is alpha-only with black colour; A4R4G4B4 expands nibbles to 0..255. */
    fmt_desc d;
    assert(describe_format(0x19, &d) && d.kind == PF_A8 && d.bytes == 1 && !d.linear);
    uint8_t a8 = 0x7F;
    assert(convert(PF_A8, &a8) == 0x7F000000u);
    uint8_t px[2] = {0x2F, 0xA1};               /* 0xA12F: A=0xA R=1 G=2 B=0xF */
    assert(convert(PF_A4R4G4B4, px) == 0xAA1122FFu);
    assert(describe_format(0x1F, &d) && d.linear && d.kind == PF_A8);
    assert(describe_format(0x0B, &d) && d.kind == PF_P8 && d.bytes == 1 && !d.linear);
}

/* Mip chains through locate()/decode(): a fake DMA instance table maps offsets 1:1 into ram. */
static struct { uint32_t instance[8]; uint8_t ram[32768]; } f;
static h2_command_state s;
static h2_kelvin_clear c;
static int read_word(void *opaque, uint32_t offset, uint32_t *word)
{
    assert(opaque == &f);
    if (offset < 0x13000 || offset >= 0x13020 || (offset & 3)) return 0;
    *word = f.instance[(offset - 0x13000) / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t offset, uint32_t bytes)
{
    assert(opaque == &f);
    if (offset > sizeof f.ram || bytes > sizeof f.ram - offset) return NULL;
    return f.ram + offset;
}
static void set(unsigned unit, unsigned offset, uint32_t value)
{
    unsigned method = 0x1B00 + unit * 64 + offset;
    s.setup[method / 4] = value;
    s.setup_valid[method / 128] |= 1u << ((method / 4) % 32);
}
static void test_mip_chain_layout(void)
{
    memset(&f, 0, sizeof f); memset(&s, 0, sizeof s); memset(&c, 0, sizeof c);
    for (unsigned i = 0; i < sizeof f.ram; ++i) f.ram[i] = (uint8_t)(i ^ (i >> 5));
    f.instance[0] = 0xB002 | (512u << 20); f.instance[1] = 16383;   /* limit: 16 KB window at ram+0 */
    f.instance[2] = f.instance[3] = 3;
    f.instance[4] = 0x2B03D; f.instance[5] = 47;
    f.instance[6] = f.instance[7] = 4099;
    s.dma[1] = 0x13000; s.dma[2] = 0x13010; s.dma_valid = 6;
    c.read_instance = read_word; c.map_physical = map_ram; c.opaque = &f;
    c.physical_bytes = sizeof f.ram;
    /* Swizzled A8R8G8B8 16x8 with 3 levels: level 1 (8x4) follows level 0's 512 bytes;
     * level 2 (4x2) is narrower than 8 texels and is left out of the chain. */
    set(0, 0, 16); set(0, 0xC, 0x40000000);
    set(0, 4, 1 | (0x06 << 8) | (3 << 16) | (4 << 20) | (3 << 24));
    located L;
    assert(locate(&s, &c, 0, 4096, 1, &L));
    assert(L.levels == 2 && L.w == 16 && L.h == 8 && L.src_bytes == 512 + 128);
    assert(L.lvl_src[0] == 0 && L.lvl_src[1] == 512 && L.lvl_dst[1] == 128 && L.texels == 160);
    static uint32_t px[4096];
    decode(&L, px);
    const uint8_t *p0 = L.src + 4 * morton(3, 2, 4, 3);              /* level 0: 16x8 swizzled */
    const uint8_t *p1 = L.src + 512 + 4 * morton(1, 3, 3, 2);        /* level 1: 8x4 swizzled, after level 0 */
    assert(L.src >= f.ram && L.src < f.ram + sizeof f.ram);
    assert(px[2 * 16 + 3] == convert(PF_A8R8G8B8, p0));
    assert(px[128 + 3 * 8 + 1] == convert(PF_A8R8G8B8, p1));
    /* Without mips: level 0 only, as the software path and the tests always saw it. */
    assert(locate(&s, &c, 0, 4096, 0, &L) && L.levels == 1 && L.src_bytes == 512 && L.texels == 128);
    /* The texel cap covers the whole chain. */
    assert(!locate(&s, &c, 0, 150, 1, &L) && locate(&s, &c, 0, 160, 1, &L));
    /* DXT1 32x8, 4 levels: 8x2 blocks, then 4x1 (16x4), then 2x1 (8x2); 4x1 texels stops it. */
    set(0, 4, 1 | (0x0C << 8) | (4 << 16) | (5 << 20) | (3 << 24));
    assert(locate(&s, &c, 0, 4096, 1, &L) && L.levels == 3);
    assert(L.src_bytes == 128 + 32 + 16 && L.lvl_src[1] == 128 && L.lvl_src[2] == 160);
    assert(L.native && L.out_bytes == 128 + 32 + 16 && L.texels == 44 && L.blocks_x == 8 && L.blocks_y == 2);   /* GPU path: native blocks */
    assert(locate(&s, &c, 0, 4096, 0, &L) && !L.native && L.levels == 1 && L.texels == 256);                    /* software path: texels */
    /* Cube map: A8R8G8B8 8x8, 2 levels, format bit 2. Each face holds the whole chain
     * (256 + 64 bytes) padded to 128 -> 384; six faces; the GPU path decodes level 0 of
     * each face into GXM swizzled (Morton) order. */
    set(0, 4, 1 | 4 | (0x06 << 8) | (2 << 16) | (3 << 20) | (3 << 24));
    assert(locate(&s, &c, 0, 4096, 1, &L) && L.cube && L.levels == 1 && L.w == 8 && L.h == 8);
    assert(L.face_stride == 384 && L.src_bytes == 6 * 384 && L.texels == 6 * 64 && L.face_texels == 64);
    decode(&L, px);
    for (unsigned f = 0; f < 6; ++f) {
        const uint8_t *face = L.src + f * 384;
        /* GXM face order interleaves y into the even bits: texel (x,y) lands at morton(y,x). */
        assert(px[f * 64 + morton(2, 5, 3, 3)] == convert(PF_A8R8G8B8, face + 4 * morton(5, 2, 3, 3)));
        assert(px[f * 64 + morton(7, 1, 3, 3)] == convert(PF_A8R8G8B8, face + 4 * morton(1, 7, 3, 3)));
    }
    /* Not square: unsupported. The software path ignores the cube bit (face 0, linear rows). */
    set(0, 4, 1 | 4 | (0x06 << 8) | (2 << 16) | (3 << 20) | (2 << 24));
    assert(!locate(&s, &c, 0, 4096, 1, &L) && locate(&s, &c, 0, 4096, 0, &L) && !L.cube && L.texels == 32);
    /* DXT1 cube 16x16 with 3 levels: 128 + 32 + 8 = 168 -> 256 per face. */
    set(0, 4, 1 | 4 | (0x0C << 8) | (3 << 16) | (4 << 20) | (4 << 24));
    assert(locate(&s, &c, 0, 4096, 1, &L) && L.cube && L.face_stride == 256 && L.src_bytes == 1536);
    assert(L.native && L.face_texels == 32 && L.out_bytes == 6 * 128 && L.texels == 6 * 32);   /* native faces: 16 blocks each, no 2048 padding below 32x32 */
    /* DXT blocks are row-major: in a 16x8 DXT1 image (4x2 blocks) the block at (3,1) is the
     * eighth (index 7), not Morton index 11; every texel of it decodes from that block. */
    set(0, 4, 1 | (0x0C << 8) | (1 << 16) | (4 << 20) | (3 << 24));
    assert(locate(&s, &c, 0, 4096, 0, &L) && !L.cube && !L.native && L.levels == 1 && L.blocks_x == 4 && L.blocks_y == 2);
    decode(&L, px);
    { uint32_t tile[16]; menu_dxt1_block(L.src + 7 * 8, tile, 4);
      for (unsigned ty = 0; ty < 4; ++ty) for (unsigned tx = 0; tx < 4; ++tx) assert(px[(4 + ty) * 16 + 12 + tx] == tile[ty * 4 + tx]);
      menu_dxt1_block(L.src + 1 * 8, tile, 4);
      for (unsigned ty = 0; ty < 4; ++ty) for (unsigned tx = 0; tx < 4; ++tx) assert(px[ty * 16 + 4 + tx] == tile[ty * 4 + tx]); }
    /* Native DXT for the GPU: 16x8 DXT1 (4x2 blocks) keeps its blocks, reordered into GXM's
     * swizzled block order (row in the even Morton bits, column in the odd bits, the wider
     * dimension's extra bit on top): block (2,1) -> index 5, block (1,0) -> 2, (3,1) -> 7. */
    set(0, 4, 1 | (0x0C << 8) | (1 << 16) | (4 << 20) | (3 << 24));
    assert(locate(&s, &c, 0, 4096, 1, &L) && L.native && L.out_bytes == 64 && L.texels == 16 && L.levels == 1);
    decode(&L, px);
    assert(!memcmp((uint8_t *)px + 5 * 8, L.src + (1 * 4 + 2) * 8, 8));
    assert(!memcmp((uint8_t *)px + 2 * 8, L.src + (0 * 4 + 1) * 8, 8));
    assert(!memcmp((uint8_t *)px + 7 * 8, L.src + (1 * 4 + 3) * 8, 8));
    /* Red/blue exchange keeps every texel's colours (swapped) for both DXT1 modes and DXT5. */
    for (unsigned t = 0; t < 3; ++t) {
        uint8_t blk[16], orig[16]; uint32_t a[16], b[16];
        for (unsigned i = 0; i < 16; ++i) blk[i] = (uint8_t)(0x3D * (i + 1) + 7 * t);
        if (t == 0) { blk[0] = 0x1F; blk[1] = 0x00; blk[2] = 0x00; blk[3] = 0xF8; }   /* c0 = pure blue < c1 = pure red: 3-colour, reverses on swap */
        if (t == 1) { blk[0] = 0x00; blk[1] = 0xF8; blk[2] = 0x1F; blk[3] = 0x00; }   /* c0 = red > c1 = blue: 4-colour, reverses on swap */
        if (t == 2) { blk[8] = 0xE0; blk[9] = 0xFF; blk[10] = 0x00; blk[11] = 0x00; } /* DXT5 colour: c0 = yellow > c1 = black stays ordered */
        memcpy(orig, blk, 16);
        if (t < 2) { menu_dxt1_block(orig, a, 4); menu_dxt_swap_rb(blk, 1); menu_dxt1_block(blk, b, 4); }
        else { menu_dxt5_block(orig, a, 4); menu_dxt_swap_rb(blk, 5); menu_dxt5_block(blk, b, 4); }
        for (unsigned i = 0; i < 16; ++i)
            assert(A(b[i]) == A(a[i]) && R(b[i]) == B(a[i]) && G(b[i]) == G(a[i]) && B(b[i]) == R(a[i]));
    }
    /* DXT5 three-colour-mode blocks: 8x8 DXT5 (4 blocks). Blocks using only indices 0/1 are
     * rewritten exactly for four-colour decoders; a block using index 3 forces CPU decoding. */
    {
        set(0, 4, 1 | (0x0F << 8) | (1 << 16) | (3 << 20) | (3 << 24));
        assert(locate(&s, &c, 0, 4096, 1, &L) && L.native && L.src_bytes == 64);
        uint8_t *src = (uint8_t *)L.src;
        for (unsigned b = 0; b < 4; ++b) {                       /* alpha a0=255 index 0; colour c0=0x001F < c1=0xF800 */
            uint8_t *blk = src + b * 16; memset(blk, 0, 16); blk[0] = 0xFF;
            blk[8] = 0x1F; blk[9] = 0x00; blk[10] = 0x00; blk[11] = 0xF8;
            blk[12] = 0x44; blk[13] = 0x11; blk[14] = 0x00; blk[15] = 0x55;   /* indices 0 and 1 only */
        }
        assert(!image_needs_decode(&L));
        uint32_t want[16], got[16]; menu_dxt5_block(src, want, 4);
        uint8_t copy[16]; memcpy(copy, src, 16); rewrite_mode3_block(copy, 5); menu_dxt5_block(copy, got, 4);
        assert(!memcmp(want, got, sizeof want));
        assert((copy[8] | copy[9] << 8) == 0xF800 && (copy[10] | copy[11] << 8) == 0x001F);   /* endpoints swapped: four-colour order */
        src[3 * 16 + 12] = 0xC0;                                  /* block 3 texel 3 uses index 3 (black on the NV2A) */
        assert(image_needs_decode(&L));
        uint32_t tw2, th2; int lin2; uint64_t h2v; uint32_t lv2, tx2; int cu2, na2;
        const uint32_t *px2 = menu_texture_acquire(&s, &c, 0, 77, 4096, 1, &tw2, &th2, &lin2, &h2v, &lv2, &tx2, &cu2, &na2);
        assert(px2 && !na2 && tw2 == 8 && th2 == 8 && tx2 == 64 && menu_texture_mode3_decoded() == 1);
        assert(px2[4 * 8 + 7] == 0xFF000000u);                    /* block (1,1) texel (3,0) = image (7,4): index 3 -> opaque black, as the NV2A */
    }
    /* With a chain: 32x8 DXT1, 3 levels -> 128 + 32 + 16 native bytes, levels back to back. */
    set(0, 4, 1 | (0x0C << 8) | (4 << 16) | (5 << 20) | (3 << 24));
    assert(locate(&s, &c, 0, 4096, 1, &L) && L.native && L.levels == 3 && L.out_bytes == 176 && L.lvl_out[1] == 128 && L.lvl_out[2] == 160);
    decode(&L, px);
    assert(!memcmp((uint8_t *)px + 128, L.src + 128, 8));            /* level 1 block (0,0) */
    /* The software path still decodes DXT to texels. */
    assert(locate(&s, &c, 0, 4096, 0, &L) && !L.native && L.texels == 256);
    /* Linear (pitch) images never carry a chain. */
    set(0, 4, 1 | (0x12 << 8) | (3 << 16)); set(0, 0x10, 64 << 16); set(0, 0x1C, (16 << 16) | 4);
    assert(locate(&s, &c, 0, 4096, 1, &L) && L.levels == 1 && L.texels == 64 && L.src_bytes == 3 * 64 + 64);
}
int main(void)
{
    test_dxt1_solid();
    test_dxt1_indices();
    test_dxt3_alpha();
    test_dxt5_alpha();
    test_a8_and_a4r4g4b4_layouts();
    test_mip_chain_layout();
    printf("menu_texture_test: all assertions passed\n");
    return 0;
}
