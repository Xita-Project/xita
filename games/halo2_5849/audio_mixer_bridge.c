/* Same shared decoder, compiled only for the H2 diagnostic target. Keeping
 * the read-only cursor probe in this translation unit avoids exporting shared
 * mutable voice state or changing the CE mixer. */
#include "recomp/kernel/xk_audio.c"
#include "audio_stream_cursor.h"
#include "audio_mixer_bridge.h"
int h2_audio_stream_cursor_read(int voice, h2_stream_cursor *out)
{
    if (voice<0 || voice>=XA_MAX_VOICES || !out) return 0;
    xk_audio_lock();int valid=h2_stream_cursor_from_voice(&g_v[voice],out);xk_audio_unlock();
    return valid;
}
int h2_audio_movie_contract(int movie,const int muted[2],const int zero[4],int movie_playing,const uint8_t *allowed)
{
    if(movie<0 || movie>=XA_MAX_VOICES || !muted || !zero)return 0;
    /* Preserve the shared decoder's established50% master headroom. This
     * is an explicit software-output gain, not Xbox per-route attenuation. */
    xk_audio_lock();int ok=g_master_pct==50;unsigned seen[XA_MAX_VOICES]={0};seen[movie]=1;
    xa_voice *m=&g_v[movie];
    if(!m->used || m->kind!=1 || m->adpcm || m->channels!=2 || m->bits!=16 || m->rate!=44100 ||
       (m->freq_override && m->freq_override!=44100) || m->volume!=1.0f || m->size!=106496 ||
       !m->data || !!m->playing!=!!movie_playing)ok=0;
    for(unsigned i=0;i<6 && ok;++i){
        int id=i<2?muted[i]:zero[i-2];if(id<0 || id>=XA_MAX_VOICES || seen[id]){ok=0;break;}seen[id]=1;
        xa_voice *v=&g_v[id];
        if(i<2){
            /* Shared parser retains its48000 default below4000; the audited
             * SetFrequency1000 override supplies these voices' actual step. */
            if(!v->used || v->kind!=1 || v->adpcm || v->channels!=1 || v->bits!=8 || v->rate!=48000 ||
               v->freq_override!=1000 || v->volume!=0.0f || v->size!=1000 || !v->playing)ok=0;
        }else {h2_stream_cursor cursor;if(!h2_stream_cursor_from_voice(v,&cursor))ok=0;}
    }
    for(unsigned i=0;i<XA_MAX_VOICES && ok;++i)if(g_v[i].playing && !seen[i] && !(allowed && allowed[i]))ok=0;
    xk_audio_unlock();return ok;
}
