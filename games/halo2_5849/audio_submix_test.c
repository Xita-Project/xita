/* Inactive MIXIN storage/ABI and deferred state. No owned game bytes and no
 * synthetic feed is presented as supported DSP/HRTF playback. */
#define main original_pcm_test_main
#include "audio_buffer_test.c"
#undef main

static xctx submix_description(uint32_t dev)
{
    uint32_t fields[6] = {24, 0x2010, 0, 0, 0, 0};
    x_guest_write(0x3FFD, fields, sizeof fields);
    xctx c = context(dev, 0x3FFD, 0x6FFE, 0); X_M32(c.r[4]) = 0x220AC8; return c;
}
int main(void)
{
    g_xram = malloc(0x200000); g_img_base = g_xram; g_xpt = malloc((1u << 20) * 4);
    assert(g_xram && g_xpt); memset(g_xram, 0xcc, 0x200000);
    for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x1ff000;
    for (unsigned i = 1; i < 16; ++i) g_xpt[i] = (i - 1) * 4096;
    g_xpt[3] = 0x8000; g_xpt[7] = 0x9000; g_xpt[0x386] = 0xf000; X_M32(0x386B0C) = 0;
    xctx c = context(0, 0x6200, 0, 0); call(&c, 0x37D797, 0, 3); uint32_t dev = read32(0x6200);
    c = submix_description(dev); X_M32(c.r[4])++; reject(&c, 0x37D4BE);
    for (unsigned i = 0; i < 6; ++i) {
        c = submix_description(dev); uint32_t v = read32(0x3FFD + i * 4) ^ 1;
        x_guest_write(0x3FFD + i * 4, &v, 4); reject(&c, 0x37D4BE);
    }
    const uint32_t bad_out[] = {0, dev, 0x1ff8, 0x4000, 0xfffffffe};
    for (unsigned i = 0; i < sizeof bad_out / sizeof *bad_out; ++i) {
        c = submix_description(dev); X_M32(c.r[4] + 12) = bad_out[i]; reject(&c, 0x37D4BE);
    }
    c = submix_description(dev); healthy = 0; reject(&c, 0x37D4BE); healthy = 1;
    c = submix_description(dev); X_M32(0x386B0C) = 1; reject(&c, 0x37D4BE); X_M32(0x386B0C) = 0;
    c = submix_description(dev); uint32_t untouched = read32(0x6FFE); allocation_failure = 1;
    call(&c, 0x37D4BE, 0x8007000e, 4); allocation_failure = 0;
    assert(read32(0x6FFE) == untouched && !device.children && xk_audio_free_voices() == XA_MAX_VOICES);
    c = submix_description(dev); call(&c, 0x37D4BE, 0, 4);
    uint32_t handle = read32(0x6FFE); h2_audio_buffer *b = find_buffer(handle - 0x1C);
    assert(b && b->submix && b->voice == -1 && b->frequency == 48000 && !b->headroom);
    assert(b->mirror == b->base + 4096 && b->bytes == 128 && xk_mem_size(b->base) == 8192);
    uint32_t bus[32]; x_guest_read(bus, b->mirror, sizeof bus);
    for (unsigned i = 0; i < 32; ++i) assert(!bus[i]);
    assert(xk_audio_free_voices() == XA_MAX_VOICES && device.children == 1 && device.references == 2);
    c = context(dev, 0x3f800000, 0, 0); reject(&c, 0x37D506);
    assert(b->spatial[0] == 0x07ff0000 && b->spatial[0x38 / 4] == 0x3f800000 && b->spatial[0x3c / 4] == 0x4e6e6b28);
    /* Typed bus samples have full 24-bit precision in their 32-bit storage;
     * no PCM decoder truncates the resource or obtains a reference to it. */
    int32_t samples[32]; for (unsigned i = 0; i < 32; ++i) samples[i] = i & 1 ? -8388608 : 8388607;
    x_guest_write(b->mirror, samples, sizeof samples); x_guest_read(bus, b->mirror, sizeof bus);
    assert(!memcmp(samples, bus, sizeof bus));
    c = context(dev, b->mirror, 0, 0); reject(&c, 0x37B5AE);
    g_xpt[0x300] = g_xpt[b->mirror >> 12]; c = context(dev, 0x300000, 0, 0); reject(&c, 0x37B5AE);
    const uint32_t setters[][5] = {
        {0x37C620,0x220B3E,0x3C,0x7f7fffff,0x00200000},
        {0x37C644,0x220B4D,0x38,0x7f7fffff,0x00200000},
        {0x37C6C1,0x220B58,0x48,0,0x01000000},
        {0x37C69D,0x220B63,0x4C,0,0x02000000},
        {0x37C600,0x220B6E,0x34,0,0x00100000}
    };
    for (unsigned i = 0; i < 5; ++i) {
        const uint32_t *s = setters[i];
        c = context(handle, s[3], 1, 0); X_M32(c.r[4]) = s[1] + 1; reject(&c, s[0]);
        c = context(handle, s[3] ^ 1, 1, 0); X_M32(c.r[4]) = s[1]; reject(&c, s[0]);
        c = context(handle, s[3], 0, 0); X_M32(c.r[4]) = s[1]; reject(&c, s[0]);
        uint32_t before[41]; memcpy(before, b->spatial, sizeof before); b->spatial[0] = 8;
        c = context(handle, s[3], 1, 0); X_M32(c.r[4]) = s[1]; call(&c, s[0], 0, 3);
        before[0] = 8 | s[4]; before[s[2] / 4] = s[3]; assert(!memcmp(before, b->spatial, sizeof before));
        x_guest_read(bus, b->mirror, sizeof bus); assert(!memcmp(samples, bus, sizeof bus));
    }
    uint32_t list[2] = {5, 0x5FFB}, pairs[10] = {6,0,8,0,7,0,9,0,10,0};
    x_guest_write(0x4FFC, list, 8); x_guest_write(list[1], pairs, sizeof pairs);
    c = context(handle, 0x4FFC, 0, 0); X_M32(c.r[4]) = 0x220B29; call(&c, 0x37C5E4, 0, 2);
    for (unsigned i = 0; i < 10; ++i) {
        pairs[i] ^= 1; x_guest_write(list[1], pairs, sizeof pairs);
        c = context(handle, 0x4FFC, 0, 0); X_M32(c.r[4]) = 0x220B29; reject(&c, 0x37C5E4); pairs[i] ^= 1;
    }
    /* Level start on an inactive bus: SetI3DL2Source(deferred) stores the nine
     * DSI3DL2BUFFER words (each refused just outside its documented range) and
     * SetFilter stores the fixed low-pass descriptor; nothing else changes. */
    {
        const uint32_t source[9] = {0,0,(uint32_t)-6400,(uint32_t)-6400,0,0,0,0,0x3e800000};
        const uint32_t below[9] = {(uint32_t)-10001,(uint32_t)-10001,(uint32_t)-10001,(uint32_t)-10001,0xbf800000u,
                                   (uint32_t)-10001,0xbf800000u,(uint32_t)-10001,0xbf800000u};
        const uint32_t above[9] = {1,1,1,1,0x41200001u,1,0x3f800001u,1,0x3f800001u};
        uint32_t before[41]; memcpy(before, b->spatial, sizeof before);
        x_guest_write(0x5FFB, source, sizeof source);
        c = context(handle, 0x5FFB, 1, 0); X_M32(c.r[4]) = 0x221428; reject(&c, 0x37C6E5);
        c = context(handle, 0x5FFB, 0, 0); X_M32(c.r[4]) = 0x221427; reject(&c, 0x37C6E5);
        c = context(handle, 0, 1, 0); X_M32(c.r[4]) = 0x221427; reject(&c, 0x37C6E5);
        b->started = 1; c = context(handle, 0x5FFB, 1, 0); X_M32(c.r[4]) = 0x221427; reject(&c, 0x37C6E5); b->started = 0;
        for (unsigned field = 0; field < 9; ++field) for (unsigned edge = 0; edge < 2; ++edge) {
            uint32_t words[9]; memcpy(words, source, sizeof words); words[field] = edge ? above[field] : below[field];
            x_guest_write(0x5FFB, words, sizeof words);
            c = context(handle, 0x5FFB, 1, 0); X_M32(c.r[4]) = 0x221427; reject(&c, 0x37C6E5);
        }
        assert(!memcmp(before, b->spatial, sizeof before));
        x_guest_write(0x5FFB, source, sizeof source);
        c = context(handle, 0x5FFB, 1, 0); X_M32(c.r[4]) = 0x221427; call(&c, 0x37C6E5, 0, 3);
        memcpy(before + 0x80 / 4, source, sizeof source); before[0x7C / 4] |= 0x007F0000;
        assert(!memcmp(before, b->spatial, sizeof before) && !b->started && !b->stopped && b->voice == -1);
        uint32_t desc[6] = {1,0,0,0x8000,0,0};
        for (unsigned field = 0; field < 6; ++field) {
            desc[field] ^= 1; x_guest_write(0x5FFB, desc, sizeof desc);
            c = context(handle, 0x5FFB, 0, 0); X_M32(c.r[4]) = 0x22147C; reject(&c, 0x37B68B); desc[field] ^= 1;
        }
        x_guest_write(0x5FFB, desc, sizeof desc);
        c = context(handle, 0x5FFB, 0, 0); X_M32(c.r[4]) = 0x22147D; reject(&c, 0x37B68B);
        c = context(handle, 0, 0, 0); X_M32(c.r[4]) = 0x22147C; reject(&c, 0x37B68B);
        b->started = 1; c = context(handle, 0x5FFB, 0, 0); X_M32(c.r[4]) = 0x22147C; reject(&c, 0x37B68B); b->started = 0;
        c = context(handle, 0x5FFB, 0, 0); X_M32(c.r[4]) = 0x22147C; call(&c, 0x37B68B, 0, 2);
        assert(!memcmp(b->filter, desc, sizeof desc) && !memcmp(before, b->spatial, sizeof before) && b->voice == -1);
        x_guest_read(bus, b->mirror, sizeof bus); assert(!memcmp(samples, bus, sizeof bus));
    }
    /* SetPosition (0x37C668, DS3D_DEFERRED) from the level's 3D update stores
     * the position words and the dirty bit on the inactive bus. */
    {
        uint32_t before[41]; memcpy(before, b->spatial, sizeof before);
        c = context(handle, 0x3f800000, 0x40000000, 0x40400000); X_M32(c.r[4] + 20) = 1; X_M32(c.r[4]) = 0x220EA7; reject(&c, 0x37C668);
        c = context(handle, 0x3f800000, 0x40000000, 0x40400000); X_M32(c.r[4] + 20) = 0; X_M32(c.r[4]) = 0x220EA6; reject(&c, 0x37C668);
        c = context(handle, 0x7fc00000, 0x40000000, 0x40400000); X_M32(c.r[4] + 20) = 1; X_M32(c.r[4]) = 0x220EA6; reject(&c, 0x37C668);
        c = context(handle, 0x3f800000, 0x7f800000, 0x40400000); X_M32(c.r[4] + 20) = 1; X_M32(c.r[4]) = 0x220EA6; reject(&c, 0x37C668);
        b->started = 1; c = context(handle, 0x3f800000, 0x40000000, 0x40400000); X_M32(c.r[4] + 20) = 1; X_M32(c.r[4]) = 0x220EA6; reject(&c, 0x37C668); b->started = 0;
        assert(!memcmp(before, b->spatial, sizeof before));
        c = context(handle, 0x3f800000, 0x40000000, 0x40400000); X_M32(c.r[4] + 20) = 1; X_M32(c.r[4]) = 0x220EA6; call(&c, 0x37C668, 0, 5);
        before[2] = 0x3f800000; before[3] = 0x40000000; before[4] = 0x40400000; before[0] |= 0x00010000;
        assert(!memcmp(before, b->spatial, sizeof before));
        x_guest_read(bus, b->mirror, sizeof bus); assert(!memcmp(samples, bus, sizeof bus));
    }
    const uint32_t unsupported[] = {0x37CC4A,0x37B7B3,0x379F40,0x37C5C8,0x37B66F,0x37B6A7,
        0x37B6C3,0x37B6DF,0x37B703,0x37B75B,0x37B797,0x37B777};
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; ++i) {
        c = context(handle, 0, 0, 0); reject(&c, unsupported[i]);
    }
    c = context(b->base, 0, 0, 0); call(&c, 0x37A14F, 2, 1); assert(device.references == 2);
    c = context(handle, 0, 0, 0); call(&c, 0x379F45, 1, 1);
    uint32_t base = b->base; c = context(handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
    assert(!find_buffer(base) && !device.children && device.references == 1);
    c = context(handle, 0, 0, 0); reject(&c, 0x379F45);
    c = context(dev - 8, 0, 0, 0); call(&c, 0x37C70F, 0, 1); assert(!healthy);
    for (unsigned i = 0; i < 256; ++i) assert(!pool[i]);
    free(g_xram); free(g_xpt);
    puts("Halo 2 inactive submix: typed owned bus, exact deferred state/routing, strict activation, ABI and lifetime pass");
    return 0;
}
