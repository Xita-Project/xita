/* Actual shared PCM decoder + GP + concurrent worker, synthetic game data. */
#define H2_AUDIO_DSP 1
#define H2_AUDIO_TEST_REAL_MIXER 1
#define main unused_platform_tests
#include "audio_vita_test.c"
#undef main
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
h2_dsp_engine *h2_test_fx_engine(void);
void h2_test_fx_input(h2_dsp_engine *,unsigned,uint32_t [32]);
static atomic_int pcm_play_done;
static void *play_first(void *unused)
{
    (void)unused; assert(h2_audio_backend_gp_pcm_play(0)==0);
    atomic_store(&pcm_play_done,1); return NULL;
}
static int create_pcm(unsigned index)
{
    uint32_t format=0x1000+index*64,data=0x2000+index*4096;
    const uint8_t wfx[18]={1,0,1,0,0xe8,3,0,0,0xe8,3,0,0,1,0,8,0,0,0};
    x_guest_write(format,wfx,18);
    uint8_t samples[1000];for(unsigned i=0;i<1000;++i)samples[i]=(uint8_t)(i+index*73);
    x_guest_write(data,samples,1000);
    int voice=xk_audio_voice_new(1,format);assert(voice==(int)index);
    xk_audio_voice_set_data(voice,data,1000);
    xk_audio_lock();xk_audio_voice_set_frequency(voice,1000);xk_audio_voice_set_volume_db100(voice,-10000);xk_audio_unlock();
    return voice;
}
static h2_dsp_engine *open_complete_fx(void)
{
    assert(h2_audio_backend_open()==0);
    for(unsigned b=0;b<11;++b)assert(h2_audio_backend_set_headroom(b,0)==0);
    h2_dsp_engine *s=h2_test_fx_engine();int8_t taps[31]={127};
    assert(h2_audio_backend_fx_bind(s,13)==0 && h2_audio_backend_fx_route(13,6)==0 && h2_audio_backend_fx_play(13)==0);
    for(unsigned b=23;b<=25;++b){
        assert(h2_audio_backend_fx_bind(s,b)==0 && h2_audio_backend_fx_play(b)==0);
        assert(h2_audio_backend_fx_bind_spatial(s,b,taps)==0 && h2_audio_backend_fx_play(0x10000u|b)==0);
    }
    assert(h2_audio_backend_fx_route_mask(23,64)==0 && h2_audio_backend_fx_mute(H2_FX_SPATIAL23)==0);
    assert(h2_audio_backend_fx_route_mask(24,128)==0 && h2_audio_backend_fx_mute(H2_FX_SPATIAL24)==0);
    assert(h2_audio_backend_fx_route_mask(H2_FX_SPATIAL25,1024)==0 && h2_audio_backend_fx_mute(25)==0);
    assert(h2_audio_backend_fx_filter(23)==0 && h2_audio_backend_fx_filter(24)==0);
    for(unsigned b=15;b<=22;++b){
        assert(h2_audio_backend_fx_bind(s,b)==0 && h2_audio_backend_fx_route_mask(b,1u<<(6+(b-15)%4))==0);
        assert(h2_audio_backend_fx_play(b)==0);
    }
    return s;
}
int main(void)
{
    g_xram=calloc(1,0x10000);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);assert(g_xram && g_xpt);
    for(unsigned p=0;p<1u<<20;++p)g_xpt[p]=0xf000;
    for(unsigned p=1;p<15;++p)g_xpt[p]=p*4096;
    h2_dsp_engine *s=open_complete_fx();h2_audio_backend_status status;
    assert(create_pcm(0)==0);
    assert(h2_audio_backend_gp_pcm_play(-1)<0 && h2_audio_backend_gp_pcm_play(XA_MAX_VOICES)<0);
    atomic_store(&faults,1u<<F_HOLD);
    for(;;){h2_audio_backend_snapshot(&status);if(status.fx_computed_frames>status.fx_submitted_frames)break;usleep(1000);}
    pthread_t thread;assert(!pthread_create(&thread,NULL,play_first,NULL));
    for(;;){h2_audio_backend_snapshot(&status);if(status.gp_pcm_playing_mask==1)break;usleep(1000);}
    assert(!status.gp_pcm_submitted[0] && !atomic_load(&pcm_play_done));
    atomic_store(&faults,0);pthread_join(thread,NULL);assert(atomic_load(&pcm_play_done));
    assert(create_pcm(1)==1);assert(h2_audio_backend_gp_pcm_play(0)<0 && h2_audio_backend_gp_pcm_play(1)==0);
    for(;;){h2_audio_backend_snapshot(&status);if(status.gp_pcm_consumed[1]>=1024)break;usleep(1000);}
    assert(status.gp_pcm_playing_mask==3 && status.gp_pcm_submitted[0]>status.gp_pcm_submitted[1]);
    assert(status.last_peak_left==1953 && !status.error); /* real GP output retained */
    sceKernelLockMutex(progress_mutex,1,NULL);
    uint32_t routed[32];h2_test_fx_input(s,14,routed);for(unsigned i=0;i<32;++i)assert(!routed[i]);
    xk_audio_lock();
    for(unsigned i=0;i<2;++i)assert(g_v[i].frames_out>=gp_pcm_submitted[i] && g_v[i].frames_out<=gp_pcm_submitted[i]+1024);
    for(unsigned i=0;i<2;++i)assert(g_v[i].frames_out>=1024 && g_v[i].pos && g_v[i].volume==0.0f);
    xk_audio_unlock();sceKernelUnlockMutex(progress_mutex,1);
    /* Deliberately violate the host adapter's mute contract. Nonzero PCM
     * must stop before it can be discarded or submitted as another route. */
    xk_audio_lock();xk_audio_voice_set_volume_db100(0,0);xk_audio_unlock();
    for(;;){h2_audio_backend_snapshot(&status);if(status.error)break;usleep(1000);}
    assert(status.error==(uint32_t)-1007 && status.last_peak_left==1953);
    assert(h2_audio_backend_close()==0 && !resources && !atomic_load(&queued));
    h2_audio_backend_snapshot(&status);
    for(unsigned i=0;i<2;++i)assert(status.gp_pcm_consumed[i]==status.gp_pcm_submitted[i] && !g_v[i].playing);
    assert(!status.gp_pcm_playing_mask);h2_dsp_destroy(s);free(g_xram);free(g_xpt);
    puts("Halo 2 muted PCM/GP: actual decoder time, two owners, retained grains, sink consumption, strict nonzero rejection and drain pass");return 0;
}
