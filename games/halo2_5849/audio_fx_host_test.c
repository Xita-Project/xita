/* Guest ABI and resource lifetime; provider/sink are scripted here. Actual
 * DSP output and concurrent ownership are covered by the companion tests. */
#define H2_AUDIO_SPATIAL_MODEL 1
#define H2_AUDIO_DSP_TEST_MAIN dsp_adapter_tests
#include "audio_dsp_test.c"
static xctx fx_description(uint32_t dev)
{
    uint32_t fields[6] = {24, 0x100000, 0, 0, 0, 13}; x_guest_write(0x3ffd, fields, sizeof fields);
    xctx c = context(dev, 0x3ffd, 0x6ffe, 0); X_M32(c.r[4]) = 0x220C26; return c;
}
static xctx fx_context(uint32_t handle, uint32_t arg, uint32_t caller)
{ xctx c = context(handle, arg, 0, 0); X_M32(c.r[4]) = caller; return c; }
int main(int argc, char **argv)
{
    g_xram = malloc(0x200000); g_img_base = g_xram; g_xpt = malloc((1u << 20) * 4); assert(g_xram && g_xpt);
    memset(g_xram, 0xcc, 0x200000); for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x1ff000;
    for (unsigned i = 1; i < 16; ++i) g_xpt[i] = (i - 1) * 4096;
    g_xpt[3] = 0x8000; g_xpt[7] = 0x9000; g_xpt[0x386] = 0xf000; g_xpt[0x387] = 0x1e0000; X_M32(0x386B0C) = 0;
    xctx c = context(0, 0x6200, 0, 0); call(&c, 0x37D797, 0, 3); uint32_t dev = read32(0x6200);
    c = fx_description(dev); reject(&c, 0x37D4BE);
    c = download_context(); call(&c, 0x37B86D, 0, 4);
    for (unsigned i = 0; i < 6; ++i) {
        c = fx_description(dev); uint32_t v = read32(0x3ffd + i * 4) ^ 1;
        x_guest_write(0x3ffd + i * 4, &v, 4); reject(&c, 0x37D4BE); assert(!test_fx_bound);
    }
    c = fx_description(dev); X_M32(c.r[4])++; reject(&c, 0x37D4BE);
    c = fx_description(dev); X_M32(c.r[4] + 12) = 0x4000; reject(&c, 0x37D4BE);
    c = fx_description(dev); uint32_t before = read32(0x6ffe); allocation_failure = 1;
    call(&c, 0x37D4BE, 0x8007000e, 4); allocation_failure = 0;
    assert(!test_fx_bound && !device.children && read32(0x6ffe) == before);
    for (unsigned active_case = 0; active_case < 2; ++active_case) {
        c = fx_description(dev); call(&c, 0x37D4BE, 0, 4);
        uint32_t handle = read32(0x6ffe); h2_audio_buffer *b = find_buffer(handle - 0x1c);
        assert(b && b->submix == 2 && b->voice == -1 && !b->mirror && !b->headroom && !b->volume && b->route_count == 2);
        assert(device.children == 1 && device.references == 2 && xk_audio_free_voices() == XA_MAX_VOICES);
        c = fx_description(dev); reject(&c, 0x37D4BE);
        c = fx_context(handle, 1, 0x220C37); reject(&c, 0x37B66F);
        c = fx_context(handle, 0, 0x220C38); reject(&c, 0x37B66F);
        c = fx_context(handle, 0, 0x220C37); call(&c, 0x37B66F, 0, 2);
        uint32_t list[2] = {6, 0x5ffb}, pairs[12] = {0,0,1,0,2,0,3,0,4,0,5,0};
        x_guest_write(0x4ffc, list, 8);
        for (unsigned i = 0; i < 12; ++i) {
            pairs[i] ^= 1; x_guest_write(list[1], pairs, sizeof pairs);
            c = fx_context(handle, 0x4ffc, 0x220CAA); reject(&c, 0x37C5E4); pairs[i] ^= 1;
        }
        x_guest_write(list[1], pairs, sizeof pairs);
        c = fx_context(handle, 0x4ffc, 0x220CAA); call(&c, 0x37C5E4, 0, 2); assert(b->route_count == 6 && test_fx_routes == 6);
        const uint32_t unsupported[] = {0x37CC4A,0x37B7B3,0x379F40,0x37C5C8,0x37B6A7,0x37B6C3,0x37B703,0x37B75B,0x37B797,0x37B777,0x37C620};
        for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; ++i) { c = fx_context(handle, 0, 0x220CB5); reject(&c, unsupported[i]); }
        c = fx_context(handle, 0, 0x220CB5); X_M32(c.r[4] + 16) = 1; reject(&c, 0x37B6DF);
        if (active_case) {
            c = fx_context(handle, 0, 0x220CB5); call(&c, 0x37B6DF, 0, 4); assert(b->started && test_fx_playing);
            /* Exact next original constructor: independent default FL/FR
             * source, with first source ownership retained throughout. */
            c = fx_description(dev); uint32_t second_bin = 23;
            x_guest_write(0x3ffd + 20, &second_bin, 4); X_M32(c.r[4]) = 0x21E830;
            call(&c, 0x37D4BE, 0, 4); uint32_t second_handle = read32(0x6ffe);
            h2_audio_buffer *second = find_buffer(second_handle - 0x1c);
            assert(second && second != b && second->fx_bin == 23 && second->route_count == 2);
            assert(device.references == 3 && device.children == 2 && test_fx_bound == 3 && test_fx_playing == 1);
            c = fx_context(second_handle, 0, 0x220CB5); reject(&c, 0x37B6DF);
            c = fx_context(second_handle, 0, 0x220C37); reject(&c, 0x37B66F);
            c = fx_context(second_handle, 0x4ffc, 0x220CAA); reject(&c, 0x37C5E4);
            c = fx_context(second_handle, 0, 0x21E842); call(&c, 0x37B6DF, 0, 4);
            assert(second->started && test_fx_playing == 3 && b->started);
            /* Invalid/unmapped owned-filter contracts fail before allocating.
             * Optional privately supplied 32-byte owned filter enables the
             * exact valid creation ABI path without tracking owned bytes. */
            c = fx_description(dev); uint32_t spatial_desc[6] = {24,0x100010,0,0,0,23};
            x_guest_write(0x3ffd, spatial_desc, sizeof spatial_desc); X_M32(c.r[4]) = 0x21E88A;
            X_M32(0x3871F0) = 2; memset(X_G(0x386958), 0, 64); reject(&c, 0x37D4BE);
            if (argc == 2) {
                FILE *filter = fopen(argv[1], "rb"); assert(filter); int8_t taps[32];
                assert(fread(taps, 1, sizeof taps, filter) == sizeof taps && fgetc(filter) == EOF); fclose(filter);
                x_guest_write(0x386958, taps, 32); x_guest_write(0x386978, taps, 32);
                uint32_t extra_handles[5]; unsigned extras = 0;
                for (unsigned bin = 23; bin <= 25; ++bin) {
                    if (bin != 23) {
                        c = fx_description(dev); x_guest_write(0x3ffd + 20, &bin, 4); X_M32(c.r[4]) = 0x21E830;
                        call(&c, 0x37D4BE, 0, 4); uint32_t handle = read32(0x6ffe); extra_handles[extras++] = handle;
                        c = fx_context(handle, 0, 0x21E842); call(&c, 0x37B6DF, 0, 4);
                    }
                    c = fx_description(dev); spatial_desc[5] = bin; x_guest_write(0x3ffd, spatial_desc, sizeof spatial_desc);
                    X_M32(c.r[4]) = 0x21E88A;
                    unsigned before_mask = (1u << (2 + (bin - 23) * 2)) - 1, after_mask = before_mask * 2 + 1;
                xctx invalid = c; X_M32(c.r[4] + 12) = 0x386958; reject(&invalid, 0x37D4BE);
                X_M32(c.r[4] + 12) = 0x3871F0; invalid = c; reject(&invalid, 0x37D4BE);
                g_xpt[0x388] = g_xpt[0x386]; X_M32(c.r[4] + 12) = 0x388958; invalid = c; reject(&invalid, 0x37D4BE);
                X_M32(c.r[4] + 12) = 0x6ffe;
                call(&c, 0x37D4BE, 0, 4); uint32_t spatial_handle = read32(0x6ffe);
                h2_audio_buffer *spatial = find_buffer(spatial_handle - 0x1c);
                extra_handles[extras++] = spatial_handle;
                assert(spatial && spatial->fx_bin == (0x10000u | bin) && spatial->route_count == 5);
                assert(test_fx_bound == after_mask && test_fx_playing == before_mask && device.references == 4 + (bin - 23) * 2);
                for (unsigned setter = 0; setter < 2; ++setter) {
                    uint32_t ip = setter ? 0x37C644 : 0x37C620, caller = setter ? 0x21E8AC : 0x21E89D;
                    c = context(spatial_handle, 0x7f7fffff, 0, 0); X_M32(c.r[4]) = caller; reject(&c, ip);
                    c = context(spatial_handle, 0x7f7ffffe, 1, 0); X_M32(c.r[4]) = caller; reject(&c, ip);
                    c = context(spatial_handle, 0x7f7fffff, 1, 0); X_M32(c.r[4]) = caller+1; reject(&c, ip);
                    c = context(spatial_handle, 0x7f7fffff, 1, 0); X_M32(c.r[4]) = caller; call(&c, ip, 0, 3);
                }
                device.pending_position[0] = 1; c = fx_context(spatial_handle, 0, 0x21E8BA); reject(&c, 0x37B6DF);
                device.pending_position[0] = 0; device.dirty = 0x25; device.pending_distance = 0x4043126f;
                device.pending_rolloff = device.pending_doppler = 0;
                device.pending_orientation[0] = device.pending_orientation[4] = 0x3f800000;
                c = fx_context(spatial_handle, 0, 0x21E8BA); call(&c, 0x37B6DF, 0, 4);
                assert(test_fx_playing == after_mask && spatial->started && !spatial->spatial[0]);
                assert(spatial->spatial[1] == 0xf8000000 && spatial->spatial[0x70/4] == 0x43340000);
                c = fx_context(spatial_handle, 0, 0x21E8BA); reject(&c, 0x37B6DF);
                c = context(spatial_handle, 0, 0, 0); reject(&c, 0x379F45);
                }
                assert(extras == 5 && test_fx_playing == 127 && device.references == 8);
                while (extras) {
                    uint32_t handle = extra_handles[--extras]; h2_audio_buffer *retiring = find_buffer(handle - 0x1c);
                    retiring->started = 0; test_fx_playing &= ~test_fx_mask(retiring->fx_bin); /* test teardown only */
                    c = context(handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
                }
                assert(test_fx_bound == 3 && device.references == 3);
            }
            c = context(second_handle, 0, 0, 0); reject(&c, 0x379F45);
            second->started = 0; test_fx_playing &= ~2u; /* test teardown only */
            c = context(second_handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
            assert(device.references == 2 && device.children == 1 && test_fx_bound == 1 && test_fx_playing == 1);
            c = fx_context(handle, 0, 0x220CB5); reject(&c, 0x37B6DF);
            c = context(handle, 0, 0, 0); reject(&c, 0x379F45);
            /* Test teardown only: active Stop remains unsupported in guest. */
            b->started = 0; test_fx_playing = 0;
        }
        c = context(b->base, 0, 0, 0); call(&c, 0x37A14F, 2, 1);
        c = context(handle, 0, 0, 0); call(&c, 0x379F45, 1, 1);
        c = context(handle, 0, 0, 0); call(&c, 0x379F45, 0, 1);
        assert(!test_fx_bound && !device.children && device.references == 1);
    }
    c = context(dev - 8, 0, 0, 0); call(&c, 0x37C70F, 0, 1); assert(!effects && !healthy);
    for (unsigned i = 0; i < 256; ++i) assert(!pool[i]); free(g_xram); free(g_xpt);
    puts("Halo 2 FXIN2 ABI: checked descriptor/gain/routes/Play, full context, aliases, strict methods and device lifetime pass"); return 0;
}
