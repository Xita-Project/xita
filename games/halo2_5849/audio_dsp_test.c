/* Actual adapter/mixer with synthetic DSP provider for ABI/lifetime tests.
 * The real interpreter and owned-program execution are separately tested. */
#define H2_AUDIO_DSP 1
#define xv_call test_stream_invoke
#define main original_pcm_test_main
#include "audio_buffer_test.c"
#undef main
#undef xv_call
struct h2_dsp_engine { uint32_t marker; };
static unsigned dsp_opens, dsp_closes, dsp_reads;
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
void test_stream_invoke(xctx *c,uint32_t routine)
{
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
    if (index >= 15 || offset > 128 || bytes > 128 - offset) return 0;
    return h2_dsp_read_effect(s, index, offset, out, bytes);
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
    unsigned op=dsp_opens,cl=dsp_closes,rd=dsp_reads;h2_dsp_engine*owner=effects;
    uint32_t guest=effects_guest,bytes=effects_guest_bytes;
    reject(c,ip);assert(op==dsp_opens&&cl==dsp_closes&&rd==dsp_reads&&owner==effects&&guest==effects_guest&&bytes==effects_guest_bytes);
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
    c=context(0,0,0,1);dsp_reject(&c,0x37B6DF); /* no unprocessed effect playback */
    c=context(dev-8,0,0,0);call(&c,0x37C70F,0,1);assert(!effects&&!effects_guest&&!effects_guest_bytes&&dsp_closes==2&&!healthy);
    for(unsigned i=0;i<256;i++)assert(!pool[i]);free(g_xram);free(g_xpt);
    puts("Halo 2 DSP adapter: actual mixer owner, synthetic provider ABI, real-view mapping, query bounds, rollback and final lifetime pass");return 0;
}
