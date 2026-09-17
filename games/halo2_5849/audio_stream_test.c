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
static xctx muted_stream_description(void)
{
    xctx c=global_stream_description();uint32_t count=5;
    uint32_t pairs[10]={27,(uint32_t)-10000,28,(uint32_t)-10000,29,(uint32_t)-10000,30,(uint32_t)-10000,2,(uint32_t)-10000};
    x_guest_write(0x8ffd,&count,4);x_guest_write(0xaffe,pairs,sizeof pairs);return c;
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
static void empty_stream_work(h2_audio_stream *s,int allowed)
{
    g_xpt[0x387]=0x1e0000;X_M32(0x387198)=0;
    xctx c=context(0,0,0,0);c.fs_base=0x7000;X_M8(c.fs_base+0x24)=0;X_M32(c.r[4])=0x21EC3C;
    xctx saved=c;h2_audio_stream state=*s;h2_audio_device_snapshot owner=device;uint32_t fp=native_fp;
    uint8_t *memory=malloc(0x200000);assert(memory);memcpy(memory,g_xram,0x200000);
    if(!setjmp(stopped)){h2_audio_guest_entry(&c,0x37B844);assert(allowed);}else assert(!allowed);
    assert(!memcmp(&c,&saved,sizeof c)&&!memcmp(s,&state,sizeof state)&&!memcmp(&device,&owner,sizeof owner));
    assert(fp==native_fp&&!memcmp(memory,g_xram,0x200000));free(memory);
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
        empty_stream_work(s,1);
        v->playing=1;empty_stream_work(s,0);v->playing=0;
        s->submitted=1;empty_stream_work(s,0);s->submitted=0;
        direct_codec_fixture(s,kind,500); /* fixed-point mixer rounds 600 dB100 to 128/256 */
        c=context(handle,0,0,0);call(&c,0x37B818,0,2);assert(s->headroom==0&&v->volume==1.0f);
        direct_codec_fixture(s,kind,1000);
        c=context(handle,1,0,0);reject_stream(&c,0x37B818);
        /* IDirectSoundStream::Pause (0x37B822 -> voice Pause 0x380074) on the
         * empty voice: only the pause/synch/deferred bits change, exactly as
         * the original rewrites them; nothing is queued or started. */
        for(unsigned mode=0;mode<4;++mode){c=context(handle,mode,0,0);X_M32(c.r[4])=0x21F8E4;reject_stream(&c,0x37B822);}
        c=context(handle,4,0,0);X_M32(c.r[4])=0x21F8E3;reject_stream(&c,0x37B822);
        s->submitted=1;c=context(handle,0,0,0);X_M32(c.r[4])=0x21F8E3;reject_stream(&c,0x37B822);s->submitted=0;
        c=context(handle,1,0,0);X_M32(c.r[4])=0x21F63D;call(&c,0x37B822,0,2);assert(s->pause==4);
        c=context(handle,2,0,0);X_M32(c.r[4])=0x21F461;call(&c,0x37B822,0,2);assert(s->pause==0x40);
        c=context(handle,3,0,0);X_M32(c.r[4])=0x21F8E3;call(&c,0x37B822,0,2);assert(s->pause==0x60);
        c=context(handle,0,0,0);X_M32(c.r[4])=0x2AE4D8;call(&c,0x37B822,0,2);assert(!s->pause);
        c=context(handle,3,0,0);X_M32(c.r[4])=0x21F8E3;call(&c,0x37B822,0,2);assert(s->pause==0x20);
        c=context(handle,1,0,0);X_M32(c.r[4])=0x21F63D;call(&c,0x37B822,0,2);assert(s->pause==0x24);
        c=context(handle,0,0,0);X_M32(c.r[4])=0x21F8E3;call(&c,0x37B822,0,2);
        assert(!s->pause&&!v->playing&&!v->nq&&!s->headroom&&v->volume==1.0f);
        /* Level-start setters on the empty voice: each records what the original
         * packs into the voice image (0x37A5D4/0x37A5BB/0x38144F/0x38157E/0x381710/
         * 0x37A4DB/0x37BC89/0x37BD54); nothing runs and nothing is queued. */
        c=context(handle,(uint32_t)-1300,0,0);X_M32(c.r[4])=0x21FC0F;reject_stream(&c,0x37B7FF);
        c=context(handle,1,0,0);X_M32(c.r[4])=0x21FC0E;reject_stream(&c,0x37B7FF);
        c=context(handle,(uint32_t)-10001,0,0);X_M32(c.r[4])=0x21FC0E;reject_stream(&c,0x37B7FF);
        c=context(handle,(uint32_t)-1300,0,0);X_M32(c.r[4])=0x21FC0E;call(&c,0x37B7FF,0,2);
        assert(s->volume==-1300&&v->volume>0.2f&&v->volume<0.25f&&!v->playing);
        c=context(handle,0,0,0);X_M32(c.r[4])=0x21FC0E;call(&c,0x37B7FF,0,2);assert(!s->volume&&v->volume==1.0f);
        c=context(handle,(uint32_t)-501,0,0);X_M32(c.r[4])=0x2201CA;reject_stream(&c,0x37B804);
        c=context(handle,(uint32_t)-4097,0,0);X_M32(c.r[4])=0x2201C9;reject_stream(&c,0x37B804);
        c=context(handle,4096,0,0);X_M32(c.r[4])=0x2201C9;reject_stream(&c,0x37B804);
        c=context(handle,(uint32_t)-501,0,0);X_M32(c.r[4])=0x2201C9;call(&c,0x37B804,0,2);assert(s->pitch==-501&&v->rate==44100);
        c=context(handle,4095,0,0);X_M32(c.r[4])=0x2201C9;call(&c,0x37B804,0,2);assert(s->pitch==4095);
        c=context(handle,0,0,0);X_M32(c.r[4])=0x2201C9;call(&c,0x37B804,0,2);assert(!s->pitch);
        {
            uint32_t lfo[6]={1,100,50,(uint32_t)-5,7,(uint32_t)-128};x_guest_write(0x9F00,lfo,sizeof lfo);
            c=context(handle,0x9F00,0,0);X_M32(c.r[4])=0x22050E;reject_stream(&c,0x37B809);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x22050D;reject_stream(&c,0x37B809);
            static const uint32_t lfo_bad[6]={2,0x8000,0x400,128,(uint32_t)-129,0x80};
            for(unsigned f=0;f<6;++f){uint32_t w[6];memcpy(w,lfo,sizeof w);w[f]=lfo_bad[f];x_guest_write(0x9F00,w,sizeof w);
                c=context(handle,0x9F00,0,0);X_M32(c.r[4])=0x22050D;reject_stream(&c,0x37B809);}
            x_guest_write(0x9F00,lfo,sizeof lfo);
            static const uint32_t lfo_callers[4]={0x22050D,0x220664,0x2206D5,0x220709};
            for(unsigned i=0;i<4;++i){c=context(handle,0x9F00,0,0);X_M32(c.r[4])=lfo_callers[i];call(&c,0x37B809,0,2);}
            assert(!memcmp(s->lfo[1],lfo,sizeof lfo)&&!s->lfo[0][0]&&!s->lfo[0][1]);
            uint32_t eg[10]={1,2,0,441,0,0,940,0xFF,0,0};x_guest_write(0x9F40,eg,sizeof eg);
            c=context(handle,0x9F40,0,0);X_M32(c.r[4])=0x2AE818;reject_stream(&c,0x37B80E);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x2AE817;reject_stream(&c,0x37B80E);
            static const uint32_t eg_bad[10]={2,16,0x1000,0x1000,0x1000,0x1000,0x1000,0x100,128,(uint32_t)-129};
            for(unsigned f=0;f<10;++f){uint32_t w[10];memcpy(w,eg,sizeof w);w[f]=eg_bad[f];x_guest_write(0x9F40,w,sizeof w);
                c=context(handle,0x9F40,0,0);X_M32(c.r[4])=0x2AE817;reject_stream(&c,0x37B80E);}
            x_guest_write(0x9F40,eg,sizeof eg);
            c=context(handle,0x9F40,0,0);X_M32(c.r[4])=0x2AE817;call(&c,0x37B80E,0,2);
            assert(!memcmp(s->eg[1],eg,sizeof eg)&&!s->eg[0][0]);
            uint32_t filter[6]={1,0,0,0x8000,0,0};x_guest_write(0x9F80,filter,sizeof filter);
            c=context(handle,0x9F80,0,0);X_M32(c.r[4])=0x2203EF;reject_stream(&c,0x37B813);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x22069D;reject_stream(&c,0x37B813);
            static const uint32_t filter_bad[6]={4,0x10000,0x10000,0x10000,0x10000,0x10000};
            for(unsigned f=0;f<6;++f){uint32_t w[6];memcpy(w,filter,sizeof w);w[f]=filter_bad[f];x_guest_write(0x9F80,w,sizeof w);
                c=context(handle,0x9F80,0,0);X_M32(c.r[4])=0x22069D;reject_stream(&c,0x37B813);}
            x_guest_write(0x9F80,filter,sizeof filter);
            c=context(handle,0x9F80,0,0);X_M32(c.r[4])=0x2203EE;call(&c,0x37B813,0,2);
            c=context(handle,0x9F80,0,0);X_M32(c.r[4])=0x22069D;call(&c,0x37B813,0,2);
            assert(!memcmp(s->filter,filter,sizeof filter));
            /* Mix bins: SetMixBinVolumes rewrites only the listed gains; SetMixBins
             * replaces the route list, a NULL list restoring the format default. */
            uint32_t list[2]={3,0x9FC0},pairs[6]={6,(uint32_t)-1200,8,0,7,(uint32_t)-10000};
            x_guest_write(0x9FA0,list,sizeof list);x_guest_write(0x9FC0,pairs,sizeof pairs);
            assert(s->route_count==2&&!s->route_bins[0]&&s->route_bins[1]==1);
            c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x2216B8;reject_stream(&c,0x37B81D);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x2216B7;reject_stream(&c,0x37B81D);
            c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x2216B7;call(&c,0x37B81D,0,2);
            assert(s->bin_gain[6]==-1200&&!s->bin_gain[8]&&s->bin_gain[7]==-10000&&s->route_count==2);
            c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x22013C;reject_stream(&c,0x37C70A);
            c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x22013B;call(&c,0x37C70A,0,2);
            assert(s->route_count==3&&s->route_bins[0]==6&&s->route_bins[1]==8&&s->route_bins[2]==7&&s->route_gains[0]==-1200&&s->route_gains[2]==-10000);
            static const uint32_t bad_pairs[3][2]={{32,0},{6,1},{6,(uint32_t)-10001}};
            for(unsigned i=0;i<3;++i){uint32_t w[6];memcpy(w,pairs,sizeof w);w[0]=bad_pairs[i][0];w[1]=bad_pairs[i][1];x_guest_write(0x9FC0,w,sizeof w);
                c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x22013B;reject_stream(&c,0x37C70A);
                c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x2216B7;reject_stream(&c,0x37B81D);}
            x_guest_write(0x9FC0,pairs,sizeof pairs);
            list[0]=9;x_guest_write(0x9FA0,list,sizeof list);
            c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x22013B;reject_stream(&c,0x37C70A);
            list[0]=0;x_guest_write(0x9FA0,list,sizeof list);
            c=context(handle,0x9FA0,0,0);X_M32(c.r[4])=0x22013B;reject_stream(&c,0x37C70A);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x2216DC;call(&c,0x37C70A,0,2);
            assert(s->route_count==2&&!s->route_bins[0]&&s->route_bins[1]==1&&!s->route_gains[0]&&!s->route_gains[1]);
            /* PauseEx(0,0,3), Flush and GetStatus act only on a started voice. */
            c=context(handle,0,0,3);X_M32(c.r[4])=0x2AE4E8;reject_stream(&c,0x37B827);
            c=context(handle,1,0,3);X_M32(c.r[4])=0x2AE4E7;reject_stream(&c,0x37B827);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x2AE4E7;reject_stream(&c,0x37B827);
            c=context(handle,0,0,3);X_M32(c.r[4])=0x2AE4E7;call(&c,0x37B827,0,4);assert(!s->pause);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x21EC86;reject_stream(&c,0x37AC89);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x21EC85;call(&c,0x37AC89,0,1);
            c=context(handle,0x9FE0,0,0);X_M32(c.r[4])=0x2AE857;reject_stream(&c,0x37ACD4);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x2AE856;reject_stream(&c,0x37ACD4);
            uint32_t status=0xcccccccc;x_guest_write(0x9FE0,&status,4);
            c=context(handle,0x9FE0,0,0);X_M32(c.r[4])=0x2AE856;call(&c,0x37ACD4,0,2);assert(!read32(0x9FE0));
            c=context(handle,3,0,0);X_M32(c.r[4])=0x21F8E3;call(&c,0x37B822,0,2);
            c=context(handle,0x9FE0,0,0);X_M32(c.r[4])=0x2AE856;call(&c,0x37ACD4,0,2);assert(read32(0x9FE0)==0x80000);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x21F8E3;call(&c,0x37B822,0,2);
            c=context(handle,0x9FE0,0,0);X_M32(c.r[4])=0x21FB3E;reject_stream(&c,0x37B83F);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x21FB3D;reject_stream(&c,0x37B83F);
            status=0x12345678;x_guest_write(0x9FE0,&status,4);
            c=context(handle,0x9FE0,0,0);X_M32(c.r[4])=0x21FB3D;call(&c,0x37B83F,0x88780032,2);assert(read32(0x9FE0)==0x12345678);
            /* SetOutputBuffer routes the voice into an inactive 3D bus's input;
             * NULL detaches back to the default bins. */
            uint32_t bus_desc[6]={24,0x2010,0,0,0,0};x_guest_write(0x9E00,bus_desc,sizeof bus_desc);
            c=context(dev,0x9E00,0x9E40,0);X_M32(c.r[4])=0x220AC8;call(&c,0x37D4BE,0,4);uint32_t bus=read32(0x9E40);
            c=context(handle,bus,0,0);X_M32(c.r[4])=0x21F917;reject_stream(&c,0x37C705);
            c=context(handle,bus+4,0,0);X_M32(c.r[4])=0x21F916;reject_stream(&c,0x37C705);
            c=context(handle,handle,0,0);X_M32(c.r[4])=0x21F916;reject_stream(&c,0x37C705);
            c=context(handle,bus,0,0);X_M32(c.r[4])=0x21F916;call(&c,0x37C705,0,2);
            assert(s->output==bus-0x1c&&s->route_count==1&&s->route_bins[0]==31&&!s->route_gains[0]);
            c=context(handle,0,0,0);X_M32(c.r[4])=0x21F928;call(&c,0x37C705,0,2);
            assert(!s->output&&s->route_count==2&&!s->route_bins[0]&&s->route_bins[1]==1);
            c=context(bus,0,0,0);call(&c,0x379F45,0,1);assert(!find_buffer(bus-0x1c));
        }
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
    c=context(global_handle,0,0,0);X_M32(c.r[4])=0x21F8E3;reject_stream(&c,0x37B822); /* global voices carry data */
    {
        static const uint32_t game_only[11]={0x37B7FF,0x37B804,0x37B809,0x37B80E,0x37B813,0x37B81D,0x37C70A,0x37C705,0x37B827,0x37AC89,0x37ACD4};
        for(unsigned i=0;i<11;++i){c=context(global_handle,0x9F00,0,3);X_M32(c.r[4])=0x21F8E3;reject_stream(&c,game_only[i]);}
    }
    c=context(global_handle,0,0,0);call(&c,0x37AB40,2,1);assert(device.references==2);
    c=context(global_handle,0,0,0);call(&c,0x37AB87,1,1);
    c=context(global_handle,0,0,0);call(&c,0x37AB87,0,1);assert(device.references==1 && !device.children);
    for(unsigned bin=28;bin<=30;++bin){
        c=global_stream_description();x_guest_write(0xaffe,&bin,4);call(&c,0x37D835,0,2);
        uint32_t h=read32(0x6ffe);h2_audio_stream *s=stream_live(&c,0x37D835,h);assert(s->route_bin==bin);
        c=context(h,0,0,0);call(&c,0x37AB87,0,1);
    }
    c=global_stream_description();uint32_t bad_bin=31;x_guest_write(0xaffe,&bad_bin,4);reject_stream(&c,0x37D835);
    for(unsigned i=0;i<10;++i){
        c=muted_stream_description();uint32_t value=read32(0xaffe + i*4)^1;
        x_guest_write(0xaffe + i*4,&value,4);reject_stream(&c,0x37D835);
    }
    c=muted_stream_description();call(&c,0x37D835,0,2);uint32_t muted=read32(0x6ffe);
    h2_audio_stream *ms=stream_live(&c,0x37D835,muted);xa_voice *mv=g_v+ms->voice;
    assert(ms->route_count==5 && ms->route_bin==UINT32_MAX && !mv->volume && !mv->playing && !mv->nq);
    for(unsigned i=0;i<5;++i)assert(ms->route_bins[i]==(i<4?27+i:2) && ms->route_gains[i]==-10000);
    c=context(muted,0,0,0);call(&c,0x37B818,0,2);assert(!ms->headroom && !mv->volume);
    c=context(muted,0,0,0);X_M32(c.r[4])=0x21F8E3;reject_stream(&c,0x37B822);
    /* Direct fixture feed verifies genuine gain zero, not silent output data. */
    x_guest_write(0xc000,samples,sizeof samples);assert(!xk_audio_stream_push(ms->voice,0xc000,sizeof samples));
    xk_audio_mix(decoded,48);for(unsigned i=0;i<96;++i)assert(!decoded[i]);
    xk_audio_voice_stop(ms->voice);xk_audio_stream_flush(ms->voice);
    c=context(muted,0,0,0);call(&c,0x37AB87,0,1);assert(device.references==1 && !device.children);
    c=muted_stream_description();uint32_t alt=0x335d99;x_guest_write(0x3ffd+12,&alt,4);reject_stream(&c,0x37D835);
    for(unsigned bin=27;bin<=30;++bin){
        c=global_stream_description();x_guest_write(0xaffe,&bin,4);x_guest_write(0x3ffd+12,&alt,4);call(&c,0x37D835,0,2);
        uint32_t h=read32(0x6ffe);h2_audio_stream *s=stream_live(&c,0x37D835,h);
        assert(s->callback==alt && s->route_count==1 && s->route_bin==bin && !g_v[s->voice].playing && !g_v[s->voice].nq);
        c=context(h,0,0,0);call(&c,0x37B818,0,2);assert(g_v[s->voice].volume==1.0f);
        c=context(h,0,0,0);call(&c,0x37AB87,0,1);
    }
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
