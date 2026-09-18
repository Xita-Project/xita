/* Full guest ABI with real decoder and scripted sink completion. */
#define H2_AUDIO_DSP_TEST_MAIN old_dsp_tests
#include "audio_dsp_test.c"
static xctx global_stream_description(void)
{
    const uint8_t format[18]={1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,0,0};
    uint32_t fields[6]={0x40000000,2,0x4ff9,0x335d82,0x6fe6,0x8ffd},routes[2]={1,0xaffe},pair[2]={27,0};
    x_guest_write(0x3ffd,fields,24);x_guest_write(0x4ff9,format,18);x_guest_write(0x8ffd,routes,8);x_guest_write(0xaffe,pair,8);
    xctx c=context(0x3ffd,0x6ffe,0,0);X_M32(c.r[4])=0x335ed8;return c;
}
static xctx packet(uint32_t handle,unsigned index,uint32_t caller)
{
    uint32_t fields[6]={0xc000+index*320,320,0,0,index,0};x_guest_write(0x3ffd,fields,24);
    xctx c=context(handle,0x3ffd,0,0);X_M32(c.r[4])=caller;return c;
}
static void work_guard(h2_audio_stream *s,int allowed)
{
    xctx c=context(0,0,0,0);c.fs_base=0x7000;X_M8(c.fs_base+0x24)=0;X_M32(c.r[4])=0x21EC3C;
    xctx before=c;h2_audio_stream state=*s;h2_audio_device_snapshot owner=device;
    uint32_t fp=native_fp;unsigned callbacks=test_stream_callbacks;
    uint8_t *memory=malloc(0x200000);assert(memory);memcpy(memory,g_xram,0x200000);
    if(!setjmp(stopped)){h2_audio_guest_entry(&c,0x37B844);assert(allowed);}else assert(!allowed);
    assert(!memcmp(&c,&before,sizeof c)&&!memcmp(s,&state,sizeof state)&&!memcmp(&device,&owner,sizeof owner));
    assert(native_fp==fp&&test_stream_callbacks==callbacks&&!memcmp(memory,g_xram,0x200000));free(memory);
}
int main(void)
{
    g_xram=calloc(1,0x200000);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);assert(g_xram&&g_xpt);
    for(unsigned i=0;i<1u<<20;++i)g_xpt[i]=0x1ff000;
    for(unsigned i=1;i<16;++i)g_xpt[i]=(i-1)*4096;
    g_xpt[3]=0x8000;g_xpt[7]=0x9000;g_xpt[0x386]=0xf000;g_xpt[0x387]=0x1e0000;X_M32(0x386B0C)=0;
    xctx c=context(0,0x6200,0,0);call(&c,0x37D797,0,3);
    c=download_context();call(&c,0x37B86D,0,4);
    c=global_stream_description();call(&c,0x37D835,0,2);uint32_t handle=read32(0x6ffe);
    h2_audio_stream *s=stream_live(&c,0x37D835,handle);
    c=context(handle,0,0,0);call(&c,0x37B818,0,2);
    for(unsigned f=0;f<6;++f){c=packet(handle,0,0x33610e);uint32_t bad=read32(0x3ffd+f*4)^1;x_guest_write(0x3ffd+f*4,&bad,4);reject(&c,0x37AD25);}
    c=packet(handle,0,0x33610f);reject(&c,0x37AD25);
    c=packet(handle,0,0x33610e);X_M8(0xc080)=1;reject(&c,0x37AD25);X_M8(0xc080)=0;
    c=packet(handle,0,0x33610e);allocation_failure=1;call(&c,0x37AD25,0x8007000e,3);allocation_failure=0;
    assert(!s->submitted&&!stream_worker);
    c=packet(handle,0,0x33610e);test_stream_thread_failure=1;call(&c,0x37AD25,0x8007000e,3);test_stream_thread_failure=0;
    assert(!s->submitted&&!stream_worker);
    test_stream_submit_failure=0;
    for(unsigned i=0;i<2;++i){c=packet(handle,i,0x33610e);call(&c,0x37AD25,0,3);assert(s->packets[i].mirror&&s->packets[i].ticket==i+1);}
    assert(s->submitted==2&&g_v[s->voice].nq==2);
    work_guard(s,1);assert(g_v[s->voice].nq==2);
    s->flags=0x20000000;work_guard(s,0);s->flags=0x40000000;
    s->callback++;work_guard(s,0);s->callback--;
    xk_thread *worker=stream_worker;stream_worker=NULL;work_guard(s,0);stream_worker=worker;
    uint32_t mirror=s->packets[0].mirror;s->packets[0].mirror=0;work_guard(s,0);s->packets[0].mirror=mirror;
    s->completed=3;work_guard(s,0);s->completed=0;
    ++s->submitted;work_guard(s,0);--s->submitted;

    c=packet(handle,0,0x33610e);call(&c,0x37AD25,0x88780032,3);assert(s->submitted==2);
    c=context(handle,0,0,0);reject(&c,0x37AB87);
    /* Only the scripted sink's explicit completion marks a packet ready; the
     * worker never retires or calls back on its own. */
    c=context(0,0,0,0);c.fs_base=0x7000;X_M8(c.fs_base+0x24)=0;xctx saved=c;uint32_t fp=native_fp;
    stream_poll(&c);assert(!test_stream_callbacks&&s->packets[0].mirror&&!s->packets[0].ready);
    int16_t output[2048];xk_audio_mix(output,1024);xk_audio_mix(output,1024);
    assert(xk_audio_stream_pop_consumed(s->voice)==1 && xk_audio_stream_pop_consumed(s->voice)==1);
    test_stream_ready=2;test_stream_callback_context=s->context;
    work_guard(s,1);assert(!test_stream_callbacks); /* nothing polled yet: DoWork has nothing to deliver */
    stream_poll(&c);
    assert(!test_stream_callbacks && s->completed==0 && s->packets[0].ready && s->packets[1].ready &&
           s->packets[0].mirror && s->packets[1].mirror);           /* ready, still owned: no callback off DoWork */
    assert(!memcmp(&c,&saved,sizeof c)&&native_fp==fp&&!X_M8(c.fs_base+0x24));
    /* DirectSoundDoWork retires ready packets in ticket order and invokes the
     * original callbacks on the calling thread, then restores its context. */
    c=context(0,0,0,0);c.fs_base=0x7000;X_M8(c.fs_base+0x24)=0;X_M32(c.r[4])=0x21EC3C;saved=c;fp=native_fp;
    h2_audio_guest_entry(&c,0x37B844);
    assert(test_stream_callbacks==2 && s->completed==2 && !s->packets[0].mirror && !s->packets[1].mirror &&
           !s->packets[0].ready && !s->packets[1].ready);
    assert(!memcmp(&c,&saved,sizeof c)&&native_fp==fp&&!X_M8(c.fs_base+0x24));
    work_guard(s,1);assert(test_stream_callbacks==2);              /* nothing left to deliver */
    /* No flush API is claimed: this is fixture-only inactive teardown. */
    xk_audio_voice_stop(s->voice);s->submitted=0;c=context(handle,0,0,0);call(&c,0x37AB87,0,1);
    uint32_t dev=read32(0x6200);c=context(dev-8,0,0,0);call(&c,0x37C70F,0,1);
    free(g_xram);free(g_xpt);
    puts("Halo 2 packets: exact zero input, immutable mirrors, full ABI, allocation rollback, capacity, ownership and sink-gated callback context pass");return 0;
}
