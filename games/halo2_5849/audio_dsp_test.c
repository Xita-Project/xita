/* Actual adapter/mixer with synthetic DSP provider for ABI/lifetime tests.
 * The real interpreter and owned-program execution are separately tested. */
#define H2_AUDIO_DSP 1
#define xv_call test_stream_invoke
#define main original_pcm_test_main
#include "audio_buffer_test.c"
#undef main
#undef xv_call
struct h2_dsp_engine { uint32_t marker; };
static unsigned dsp_opens, dsp_closes, dsp_reads, dsp_writes;
static int dsp_write_failure;
static unsigned description_probe_read, description_probe_enabled;
static uint32_t dsp_written[4][2];
static int dsp_failure;
static unsigned test_fx_bound, test_fx_routes, test_fx_playing;
static unsigned test_fx_mask(unsigned key)
{
    if (key == 13) return 1;
    if (key >= 15 && key <= 22) return 1u << (7 + key - 15);
    unsigned bin = key & 0xffff; assert(bin >= 23 && bin <= 25);
    return 1u << (1 + (bin - 23) * 2 + !!(key & 0x10000));
}
int h2_audio_backend_fx_bind(h2_dsp_engine *s, unsigned bin)
{ unsigned mask = test_fx_mask(bin); assert(s == effects && !(test_fx_bound & mask)); test_fx_bound |= mask; if (bin == 13) test_fx_routes = 2; return 0; }
int h2_audio_backend_fx_bind_spatial(h2_dsp_engine *s, unsigned bin, const int8_t taps[31])
{ unsigned mask = test_fx_mask(0x10000u | bin); assert(s == effects && test_fx_bound == mask - 1 && test_fx_playing == mask - 1 && taps); test_fx_bound |= mask; return 0; }
int h2_audio_backend_fx_route(unsigned bin, unsigned routes)
{ assert(bin == 13 && (test_fx_bound & 1) && !(test_fx_playing & 1) && routes == 6); test_fx_routes = routes; return 0; }
static unsigned test_fx23_output_mask, test_fx24_output_mask, test_fx25_routed;
static unsigned test_fx23_muted, test_fx24_muted, test_fx25_muted;
int h2_audio_backend_fx_route_mask(unsigned key, unsigned output_mask)
{
    if (key >= 15 && key <= 22) {
        unsigned mask = test_fx_mask(key); assert(test_fx_bound == mask * 2 - 1 && test_fx_playing == mask - 1);
        assert(output_mask == (1u << (6 + (key - 15) % 4))); return 0;
    }
    assert(test_fx_bound == 127 && test_fx_playing == 127);
    if (key == 23) { assert(output_mask == 64); test_fx23_output_mask = output_mask; }
    else if (key == 24) { assert(output_mask == 128 && test_fx23_muted); test_fx24_output_mask = output_mask; }
    else { assert(key == H2_FX_SPATIAL25 && output_mask == 1024 && test_fx24_muted); test_fx25_routed = 1; }
    return 0;
}
int h2_audio_backend_fx_mute(unsigned key)
{
    assert(test_fx_bound == 127 && test_fx_playing == 127);
    if (key == H2_FX_SPATIAL23) { assert(test_fx23_output_mask == 64); test_fx23_muted = 1; }
    else if (key == H2_FX_SPATIAL24) { assert(test_fx24_output_mask == 128); test_fx24_muted = 1; }
    else { assert(key == 25 && test_fx25_routed); test_fx25_muted = 1; }
    return 0;
}
static unsigned test_fx_filtered;
static int test_fx_filter_failure;
int h2_audio_backend_fx_filter(unsigned key)
{
    assert(test_fx_bound == 127 && test_fx_playing == 127 && test_fx25_muted && (key == 23 || key == 24));
    if (test_fx_filter_failure) return -1;
    test_fx_filtered |= 1u << (key - 23); return 0;
}
static int test_gp_pcm_play_failure;
int h2_audio_backend_gp_pcm_play(int voice)
{
    assert(test_fx_playing==0x7fff);
    if (test_gp_pcm_play_failure) return -1;
    xk_audio_voice_play(voice,1); return 0;
}
static xk_thread test_stream_worker;
static int test_stream_thread_failure;
xk_thread *xk_thread_create_host(void (*entry)(xctx *,void *),void *arg)
{ assert(entry==stream_worker_entry && !arg);return test_stream_thread_failure ? NULL : &test_stream_worker; }
void xk_thread_kick(xk_thread *thread) { assert(thread==&test_stream_worker); }
void xk_sleep_us(uint64_t us) { (void)us;assert(0); }
static unsigned test_stream_callbacks;
static uint32_t test_stream_callback_context;
static unsigned test_reverb_calls,test_reverb_queues,test_reverb_failure,test_reverb_corruption;
static uint32_t test_reverb_parameters[66],test_reverb_flags;
void test_stream_invoke(xctx *c,uint32_t routine)
{
    if(routine==0x3838a4){
        uint32_t base=reverb_conversion.arena;
        assert(reverb_conversion.context==c && base && c->r[4]==base+0xf00);
        assert(X_M32(c->r[4])==0xdead0004 && X_M32(c->r[4]+4)==base+0x300 && X_M32(c->r[4]+8)==base);
        const uint32_t entries[]={0x379d4b,0x383167,0x3831b5,0x383243,0x38327c,0x38329a,0x3832d2,
            0x38336c,0x3833e9,0x383436,0x38357f,0x3835e0,0x3837ba,0x3838a4};
        for(unsigned i=0;i<sizeof entries/sizeof *entries;++i)h2_audio_guest_entry(c,entries[i]);
        xctx other=*c;assert(!original_reverb_entry(&other,0x3838a4));
        for(unsigned i=0;i<66;++i)X_M32(base+280+4*i)=i%2?0xff800001+i:0x123456+i;
        X_M32(base+0x340)=base;c->r[4]+=12;c->r[0]^=0xabcdef;native_fp^=0x777777;++test_reverb_calls;
        if(test_reverb_corruption==1)c->r[3]^=1;
        if(test_reverb_corruption==2)c->r[4]+=4;
        if(test_reverb_corruption==3)X_M8(base+544)=1;
        return;
    }
    assert(routine==0x335d82 && X_M32(c->r[4])==0xdead0003 && X_M32(c->r[4]+4)==test_stream_callback_context &&
        X_M32(c->r[4]+8)==test_stream_callbacks && !X_M32(c->r[4]+12) && X_M8(c->fs_base+0x24)==2);
    ++test_stream_callbacks;c->r[0]^=0xabcdef;c->r[1]^=0x123456;c->r[4]+=16;native_fp^=0x777777;
}
static int test_stream_submit_failure=1;
static unsigned test_stream_serial,test_stream_ready,test_stream_completed;
int h2_audio_backend_stream_submit(int voice,uint32_t mirror,uint64_t *ticket)
{
    if(test_stream_submit_failure)return -1;
    assert(xk_audio_stream_push(voice,mirror,320)==0);*ticket=++test_stream_serial;return 0;
}
int h2_audio_backend_stream_complete(int voice,uint64_t *ticket)
{
    assert(voice>=0);
    if(test_stream_completed==test_stream_ready)return 0;
    *ticket=++test_stream_completed;return 1;
}
int h2_audio_backend_fx_play(unsigned bin)
{ unsigned mask = test_fx_mask(bin); assert((test_fx_bound & mask) && !(test_fx_playing & mask) && (bin != 13 || test_fx_routes == 6)); test_fx_playing |= mask; return 0; }
int h2_audio_backend_fx_forget(unsigned bin)
{ unsigned mask = test_fx_mask(bin); assert((test_fx_bound & mask) && !(test_fx_playing & mask)); test_fx_bound &= ~mask; if (bin == 13) test_fx_routes = 0; return 0; }
int h2_audio_backend_effect_read(h2_dsp_engine *s, unsigned index, unsigned offset, void *out, unsigned bytes)
{
    if (description_probe_enabled && index==9 && !offset && bytes==280) {
        assert(s==effects);++description_probe_read;
        for (unsigned i=0;i<70;++i) ((uint32_t*)out)[i]=0x10000u+i;
        return 1;
    }
    if (index >= 15 || offset > 128 || bytes > 128 - offset) return 0;
    return h2_dsp_read_effect(s, index, offset, out, bytes);
}
int h2_audio_backend_effect_write_pair(h2_dsp_engine *s, unsigned index, unsigned offset, uint32_t first, uint32_t second)
{
    assert(s==effects && index>=4 && index<=7 && offset==32);
    if(dsp_write_failure || ((first|second)&0xff000000u))return 0;
    ++dsp_writes;dsp_written[index-4][0]=first;dsp_written[index-4][1]=second;return 1;
}
int h2_audio_backend_queue_reverb9(h2_dsp_engine *s,uint32_t flags,const uint32_t parameters[66])
{
    assert(s==effects&&!reverb_conversion.context&&!reverb_conversion.arena);
    if(test_reverb_failure)return 0;
    ++test_reverb_queues;test_reverb_flags=flags;memcpy(test_reverb_parameters,parameters,sizeof test_reverb_parameters);return 1;
}
h2_dsp_engine*h2_dsp_asset_open(const char*path,h2_dsp_status*status)
{
    assert(!strcmp(path,"app0:halo2-dsp.bin")&&healthy);
    ++dsp_opens;*status=(h2_dsp_status){.effect_count=15,.scratch_bytes=0xD000,.frames=2,.instructions=100,.transfers=3};
    if(dsp_failure){status->fault="injected initialization fault";return NULL;}
    h2_dsp_engine*s=malloc(sizeof(*s));assert(s);s->marker=0x1234;return s;
}
void h2_dsp_destroy(h2_dsp_engine*s){assert(s&&s->marker==0x1234);++dsp_closes;free(s);}
int h2_dsp_effect_map(const h2_dsp_engine*s,uint32_t index,h2_dsp_effect*out)
{
    assert(s&&s->marker==0x1234&&index<15);
    *out=(h2_dsp_effect){0x818+index*64,64,0x3F98+index*128,128,index*16,16,index*64,64};return 1;
}
int h2_dsp_copy_space(const h2_dsp_engine*s,unsigned space,uint32_t offset,void*out,uint32_t bytes)
{
    assert(s&&s->marker==0x1234&&space<4);
    uint8_t*p=out;for(unsigned i=0;i<bytes;i++)p[i]=(uint8_t)(space*31+offset+i);return 1;
}
int h2_dsp_read_effect(const h2_dsp_engine*s,uint32_t index,uint32_t offset,void*out,uint32_t bytes)
{
    assert(s&&s->marker==0x1234&&index<15&&offset<=128&&bytes<=128-offset);
    ++dsp_reads;uint8_t*p=out;for(unsigned i=0;i<bytes;i++)p[i]=(uint8_t)(index*64+offset+i);return 1;
}
static xctx download_context(void)
{
    xctx c=context(0x6ffc,0x6100,1,0x8100);
    X_M32(c.r[4])=0x1913A9;x_guest_write(0x6ffc,"DSPImage",9);X_M32(0x6100)=9;X_M32(0x6104)=10;return c;
}
static xctx query_context(uint32_t dev,uint32_t index,uint32_t offset,uint32_t bytes)
{
    xctx c=context(dev,index,offset,0x8ffe);X_M32(c.r[4]+20)=bytes;return c;
}
static void dsp_reject(xctx*c,uint32_t ip)
{
    unsigned op=dsp_opens,cl=dsp_closes,rd=dsp_reads,wr=dsp_writes;h2_dsp_engine*owner=effects;
    uint32_t guest=effects_guest,bytes=effects_guest_bytes;
    reject(c,ip);assert(op==dsp_opens&&cl==dsp_closes&&rd==dsp_reads&&wr==dsp_writes&&owner==effects&&guest==effects_guest&&bytes==effects_guest_bytes);
}
static xctx write_context(uint32_t dev,unsigned index)
{
    static const uint32_t callers[]={0x191294,0x1912ac,0x1912c3,0x1912db};
    xctx c=context(dev,index,32,0x6ffe);X_M32(c.r[4])=callers[index-4];
    X_M32(c.r[4]+20)=8;X_M32(c.r[4]+24)=0;return c;
}
static xctx reverb_context(void)
{
    const uint32_t desc[13]={12,(uint32_t)-6400,(uint32_t)-6400,0,0x3f800000,0x3f800000,
        (uint32_t)-6400,0,(uint32_t)-6400,0,0x42c80000,0x42c80000,0x459c4000};
    x_guest_write(0x6ffe,desc,sizeof desc);xctx c=context(9,0x6ffe,0,0);X_M32(c.r[4])=0x21ee74;
    c.df=c.fsp=0;c.fcw=0x37f;c.fsw=0;native_fp=0xa0000011;return c;
}
static void reverb_expect_stop(xctx *c, uint32_t ip, int original)
{
    if (!setjmp(stopped)) {
        if (original) h2_audio_guest_entry(c, ip);
        else h2_audio_host_call(c, ip);
        assert(0);
    }
}
static void reverb_adapter_tests(void)
{
    description_probe_enabled=1;
    for(unsigned mode=0;mode<3;++mode){
        xctx c=reverb_context();c.fcw=(uint16_t[]){0x37f,0x27f,0x23f}[mode];
        unsigned a=allocs,f=frees,q=test_reverb_queues,n=test_reverb_calls;
        uint8_t input[52],after[52];x_guest_read(input,0x6ffe,52);
        call(&c,0x37ba6f,0,3);assert(allocs==a+1&&frees==f+1&&test_reverb_queues==q+1&&test_reverb_calls==n+1);
        assert(test_reverb_flags==0x10004&&!reverb_conversion.context&&!reverb_conversion.arena);
        for(unsigned i=0;i<66;++i)assert(test_reverb_parameters[i]==(i%2?0xff800001u+i:0x123456u+i));
        x_guest_read(after,0x6ffe,52);assert(!memcmp(input,after,52));
    }
    for(unsigned field=0;field<13;++field){
        xctx c=reverb_context();uint32_t words[13];x_guest_read(words,0x6ffe,sizeof words);words[field]^=1;x_guest_write(0x6ffe,words,sizeof words);
        dsp_reject(&c,0x37ba6f);
    }
    for(unsigned bad=0;bad<10;++bad){
        xctx c=reverb_context();
        switch(bad){
        case 0:X_M32(c.r[4])^=4;break;
        case 1:X_M32(c.r[4]+4)=10;break;
        case 2:X_M32(c.r[4]+12)=0x6000;break;
        case 3:X_M32(c.r[4]+8)=0xfffffff0;break;
        case 4:X_M32(c.r[4]+8)=effects_guest;break;
        case 5:X_M32(c.r[4]+8)=c.r[4];break;
        case 6:c.df=1;break;
        case 7:c.fsp=1;break;
        case 8:c.fcw=0x7f;break;
        case 9:native_fp|=0x1000000;break;
        }
        dsp_reject(&c,0x37ba6f);
    }
    for(unsigned bit=0;bit<32;++bit)if(0x03f79f00u&(1u<<bit)){
        xctx c=reverb_context();native_fp|=1u<<bit;dsp_reject(&c,0x37ba6f);
    }
    xctx c=reverb_context();allocation_failure=1;unsigned a=allocs,f=frees,q=test_reverb_queues;
    call(&c,0x37ba6f,0x8007000e,3);assert(allocs==a+1&&frees==f&&test_reverb_queues==q);allocation_failure=0;
    c=reverb_context();xctx saved=c;uint32_t fp=native_fp;h2_audio_device_snapshot owner=device;
    a=allocs;f=frees;test_reverb_failure=1;
    reverb_expect_stop(&c,0x37ba6f,0);
    assert(!memcmp(&c,&saved,sizeof c)&&!memcmp(&owner,&device,sizeof owner)&&fp==native_fp);
    assert(allocs==a+1&&frees==f+1&&test_reverb_queues==q&&!reverb_conversion.context&&!reverb_conversion.arena);
    test_reverb_failure=0;
    for(unsigned damage=1;damage<=3;++damage){
        c=reverb_context();saved=c;fp=native_fp;a=allocs;f=frees;test_reverb_corruption=damage;
        reverb_expect_stop(&c,0x37ba6f,0);
        assert(!memcmp(&c,&saved,sizeof c)&&!memcmp(&owner,&device,sizeof owner)&&fp==native_fp);
        assert(allocs==a+1&&frees==f+1&&test_reverb_queues==q&&!reverb_conversion.context&&!reverb_conversion.arena);
    }
    test_reverb_corruption=description_probe_enabled=0;
    uint32_t base=xk_mem_alloc(4096,4096,0,0,0);assert(base);
    for(unsigned bad=0;bad<7;++bad){
        c=reverb_context();c.r[1]=base+0x340;c.r[4]=base+0xf00;uint32_t ip=0x3838a4;
        reverb_conversion.context=&c;reverb_conversion.arena=base;
        switch(bad){
        case 0:ip=0x37ba6f;break;
        case 1:reverb_conversion.arena=0;break;
        case 2:c.r[4]=base+0x7fc;break;
        case 3:c.r[4]=base+0xf04;break;
        case 4:c.r[4]|=1;break;
        case 5:c.df=1;break;
        case 6:c.r[1]^=4;break;
        }
        saved=c;fp=native_fp;
        reverb_expect_stop(&c,ip,1);
        assert(!memcmp(&c,&saved,sizeof c)&&fp==native_fp);
    }
    reverb_conversion.context=NULL;reverb_conversion.arena=0;assert(xk_mem_free(base)==0);
}
#ifndef H2_AUDIO_DSP_TEST_MAIN
#define H2_AUDIO_DSP_TEST_MAIN main
#endif
int H2_AUDIO_DSP_TEST_MAIN(void)
{
    g_xram=malloc(0x200000);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);assert(g_xram&&g_xpt);
    memset(g_xram,0xcc,0x200000);for(unsigned i=0;i<1u<<20;i++)g_xpt[i]=0x1ff000;
    for(unsigned i=1;i<16;i++)g_xpt[i]=(i-1)*4096;
    g_xpt[7]=0x8000;g_xpt[9]=0x6000; /* physically noncontiguous input/output */
    g_xpt[0x386]=0xf000;X_M32(0x386B0C)=0;
    xctx c=context(0,0x6200,0,0);call(&c,0x37D797,0,3);uint32_t dev=read32(0x6200);
    c=query_context(dev,0,0,8);dsp_reject(&c,0x37B5E6);
    c=download_context();X_M32(c.r[4])^=4;dsp_reject(&c,0x37B86D);
    c=download_context();X_M32(c.r[4]+12)=0;dsp_reject(&c,0x37B86D);
    c=download_context();X_M32(0x6104)=9;dsp_reject(&c,0x37B86D);
    c=download_context();X_M32(c.r[4]+16)=dev;dsp_reject(&c,0x37B86D);
    c=download_context();X_M32(c.r[4]+16)=c.r[4]+4;dsp_reject(&c,0x37B86D);
    c=download_context();device.children=1;dsp_reject(&c,0x37B86D);device.children=0;
    c=download_context();allocation_failure=1;uint32_t before=read32(0x8100);
    call(&c,0x37B86D,0x8007000e,4);assert(!effects&&dsp_opens==1&&dsp_closes==1&&read32(0x8100)==before);allocation_failure=0;
    c=download_context();call(&c,0x37B86D,0,4);assert(effects&&dsp_opens==2&&dsp_closes==1);
    uint32_t descriptor=read32(0x8100);assert(descriptor==effects_guest&&read32(descriptor)==15&&read32(descriptor+4)==4096);
    assert(effects_guest_bytes==0x18000);
    for(unsigned i=0;i<15;i++){
        uint32_t map=descriptor+8+i*32;
        assert(read32(map)==descriptor+0x75cc+i*64&&read32(map+4)==64);
        assert(read32(map+8)==descriptor+0x1200+i*128&&read32(map+12)==128);
        assert(read32(map+16)==descriptor+0x5000+i*16&&read32(map+20)==16);
        assert(read32(map+24)==descriptor+0x17000+i*64&&read32(map+28)==64);
    }
    for(unsigned space=0,at=descriptor+4096;space<4;space++){
        unsigned n=space==0?0x4000:space==1?0x2000:space==2?0x4000:0xd000;
        for(unsigned i=0;i<n;i++)assert(X_M8(at+i)==(uint8_t)(space*31+i));at+=n;
    }
    c=download_context();dsp_reject(&c,0x37B86D); /* no replacement/leaked owner */
    /* Terminal description capture preserves caller, guest memory and DSP
     * ownership; invalid mappings/types never ask the provider to read. */
    uint8_t *probe_ram=malloc(0x200000);assert(probe_ram);
    for (unsigned scenario=0;scenario<8;++scenario) {
        c=context(9,0x6ffe,0,0);X_M32(c.r[4])=0x21ee74;
        uint32_t desc[13]={scenario==1?4:12};x_guest_write(0x6ffe,desc,sizeof desc);
        if(scenario==2)X_M32(c.r[4]+8)=0xfffffff0;
        if(scenario==3)c.r[4]|=1;
        if(scenario==4)X_M32(c.r[4]+4)=15;
        if(scenario==5)g_xpt[7]=0x1ff000;
        if(scenario==7){c.fcw=0x7f;c.fsp=3;c.df=1;native_fp=0x100009f;}
        description_probe_enabled=scenario!=6;
        xctx saved=c;h2_audio_device_snapshot owner=device;
        memcpy(probe_ram,g_xram,0x200000);unsigned writes=dsp_writes,reads=description_probe_read;
        uint32_t fp=native_fp;trace_effect_description(&c);
        assert(!memcmp(&c,&saved,sizeof c)&&!memcmp(&owner,&device,sizeof owner));
        assert(!memcmp(probe_ram,g_xram,0x200000)&&fp==native_fp&&writes==dsp_writes);
        assert(description_probe_read==reads+(scenario==0||scenario==7));
        g_xpt[7]=0x8000;
    }
    description_probe_enabled=0;free(probe_ram);
    reverb_adapter_tests();
    for(unsigned index=0;index<15;index++){
        c=query_context(dev,index,7,60);call(&c,0x37B5E6,0,5);
        uint8_t actual[60];x_guest_read(actual,0x8ffe,sizeof actual);
        for(unsigned i=0;i<60;i++)assert(actual[i]==(uint8_t)(index*64+7+i));
    }
    c=query_context(dev,15,0,8);call(&c,0x37B5E6,0x88780032,5);
    c=query_context(dev,4,127,2);dsp_reject(&c,0x37B5E6);
    c=query_context(dev,4,0,0xffffffff);dsp_reject(&c,0x37B5E6);
    c=query_context(dev,4,0,8);X_M32(c.r[4]+16)=effects_guest+4096;dsp_reject(&c,0x37B5E6);
    g_xpt[0x300]=g_xpt[effects_guest>>12];c=query_context(dev,4,0,8);X_M32(c.r[4]+16)=0x300010;dsp_reject(&c,0x37B5E6);
    c=query_context(dev,4,0,8);X_M32(c.r[4]+16)=c.r[4]+8;dsp_reject(&c,0x37B5E6);
    /* Public six-argument ABI, both parameter values, split guest pages and
     * exact original caller pairing. Synthetic provider is not a DSP oracle. */
    for(unsigned index=4;index<=7;index++)for(unsigned change=0;change<2;change++){
        uint32_t words[2]={change?0xabcdef:0,change?0x123456:30};x_guest_write(0x6ffe,words,8);
        c=write_context(dev,index);call(&c,0x37B60D,0,6);
        assert(!memcmp(dsp_written[index-4],words,8));
    }
    for(unsigned slot=0;slot<6;slot++){
        if(slot==3)continue; /* source bytes may be unaligned */
        c=write_context(dev,4);X_M32(c.r[4]+4+slot*4)^=1;dsp_reject(&c,0x37B60D);
    }
    c=write_context(dev,4);X_M32(c.r[4])^=1;dsp_reject(&c,0x37B60D);
    c=write_context(dev,4);X_M32(c.r[4]+16)=effects_guest+4096;dsp_reject(&c,0x37B60D);
    c=write_context(dev,4);X_M32(c.r[4]+16)=0x300010;dsp_reject(&c,0x37B60D);
    c=write_context(dev,4);X_M32(c.r[4]+16)=c.r[4]+8;dsp_reject(&c,0x37B60D);
    c=write_context(dev,4);X_M32(c.r[4]+16)=0xfffffffcu;dsp_reject(&c,0x37B60D);
    for(unsigned i=0;i<2;i++){
        uint32_t words[2]={1,2};words[i]|=0xff000000u;x_guest_write(0x6ffe,words,8);
        c=write_context(dev,4);dsp_reject(&c,0x37B60D);
    }
    uint32_t words[2]={0,30};x_guest_write(0x6ffe,words,8);dsp_write_failure=1;
    c=write_context(dev,4);dsp_reject(&c,0x37B60D);dsp_write_failure=0;
    c=context(0,0,0,1);dsp_reject(&c,0x37B6DF); /* no unprocessed effect playback */
    c=context(dev-8,0,0,0);call(&c,0x37C70F,0,1);assert(!effects&&!effects_guest&&!effects_guest_bytes&&dsp_closes==2&&!healthy);
    for(unsigned i=0;i<256;i++)assert(!pool[i]);free(g_xram);free(g_xpt);
    puts("Halo 2 DSP adapter: actual mixer owner, synthetic provider ABI, real-view mapping, query bounds, rollback and final lifetime pass");return 0;
}
