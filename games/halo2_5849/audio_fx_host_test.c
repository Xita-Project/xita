/* Guest ABI and resource lifetime; provider/sink are scripted here. Actual
 * DSP output and concurrent ownership are covered by the companion tests. */
#define H2_AUDIO_SPATIAL_MODEL 1
#ifndef H2_AUDIO_FILTER_MODEL
#define H2_AUDIO_FILTER_MODEL 1
#endif
#define H2_AUDIO_DSP_TEST_MAIN dsp_adapter_tests
#include "audio_dsp_test.c"
static xctx fx_description(uint32_t dev)
{
    uint32_t fields[6] = {24, 0x100000, 0, 0, 0, 13}; x_guest_write(0x3ffd, fields, sizeof fields);
    xctx c = context(dev, 0x3ffd, 0x6ffe, 0); X_M32(c.r[4]) = 0x220C26; return c;
}
static xctx global_pcm_description(void)
{
    uint32_t fields[6]={24,0,0,0x4ff9,0x5ffc,0},list[2]={1,0x7ff9},pair[2]={14,0};
    uint8_t format[18]={1,0,1,0,0xe8,3,0,0,0xe8,3,0,0,1,0,8,0,0,0};
    x_guest_write(0x3ffd,fields,24);x_guest_write(0x4ff9,format,18);
    x_guest_write(0x5ffc,list,8);x_guest_write(0x7ff9,pair,8);
    xctx c=context(0x3ffd,0x8ffb,0,0);X_M32(c.r[4])=0x22153E;return c;
}
static xctx fx_context(uint32_t handle, uint32_t arg, uint32_t caller)
{ xctx c = context(handle, arg, 0, 0); X_M32(c.r[4]) = caller; return c; }
#if H2_AUDIO_FILTER_MODEL
static void fixed_commit_tests(uint32_t dev)
{
    h2_audio_buffer*spatial[3]={0};
    for(unsigned i=0;i<XA_MAX_VOICES;++i)if(buffers[i].base && (buffers[i].fx_bin&0x10000))
        spatial[(buffers[i].fx_bin&0xffff)-23]=&buffers[i];
    assert(spatial[0]&&spatial[1]&&spatial[2]);
    xctx c=fx_context(dev,0,0x21f201);reject(&c,0x37D141); /* prior synthetic dynamic I3DL2 input */
    uint32_t input[9]={0,0,0,0,0,0,0,0,0x3e800000};x_guest_write(0x5ffb,input,sizeof input);
    c=context(spatial[2]->base+0x1c,0x5ffb,1,0);X_M32(c.r[4])=0x2AEF6A;call(&c,0x37C6E5,0,3);
    h2_audio_device_snapshot owner=device;
    for(unsigned field=0;field<13;++field){
        device=owner;
        if(field==0)device.dirty=1;else if(field==1)device.distance^=1;
        else if(field==2)device.rolloff=1;else if(field==3)device.doppler=1;
        else if(field==4)device.pending_distance^=1;else if(field==5)device.pending_rolloff=1;
        else if(field==6)device.pending_doppler=1;else device.pending_orientation[field-7]^=1;
        c=fx_context(dev,0,0x21f201);reject(&c,0x37D141);
    }
    device=owner;
    for(unsigned field=0;field<3;++field){device.pending_position[field]=1;c=fx_context(dev,0,0x21f201);reject(&c,0x37D141);device=owner;}
    for(unsigned n=0;n<3;++n){
        h2_audio_buffer saved=*spatial[n];
        for(unsigned j=0;j<41;++j){spatial[n]->spatial[j]^=1;c=fx_context(dev,0,0x21f201);reject(&c,0x37D141);*spatial[n]=saved;}
        for(unsigned j=0;j<5;++j){spatial[n]->route_gains[j]^=1;c=fx_context(dev,0,0x21f201);reject(&c,0x37D141);*spatial[n]=saved;}
        spatial[n]->stopped=1;c=fx_context(dev,0,0x21f201);reject(&c,0x37D141);*spatial[n]=saved;
    }
    c=fx_context(dev,0,0x21f202);reject(&c,0x37D141);
    c=fx_context(dev+1,0,0x21f201);reject(&c,0x37D141);
    test_commit_failure=1;c=fx_context(dev,0,0x21f201);reject(&c,0x37D141);test_commit_failure=0;
    h2_audio_buffer *before=malloc(sizeof buffers);assert(before);memcpy(before,buffers,sizeof buffers);
    ((h2_audio_buffer*)before)[spatial[2]-buffers].spatial[0x7c/4]=0;
    owner.doppler=0;owner.dirty=0;
    for(unsigned repeat=0;repeat<2;++repeat){unsigned calls=test_commit_calls;
        c=fx_context(dev,0,0x21f201);call(&c,0x37D141,0,1);
        assert(test_commit_calls==calls+1&&!memcmp(&device,&owner,sizeof owner)&&!memcmp(before,buffers,sizeof buffers));
    }
    free(before);
}
#endif
int main(int argc, char **argv)
{
    g_xram = malloc(0x200000); g_img_base = g_xram; g_xpt = malloc((1u << 20) * 4); assert(g_xram && g_xpt);
    memset(g_xram, 0xcc, 0x200000); for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x1ff000;
    for (unsigned i = 1; i < 16; ++i) g_xpt[i] = (i - 1) * 4096;
    g_xpt[3] = 0x8000; g_xpt[7] = 0x9000; g_xpt[0x386] = 0xf000; g_xpt[0x387] = 0x1e0000; X_M32(0x386B0C) = 0;
    xctx c = context(0, 0x6200, 0, 0); call(&c, 0x37D797, 0, 3); uint32_t dev = read32(0x6200);
    c = context(dev,0x4043126f,0,0);call(&c,0x37D506,0,3);
    c = context(dev,0,0,0);call(&c,0x37D5CD,0,3);
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
                uint32_t active_list[2] = {4,0x5ffb}, active_pairs[8] = {6,0,8,(uint32_t)-6400,7,(uint32_t)-6400,9,(uint32_t)-6400};
                x_guest_write(0x4ffc, active_list, 8);
                for (unsigned field = 0; field < 8; ++field) {
                    active_pairs[field] ^= 1; x_guest_write(active_list[1], active_pairs, sizeof active_pairs);
                    c = fx_context(second_handle, 0x4ffc, 0x2AEC87); reject(&c, 0x37C5E4); active_pairs[field] ^= 1;
                    assert(!test_fx23_output_mask && second->route_count == 2);
                }
                x_guest_write(active_list[1], active_pairs, sizeof active_pairs);
                c = fx_context(second_handle, 0x4ffc, 0x2AEC88); reject(&c, 0x37C5E4);
                c = fx_context(second_handle, 0x4ffc, 0x2AEC87); call(&c, 0x37C5E4, 0, 2);
                assert(test_fx23_output_mask == 64 && second->route_count == 4 && second->route_gains[1] == -6400);
                c = fx_context(second_handle, 0, 0x2AEC94); reject(&c, 0x37B66F);
                c = fx_context(second_handle, 1, 0x2AEC95); reject(&c, 0x37B66F);
                c = fx_context(second_handle, 0, 0x2AEC95); call(&c, 0x37B66F, 0, 2);
                uint32_t muted_handle = extra_handles[0]; h2_audio_buffer *muted = find_buffer(muted_handle - 0x1c);
                uint32_t spatial_before[41]; memcpy(spatial_before, muted->spatial, sizeof spatial_before);
                c = fx_context(muted_handle, (uint32_t)-6399, 0x2AECA6); reject(&c, 0x37B66F);
                c = fx_context(muted_handle, (uint32_t)-6400, 0x2AECA7); reject(&c, 0x37B66F);
                assert(!test_fx23_muted && !muted->volume);
                c = fx_context(muted_handle, (uint32_t)-6400, 0x2AECA6); call(&c, 0x37B66F, 0, 2);
                assert(test_fx23_muted && muted->volume == -6400 && muted->started && test_fx_playing == 127);
                assert(!memcmp(spatial_before, muted->spatial, sizeof spatial_before));
                /* Remaining exact caller sequence: route/zero-volume then
                 * mute the companion, without changing live ownership. */
                for (unsigned stage = 0; stage < 2; ++stage) {
                    uint32_t route_handle = extra_handles[stage ? 4 : 1], mute_handle = extra_handles[stage ? 3 : 2];
                    uint32_t route_caller = stage ? 0x2AEF24 : 0x2AEDD9, zero_caller = stage ? 0x2AEF32 : 0x2AEDE7;
                    uint32_t mute_caller = stage ? 0x2AEF43 : 0x2AEDF8;
                    uint32_t remaining_list[2] = {stage ? 5 : 4,0x5ffb};
                    uint32_t remaining_pairs[10] = {6,(uint32_t)-6400,8,(uint32_t)-6400,7,(uint32_t)-6400,9,(uint32_t)-6400,10,(uint32_t)-6400};
                    remaining_pairs[stage ? 9 : 5] = 0;
                    x_guest_write(0x4ffc,remaining_list,8);
                    for (unsigned field = 0; field < remaining_list[0] * 2; ++field) {
                        remaining_pairs[field] ^= 1; x_guest_write(remaining_list[1],remaining_pairs,remaining_list[0] * 8);
                        c = fx_context(route_handle,0x4ffc,route_caller); reject(&c,0x37C5E4); remaining_pairs[field] ^= 1;
                    }
                    x_guest_write(remaining_list[1],remaining_pairs,remaining_list[0] * 8);
                    c = fx_context(route_handle,0x4ffc,route_caller+1); reject(&c,0x37C5E4);
                    c = fx_context(route_handle,0x4ffc,route_caller); call(&c,0x37C5E4,0,2);
                    c = fx_context(route_handle,0,zero_caller); call(&c,0x37B66F,0,2);
                    c = fx_context(mute_handle,(uint32_t)-6399,mute_caller); reject(&c,0x37B66F);
                    c = fx_context(mute_handle,(uint32_t)-6400,mute_caller); call(&c,0x37B66F,0,2);
                }
                assert(test_fx24_output_mask == 128 && test_fx25_routed && test_fx24_muted && test_fx25_muted);
                assert(test_fx_playing == 127 && device.references == 8);
                h2_audio_buffer *deferred = find_buffer(extra_handles[4] - 0x1c);
                uint32_t raw[9] = {0x7fc12345,0x80000000,1,0xffffffff,0x3f800000,0x7f800000,7,0xff800000,0x3e800000};
                uint32_t expected[41]; memcpy(expected,deferred->spatial,sizeof expected);
                x_guest_write(0x5ffb,raw,sizeof raw);
                for (unsigned mode = 0; mode < 3; ++mode) {
                    c = context(extra_handles[4], mode == 2 ? 0 : 0x5ffb, mode == 0 ? 0 : 1, 0);
                    X_M32(c.r[4]) = mode == 1 ? 0x2AEF6B : 0x2AEF6A; reject(&c,0x37C6E5);
                    assert(!memcmp(expected,deferred->spatial,sizeof expected));
                }
                memcpy(expected + 0x80 / 4,raw,sizeof raw); expected[0x7C / 4] |= 0x007F0000;
                c = context(extra_handles[4],0x5ffb,1,0); X_M32(c.r[4]) = 0x2AEF6A; call(&c,0x37C6E5,0,3);
                assert(!memcmp(expected,deferred->spatial,sizeof expected) && test_fx_playing == 127);
                uint32_t readback[9]; x_guest_read(readback,0x5ffb,sizeof readback); assert(!memcmp(raw,readback,sizeof raw));




                for (unsigned filter_index = 0; filter_index < 2; ++filter_index) {
                    uint32_t handle = filter_index ? extra_handles[1] : second_handle;
                    uint32_t caller = filter_index ? 0x2AEFCD : 0x2AEFBC;
                    h2_audio_buffer *target = find_buffer(handle - 0x1c), before = *target;
                    uint32_t desc[6] = {1,0,0,0x8000,0,0};
                    for (unsigned field = 0; field < 6; ++field) {
                        desc[field] ^= 1; x_guest_write(0x5ffb,desc,sizeof desc);
                        c = fx_context(handle,0x5ffb,caller); reject(&c,0x37B68B); desc[field] ^= 1;
                        assert(!memcmp(target,&before,sizeof before));
                    }
                    x_guest_write(0x5ffb,desc,sizeof desc);
                    c = fx_context(handle,0x5ffb,caller+1); reject(&c,0x37B68B);
                    c = fx_context(handle,0,caller); reject(&c,0x37B68B);
#if H2_AUDIO_FILTER_MODEL
                    c = fx_context(handle,0x5ffb,caller); test_fx_filter_failure = 1; reject(&c,0x37B68B);
                    test_fx_filter_failure = 0; assert(!memcmp(target,&before,sizeof before));
                    c = fx_context(handle,0x5ffb,caller); call(&c,0x37B68B,0,2);
                    memcpy(before.filter,desc,sizeof desc); assert(!memcmp(target,&before,sizeof before));
                    uint32_t readback[6]; x_guest_read(readback,0x5ffb,sizeof readback); assert(!memcmp(readback,desc,sizeof desc));
                    assert(test_fx_filtered == (1u << (filter_index + 1)) - 1);
                    c = fx_context(handle,0x5ffb,caller); call(&c,0x37B68B,0,2);
#else
                    c = fx_context(handle,0x5ffb,caller); reject(&c,0x37B68B);
                    assert(!test_fx_filtered && !memcmp(target,&before,sizeof before));
#endif
                }
#if H2_AUDIO_FILTER_MODEL
                uint32_t extra_sources[8];
                for (unsigned bin = 15; bin <= 22; ++bin) {
                    c = fx_description(dev); x_guest_write(0x3ffd + 20,&bin,4); X_M32(c.r[4]) = 0x21E9E1; reject(&c,0x37D4BE);
                    X_M32(c.r[4]) = 0x21E9E0; call(&c,0x37D4BE,0,4);
                    uint32_t handle = read32(0x6ffe); extra_sources[bin - 15] = handle;
                    h2_audio_buffer *target = find_buffer(handle - 0x1c);
                    assert(target->route_count == 2 && !target->started);
                    c = fx_context(handle,1,0x21E9EE); reject(&c,0x37B66F);
                    c = fx_context(handle,0,0x21E9EF); reject(&c,0x37B66F);
                    c = fx_context(handle,0,0x21E9EE); call(&c,0x37B66F,0,2);
                    uint32_t list[2] = {1,0x5ffb}, pair[2] = {6 + (bin - 15) % 4,1};
                    x_guest_write(0x4ffc,list,8); x_guest_write(0x5ffb,pair,8);
                    c = fx_context(handle,0x4ffc,0x21EA1B); reject(&c,0x37C5E4);
                    pair[1] = 0; pair[0] ^= 1; x_guest_write(0x5ffb,pair,8); reject(&c,0x37C5E4);
                    pair[0] ^= 1; x_guest_write(0x5ffb,pair,8);
                    c = fx_context(handle,0x4ffc,0x21EA1C); reject(&c,0x37C5E4);
                    c = fx_context(handle,0x4ffc,0x21EA1B); call(&c,0x37C5E4,0,2);
                    assert(target->route_count == 1 && target->route_bins[0] == pair[0]);
                    c = fx_context(handle,0,0x21EA2A); reject(&c,0x37B6DF);
                    c = fx_context(handle,0,0x21EA29); call(&c,0x37B6DF,0,4);
                    assert(target->started && test_fx_playing == (1u << (8 + bin - 15)) - 1);
                    c = fx_context(handle,0,0x21EA29); reject(&c,0x379F45);
                }
                assert(device.references == 16 && device.children == 15);
                for(unsigned bin=15;bin<=22;++bin){
                    uint32_t handle=extra_sources[bin-15];h2_audio_buffer *target=find_buffer(handle-0x1c);
                    h2_audio_buffer before=*target;unsigned mask=test_extra_muted;
                    c=fx_context(handle,(uint32_t)-6400,0x21F1A1);reject(&c,0x37B66F);
                    c=fx_context(handle,(uint32_t)-6399,0x21F1A0);reject(&c,0x37B66F);
                    c=fx_context(handle,(uint32_t)-6400,0x21F1A0);reject(&c,0x37B6A7);
                    for(unsigned bad=0;bad<7;++bad){
                        *target=before;
                        switch(bad){case 0:target->started=0;break;case 1:target->stopped=1;break;
                        case 2:target->headroom=1;break;case 3:target->route_count=2;break;
                        case 4:target->route_bins[0]^=1;break;case 5:target->route_gains[0]=1;break;
                        case 6:target->volume=-1;break;}
                        h2_audio_buffer invalid=*target;c=fx_context(handle,(uint32_t)-6400,0x21F1A0);reject(&c,0x37B66F);
                        assert(!memcmp(target,&invalid,sizeof invalid)&&test_extra_muted==mask);
                    }
                    *target=before;test_extra_mute_failure=1;
                    c=fx_context(handle,(uint32_t)-6400,0x21F1A0);reject(&c,0x37B66F);
                    assert(!memcmp(target,&before,sizeof before)&&test_extra_muted==mask);test_extra_mute_failure=0;
                    c=fx_context(handle,(uint32_t)-6400,0x21F1A0);call(&c,0x37B66F,0,2);
                    before.volume=-6400;assert(!memcmp(target,&before,sizeof before));
                    assert(test_extra_muted==(1u<<(bin-14))-1);
                    c=fx_context(handle,(uint32_t)-6400,0x21F1A0);call(&c,0x37B66F,0,2);
                    c=fx_context(handle,0,0x21F1A0);reject(&c,0x37B66F); /* unmute remains unsupported */
                }
                fixed_commit_tests(dev);
                for (unsigned field=0;field<6;++field) {
                    c=global_pcm_description();uint32_t value=read32(0x3ffd+field*4)^1;x_guest_write(0x3ffd+field*4,&value,4);
                    reject(&c,0x37D7DE);
                }
                for (unsigned byte=0;byte<18;++byte) {
                    c=global_pcm_description();uint8_t value; x_guest_read(&value,0x4ff9+byte,1);value^=1;x_guest_write(0x4ff9+byte,&value,1);
                    reject(&c,0x37D7DE);
                }
                c=global_pcm_description();X_M32(c.r[4])++;reject(&c,0x37D7DE);
                c=global_pcm_description();X_M32(c.r[4]+8)=0x4ff9;reject(&c,0x37D7DE);
                c=global_pcm_description();allocation_failure=1;uint32_t untouched=read32(0x8ffb);
                call(&c,0x37D7DE,0x8007000e,2);allocation_failure=0;assert(read32(0x8ffb)==untouched && device.references==16);
                c=global_pcm_description();call(&c,0x37D7DE,0,2);uint32_t pcm_handle=read32(0x8ffb);
                h2_audio_buffer *pcm=find_buffer(pcm_handle-0x1c);assert(pcm && pcm->gp_pcm && pcm->headroom==600 && pcm->frequency==1000);
                assert(g_v[pcm->voice].channels==1 && g_v[pcm->voice].bits==8 && g_v[pcm->voice].freq_override==1000);
                assert(device.references==17 && device.children==16 && pcm->route_bins[0]==14);
                c=global_pcm_description();reject(&c,0x37D7DE); /* preceding buffer must have played */
                uint8_t bytes[1000];for(unsigned i=0;i<1000;++i)bytes[i]=(uint8_t)i;x_guest_write(0xb000,bytes,1000);
                c=context(pcm_handle,0xb000,1000,0);X_M32(c.r[4])=0x221593;reject(&c,0x37CC4A);
                X_M32(c.r[4])=0x221592;call(&c,0x37CC4A,0,3);assert(pcm->bytes==1000 && pcm->mirror);
                c=fx_context(pcm_handle,0,0x22159D);call(&c,0x37B6A7,0,2);
                /* Exercise the real shared PCM decoder independently of the
                 * separately tested loaded-DSP Play: both audible test input
                 * and fully muted input advance effective1000Hz source time. */
                int16_t output[2048];xk_audio_voice_play(pcm->voice,1);xk_audio_mix(output,1024);
                unsigned nonzero=0;for(unsigned i=0;i<2048;++i)nonzero+=output[i]!=0;assert(nonzero && g_v[pcm->voice].frames_out==1024);
                xk_audio_voice_stop(pcm->voice);
                c=fx_context(pcm_handle,(uint32_t)-9999,0x2215AC);reject(&c,0x37B66F);
                c=fx_context(pcm_handle,(uint32_t)-10000,0x2215AC);call(&c,0x37B66F,0,2);
                xk_audio_voice_play(pcm->voice,1);xk_audio_mix(output,1024);
                for(unsigned i=0;i<2048;++i)assert(!output[i]);assert(g_v[pcm->voice].frames_out==1024 && g_v[pcm->voice].pos);
                xk_audio_voice_stop(pcm->voice);
                c=context(pcm_handle,0,0,0);X_M32(c.r[4])=0x2215B9;reject(&c,0x37B6DF);
                c=context(pcm_handle,0,0,1);X_M32(c.r[4])=0x2215B9;test_gp_pcm_play_failure=1;reject(&c,0x37B6DF);
                test_gp_pcm_play_failure=0;call(&c,0x37B6DF,0,4);assert(pcm->started && g_v[pcm->voice].playing);
                c=context(pcm_handle,0,0,0);reject(&c,0x379F45);
                c=fx_context(pcm_handle,0,0x2215AC);reject(&c,0x37B66F);
                xk_audio_voice_stop(pcm->voice);pcm->started=0; /* inactive teardown only */
                c=context(pcm_handle,0,0,0);call(&c,0x379F45,0,1);assert(device.references==16 && device.children==15);

                for (unsigned i = 8; i-- > 0;) {
                    uint32_t handle = extra_sources[i]; h2_audio_buffer *target = find_buffer(handle - 0x1c);
                    target->started = 0; test_fx_playing &= ~test_fx_mask(target->fx_bin); /* test teardown only */
                    c = context(handle,0,0,0); call(&c,0x379F45,0,1);
                }
#endif
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
