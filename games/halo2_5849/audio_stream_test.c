/* Real mixer voices behind the bounded stream allocation/ownership adapter.
 * Direct synthetic mixer feeds below test codec/gain; guest Process stays strict. */
#define main original_pcm_test_main
#include "audio_buffer_test.c"
#undef main

static xctx stream_description(uint32_t dev, unsigned kind)
{
    const uint8_t formats[3][20] = {
        {0x69,0,1,0,0x44,0xAC,0,0,0xE4,0x60,0,0,36,0,4,0,2,0,64,0},
        {0x69,0,2,0,0x44,0xAC,0,0,0xC8,0xC1,0,0,72,0,4,0,2,0,64,0},
        {1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,0,0,0,0}
    };
    uint32_t fields[6] = {0x20000000,2,0x4FF9,0x220730,123,0};
    x_guest_write(0x3FFD,fields,24);x_guest_write(0x4FF9,formats[kind],20);
    xctx c=context(dev,0x3FFD,0x6FFE,0);X_M32(c.r[4])=0x2AE692;return c;
}
static xctx global_stream_description(void)
{
    const uint8_t format[18]={1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,0,0};
    uint32_t fields[6]={0x40000000,2,0x4ff9,0x335d82,0x6fe6,0x8ffd};
    uint32_t routes[2]={1,0xaffe},pair[2]={27,0};
    x_guest_write(0x3ffd,fields,24);x_guest_write(0x4ff9,format,18);
    x_guest_write(0x8ffd,routes,8);x_guest_write(0xaffe,pair,8);
    xctx c=context(0x3ffd,0x6ffe,0,0);X_M32(c.r[4])=0x335ed8;return c;
}
static void reject_stream(xctx *c,uint32_t ip)
{
    h2_audio_stream before[XA_MAX_VOICES];memcpy(before,streams,sizeof before);
    reject(c,ip);assert(!memcmp(before,streams,sizeof before));
}
static void direct_codec_fixture(h2_audio_stream *s,unsigned kind,int expected)
{
    uint8_t encoded[144]={0};unsigned bytes;
    if(kind==2){
        int16_t pcm[128];for(unsigned i=0;i<128;i++)pcm[i]=1000;
        x_guest_write(0xA000,pcm,sizeof pcm);bytes=sizeof pcm;
    }else{
        unsigned channels=kind+1,block=36*channels;bytes=block*2;
        for(unsigned b=0;b<2;b++)for(unsigned ch=0;ch<channels;ch++){
            encoded[b*block+ch*4]=0xE8;encoded[b*block+ch*4+1]=3;
        }
        x_guest_write(0xA000,encoded,bytes);
    }
    g_master_pct=100;
    /* Real continuous stream decoding, including a split inside its header. */
    assert(!xk_audio_stream_push(s->voice,0xA000,3));
    assert(!xk_audio_stream_push(s->voice,0xA003,bytes-3));
    int16_t output[96];xk_audio_mix(output,48);
    for(unsigned f=3;f<48;f++)assert(output[f*2]==expected&&output[f*2+1]==expected);
    xk_audio_voice_stop(s->voice);xk_audio_stream_flush(s->voice);
    /* Reset only the synthetic feed's resampler history. No guest API does this. */
    g_v[s->voice].frac=0;memset(g_v[s->voice].last,0,sizeof g_v[s->voice].last);memset(g_v[s->voice].prev,0,sizeof g_v[s->voice].prev);
}
int main(void)
{
    g_xram=malloc(0x200000);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);assert(g_xram&&g_xpt);
    memset(g_xram,0xcc,0x200000);for(unsigned i=0;i<1u<<20;i++)g_xpt[i]=0x1ff000;
    for(unsigned i=1;i<16;i++)g_xpt[i]=(i-1)*4096;
    g_xpt[3]=0x8000;g_xpt[7]=0x9000;g_xpt[0x386]=0xf000;X_M32(0x386B0C)=0;
    xctx c=context(0,0x6200,0,0);call(&c,0x37D797,0,3);uint32_t dev=read32(0x6200);
    c=stream_description(dev,0);X_M32(c.r[4])++;reject_stream(&c,0x37D4E2);
    for(unsigned f=0;f<6;f++)if(f!=2&&f!=4){
        c=stream_description(dev,0);uint32_t bad=read32(0x3FFD+f*4)^1;x_guest_write(0x3FFD+f*4,&bad,4);reject_stream(&c,0x37D4E2);
    }
    for(unsigned i=0;i<20;i++){
        c=stream_description(dev,0);X_M8(0x4FF9+i)^=0x80;reject_stream(&c,0x37D4E2);
    }
    c=stream_description(dev,0);X_M32(c.r[4]+12)=dev;reject_stream(&c,0x37D4E2);
    c=stream_description(dev,0);X_M32(c.r[4]+12)=c.r[4]+8;reject_stream(&c,0x37D4E2);
    c=stream_description(dev,0);X_M32(c.r[4]+12)=0x4000;reject_stream(&c,0x37D4E2);
    c=stream_description(dev,0);X_M32(c.r[4]+12)=0x5000;reject_stream(&c,0x37D4E2);
    c=stream_description(dev,0);healthy=0;reject_stream(&c,0x37D4E2);healthy=1;
    c=stream_description(dev,0);X_M32(0x386B0C)=1;reject_stream(&c,0x37D4E2);X_M32(0x386B0C)=0;
    c=stream_description(dev,0);uint32_t before=read32(0x6FFE);allocation_failure=1;
    call(&c,0x37D4E2,0x8007000e,4);allocation_failure=0;
    assert(read32(0x6FFE)==before&&!device.children&&xk_audio_free_voices()==XA_MAX_VOICES);
    for(unsigned kind=0;kind<3;kind++){
        c=stream_description(dev,kind);call(&c,0x37D4E2,0,4);uint32_t handle=read32(0x6FFE);
        h2_audio_stream*s=stream_live(&c,0x37D4E2,handle);xa_voice*v=g_v+s->voice;
        assert(s->references==1&&s->headroom==600&&s->packet_limit==2&&s->callback==0x220730&&s->context==123);
        assert(X_M32(handle)==0x417170&&X_M32(handle+4)==0x417160&&X_M32(handle+8)==1);
        assert(v->used&&v->kind==2&&!v->playing&&!v->nq&&v->adpcm==(kind!=2)&&v->channels==(kind==0?1:2)&&v->rate==44100);
        assert(device.children==1&&device.references==2&&xk_audio_free_voices()==XA_MAX_VOICES-1);
        direct_codec_fixture(s,kind,500); /* fixed-point mixer rounds 600 dB100 to 128/256 */
        c=context(handle,0,0,0);call(&c,0x37B818,0,2);assert(s->headroom==0&&v->volume==1.0f);
        direct_codec_fixture(s,kind,1000);
        c=context(handle,1,0,0);reject_stream(&c,0x37B818);
        c=context(handle+4,0,0,0);reject_stream(&c,0x37AB40);
        c=context(handle,0,0,0);call(&c,0x37AB40,2,1);assert(s->references==2&&device.references==2);
        c=context(handle,0,0,0);call(&c,0x37AB87,1,1);assert(s->references==1);
        c=context(dev,handle+32,0,0);reject_stream(&c,0x37B5AE);
        g_xpt[0x300]=g_xpt[handle>>12];c=context(dev,0x300020,0,0);reject_stream(&c,0x37B5AE);
        c=context(handle,0,0,0);call(&c,0x37AB87,0,1);assert(!device.children&&device.references==1&&xk_audio_free_voices()==XA_MAX_VOICES);
        c=context(handle,0,0,0);reject_stream(&c,0x37AB87);
    }
    c=global_stream_description();X_M32(c.r[4])++;reject_stream(&c,0x37D835);
    for(unsigned f=0;f<6;++f){
        c=global_stream_description();uint32_t value=read32(0x3ffd+f*4)^1;
        x_guest_write(0x3ffd+f*4,&value,4);reject_stream(&c,0x37D835);
    }
    for(unsigned i=0;i<18;++i){c=global_stream_description();X_M8(0x4ff9+i)^=0x80;reject_stream(&c,0x37D835);}
    for(unsigned at=0;at<4;++at){
        c=global_stream_description();uint32_t address=at<2?0x8ffd+at*4:0xaffe + (at-2)*4;
        uint32_t value=read32(address)^1;x_guest_write(address,&value,4);reject_stream(&c,0x37D835);
    }
    const uint32_t outputs[]={0x4000,0x5000,0x9000,0xb000,0x1ff8};
    for(unsigned i=0;i<sizeof outputs/sizeof outputs[0];++i){
        c=global_stream_description();uint32_t ctx=outputs[i]-24;
        x_guest_write(0x3ffd+16,&ctx,4);X_M32(c.r[4]+8)=outputs[i];reject_stream(&c,0x37D835);
    }
    c=global_stream_description();before=read32(0x6ffe);allocation_failure=1;
    call(&c,0x37D835,0x8007000e,2);allocation_failure=0;
    assert(read32(0x6ffe)==before && device.references==1 && !device.children);
    c=global_stream_description();call(&c,0x37D835,0,2);uint32_t global_handle=read32(0x6ffe);
    h2_audio_stream *gs=stream_live(&c,0x37D835,global_handle);xa_voice *gv=g_v+gs->voice;
    assert(gs->flags==0x40000000 && gs->route_bin==27 && gs->callback==0x335d82 && gs->context==0x6fe6 && gs->packet_limit==2);
    assert(gv->used && gv->kind==2 && !gv->playing && !gv->nq && !gv->adpcm && gv->channels==1 && gv->bits==16 && gv->rate==8000);
    /* Synthetic direct feed proves the real decoder/attenuation; guest Process
     * and GP27 routing are intentionally still unsupported. */
    int16_t samples[64];for(unsigned i=0;i<64;++i)samples[i]=1000;x_guest_write(0xc000,samples,sizeof samples);
    assert(!xk_audio_stream_push(gs->voice,0xc000,sizeof samples));int16_t decoded[96];xk_audio_mix(decoded,48);
    for(unsigned f=12;f<48;++f)assert(decoded[f*2]==500 && decoded[f*2+1]==500);
    xk_audio_voice_stop(gs->voice);xk_audio_stream_flush(gs->voice);
    c=context(global_handle,0,0,0);call(&c,0x37B818,0,2);assert(gs->headroom==0 && gv->volume==1.0f);
    c=context(global_handle,0,0,0);call(&c,0x37AB40,2,1);assert(device.references==2);
    c=context(global_handle,0,0,0);call(&c,0x37AB87,1,1);
    c=context(global_handle,0,0,0);call(&c,0x37AB87,0,1);assert(device.references==1 && !device.children);
    c=stream_description(dev,0);
    int ids[XA_MAX_VOICES];for(unsigned i=0;i<XA_MAX_VOICES;i++){ids[i]=xk_audio_voice_new(2,0x4FF9);assert(ids[i]>=0);}
    before=read32(0x6FFE);unsigned old_frees=frees;
    call(&c,0x37D4E2,0x8007000e,4);assert(read32(0x6FFE)==before&&frees==old_frees+1&&!device.children);
    c=global_stream_description();before=read32(0x6ffe);old_frees=frees;
    call(&c,0x37D835,0x8007000e,2);assert(read32(0x6ffe)==before && frees==old_frees+1 && !device.children);
    for(unsigned i=0;i<XA_MAX_VOICES;i++)xk_audio_voice_free(ids[i]);
    c=context(dev-8,0,0,0);call(&c,0x37C70F,0,1);assert(!healthy);
    for(unsigned i=0;i<256;i++)assert(!pool[i]);free(g_xram);free(g_xpt);
    puts("Halo 2 streams: real mono/stereo ADPCM/PCM mixer voices, split decode/gain, bounded ownership, ABI and rollback pass");return 0;
}
