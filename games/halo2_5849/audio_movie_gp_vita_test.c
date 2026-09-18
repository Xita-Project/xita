/* Actual decoder, GP and native worker; only platform and samples synthetic. */
#define H2_AUDIO_TEST_MOVIE 1
#define H2_GP_PCM_TEST_MAIN old_muted_gp_test
#include "audio_gp_pcm_vita_test.c"
static atomic_int stop_done;
static int stopping_voice;
static void *stop_movie(void *unused)
{(void)unused;assert(!h2_audio_backend_stop(stopping_voice));atomic_store(&stop_done,1);return NULL;}
int main(void)
{
    g_xram=calloc(1,0x40000);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);assert(g_xram&&g_xpt);
    for(unsigned p=0;p<1u<<20;++p)g_xpt[p]=0x3f000;
    for(unsigned p=1;p<0x3f;++p)g_xpt[p]=p*4096;
    h2_dsp_engine *s=open_complete_fx();
    for(unsigned i=0;i<2;++i){assert(create_pcm(i)==(int)i);assert(!h2_audio_backend_gp_pcm_play(i));}
    const uint8_t sw[18]={1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,0,0};x_guest_write(0x1100,sw,18);
    int zero[4];for(unsigned i=0;i<4;++i){zero[i]=xk_audio_voice_new(2,0x1100);xk_audio_lock();xk_audio_voice_set_volume_db100(zero[i],0);xk_audio_unlock();
        for(unsigned p=0;p<2;++p){uint64_t ticket;assert(!h2_audio_backend_stream_submit(zero[i],0x9000+(i*2+p)*320,&ticket));}}
    const uint8_t w[18]={1,0,2,0,0x44,0xac,0,0,0x10,0xb1,2,0,4,0,16,0,0,0};x_guest_write(0x1180,w,18);
    int movie=xk_audio_voice_new(1,0x1180);assert(movie==6);int16_t samples[106496/2];
    for(unsigned i=0;i<106496/4;++i){samples[i*2]=1000;samples[i*2+1]=-2000;}x_guest_write(0x10000,samples,sizeof samples);
    xk_audio_voice_set_data(movie,0x10000,sizeof samples);xk_audio_lock();xk_audio_voice_set_volume_db100(movie,0);xk_audio_unlock();
    int muted[2]={0,1};assert(h2_audio_movie_contract(movie,muted,zero,0,NULL));
    xk_audio_lock();g_v[movie].channels=1;xk_audio_unlock();assert(h2_audio_backend_play(movie,106496,44100)<0);
    xk_audio_lock();g_v[movie].channels=2;xk_audio_unlock();
    atomic_store(&faults,1u<<F_HOLD);h2_audio_backend_status status;
    for(;;){h2_audio_backend_snapshot(&status);if(status.fx_computed_frames>status.fx_submitted_frames)break;usleep(1000);}
    assert(!h2_audio_backend_play(movie,106496,44100));uint32_t play,write;
    assert(!h2_audio_backend_cursor(movie,&play,&write)&&!play&&!write);
    assert(!h2_audio_backend_repeat_play(movie,106496,44100));
    assert(h2_audio_backend_repeat_play(movie,106492,44100)<0 && h2_audio_backend_repeat_play(movie,106496,48000)<0);
    assert(!h2_audio_backend_cursor(movie,&play,&write)&&!play&&!write);
    /* An already prepared pre-Play GP grain remains uncredited. */
    atomic_store(&faults,0);
    for(;;){h2_audio_backend_snapshot(&status);if(status.movie_consumed_frames>=2048)break;usleep(1000);}
    assert(status.last_peak_left==2453 && status.last_peak_right==953 && !status.error);
    assert(!h2_audio_backend_cursor(movie,&play,&write)&&play&&write);
    sceKernelLockMutex(progress_mutex,1,NULL);uint32_t left[32],right[32],center[32];
    h2_test_fx_input(s,0,left);h2_test_fx_input(s,1,right);h2_test_fx_input(s,2,center);
    for(unsigned i=0;i<32;++i){assert(left[i]==628000);assert(right[i]==244000);assert(center[i]==500000);}
    xk_audio_lock();assert(g_v[movie].frames_out>=progress.completed_frames);xk_audio_unlock();
    sceKernelUnlockMutex(progress_mutex,1);
    /* Hold one queued plus one computed movie grain. Stop cannot report
     * completion, forget or rewind until both ownership stages retire. */
    atomic_store(&faults,1u<<F_HOLD);
    for(;;){sceKernelLockMutex(progress_mutex,1,NULL);int prepared=movie_prepared==movie;
        sceKernelUnlockMutex(progress_mutex,1);if(prepared)break;usleep(1000);}
    stopping_voice=movie;pthread_t stopper;assert(!pthread_create(&stopper,NULL,stop_movie,NULL));
    for(;;){sceKernelLockMutex(progress_mutex,1,NULL);int draining=movie_draining;
        sceKernelUnlockMutex(progress_mutex,1);if(draining)break;usleep(1000);}
    assert(!atomic_load(&stop_done) && h2_audio_backend_rewind(movie)<0 && h2_audio_backend_forget(movie)<0);
    atomic_store(&faults,0);pthread_join(stopper,NULL);assert(atomic_load(&stop_done));
    uint32_t state;assert(!h2_audio_backend_status_voice(movie,&state) && !state);
    assert(!h2_audio_backend_cursor(movie,&play,&write) && play==write);
    assert(!h2_audio_backend_stop(movie));
    assert(!h2_audio_backend_rewind(movie));assert(!h2_audio_backend_cursor(movie,&play,&write) && !play && !write);
    assert(!h2_audio_backend_play(movie,106496,44100));
    for(;;){h2_audio_backend_snapshot(&status);if(status.movie_consumed_frames>=2048)break;usleep(1000);}
    /* A second unregistered active voice is rejected before rendering it. */
    int extra=xk_audio_voice_new(1,0x1180);xk_audio_voice_set_data(extra,0x10000,sizeof samples);xk_audio_voice_play(extra,1);
    for(;;){h2_audio_backend_snapshot(&status);if(status.error)break;usleep(1000);}assert(status.error==(uint32_t)-1007);
    xk_audio_voice_stop(extra);
    assert(!h2_audio_backend_close() && !g_v[movie].playing && !resources && !atomic_load(&queued));
    h2_dsp_destroy(s);free(g_xram);free(g_xpt);
    puts("Halo 2 movie GP: actual PCM resampling, independent stereo GP input, retained FX, sink-tagged cursors, inventory rejection and close pass");return 0;
}
