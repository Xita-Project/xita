/* Included by the production-extracting object worker test runner. */
#include "kernel/xk.h"
#include "kernel/xk_object_jobs.h"
#include "kernel/dsound_state.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
void object_test_audio_owner(void);
static unsigned callbacks, volume_writes;
static int32_t last_volume;
static void ds_state_log(const char *event,uint32_t obj,ds_state *s,int queued)
{ (void)event;(void)obj;(void)s;(void)queued;object_test_audio_owner(); }
#define XD3D_COUNT(name) object_test_audio_owner()
#define D3DLOG(...) ((void)0)
int xk_audio_available(void) { return 0; }
uint64_t xk_audio_last_mix_us(void) { return 0; }
int xk_audio_stream_pop_consumed(int voice) { (void)voice;abort(); }
int xk_audio_stream_push(int voice,uint32_t guest,uint32_t size)
{ assert(voice==0&&guest==0xC0000&&size==192000);object_test_audio_owner();return 0; }
xk_obj *xk_handle_get_type(uint32_t h,xk_objtype type) { (void)h;(void)type;abort(); }
void xk_signal_check(void) { abort(); }

void xk_audio_voice_set_volume_db100(int voice,int32_t volume)
{
    object_test_audio_owner();assert(voice==3);
    volume_writes++;last_volume=volume;
}

/* PRODUCTION_AUDIO */

static void completed(xctx *c)
{
    object_test_audio_owner();
    assert(X_M32(c->r[4])==0xDEAD0011u);
    assert(X_M32(c->r[4]+4)==0xABCD&&X_M32(c->r[4]+8)==0xBEEF&&X_M32(c->r[4]+12)==0);
    assert(X_M32(0xA0000)==4096&&X_M32(0xA0004)==0);
    if(getenv("OBJECT_AUDIO_NESTED_FAIL")) {
        X_PUSH32(0x12CCFu);
        xv_object_job_hle(c,0x1D665Cu,xv_hle_DirectSoundDoWork);abort();
    }
    uint32_t sp=c->r[4];
    X_PUSH32(0xA0008);X_PUSH32(1);X_PUSH32(0x28B35);
    xv_call(c,0x19384Fu);
    assert(c->r[4]==sp&&c->r[0]==0&&X_M32(0xA0008)==1);
    X_M32(0xA0100)=0xC0000;X_M32(0xA0104)=192000;
    X_M32(0xA0108)=0xA0010;X_M32(0xA010C)=0xA0014;X_M32(0xA0110)=0xD00D;
    X_PUSH32(0);X_PUSH32(0xA0100);X_PUSH32(1);X_PUSH32(0x289FD);
    xv_call(c,0x193884u);
    assert(c->r[4]==sp&&c->r[0]==0&&X_M32(0xA0014)==1);
    callbacks++;c->r[4]+=16;
}
const xv_fn_entry_t xv_fn_table[]={{0x29590u,completed}};
const unsigned xv_fn_table_count=1;
const xv_fn_entry_t xv_hle_table[]={{0,0}};
const unsigned xv_hle_table_count=0;
const xv_fn_entry_t xv_hle_extra[]={
    {0x19384Fu,xv_hle_CDirectSoundStream_GetStatus},
    {0x193884u,xv_hle_CDirectSoundStream_Process},{0,0}};
void object_test_audio_pump(xctx *c)
{
    object_test_audio_owner();
    memset(g_ds_streams,0,sizeof g_ds_streams);
    ds_stream *s=&g_ds_streams[0];
    s->obj=1;s->callback=0x29590;s->cb_context=0xABCD;s->max_pkts=4;
    s->bytes_per_sec=192000;s->nq=1;s->voice=0;
    s->q[0]=(ds_pkt){.completed_ptr=0xA0000,.status_ptr=0xA0004,.size=4096,.context=0xBEEF};
    X_M32(0xA0000)=0;X_M32(0xA0004)=1;
    unsigned before=callbacks,sp=c->r[4];
    xv_hle_DirectSoundDoWork(c);
    assert(c->r[4]==sp+4&&callbacks==before+1);
    assert(s->nq==1&&s->q[0].context==0xD00D&&X_M32(0xA0014)==1);
}

/* Exercise actual stream lookup and stdcall cleanup, including an unknown
 * stream. The wrapper prepares its fixture only after all workers park. */
void object_test_stream_volume(xctx *c)
{
    object_test_audio_owner();
    unsigned sp=c->r[4],before=volume_writes;
    uint32_t object=X_M32(sp+4);
    int32_t volume=(int32_t)X_M32(sp+8);
    memset(g_ds_streams,0,sizeof g_ds_streams);
    g_ds_streams[0].obj=0x1234;g_ds_streams[0].voice=3;
    xv_hle_CDirectSoundStream_SetVolume(c);
    assert(c->r[0]==0&&c->r[4]==sp+12);
    assert(volume_writes==before+(object==0x1234));
    if(object==0x1234)assert(last_volume==volume);
}

/* Compile and call the existing DS_OK handler rather than supplying a new
 * success stub in the worker bridge. Its current behavior must stay intact. */
void object_test_audio_commit(xctx *c)
{
    object_test_audio_owner();
    unsigned sp=c->r[4];
    uint32_t saved[8];memcpy(saved,c->r,sizeof saved);
    assert(X_M32(sp)==0x291EF&&X_M32(sp+4)==0x03D07280);
    xv_hle_IDirectSound_CommitDeferredSettings(c);
    assert(c->r[0]==0&&c->r[4]==sp+8);
    for(unsigned i=1;i<8;i++)if(i!=4)assert(c->r[i]==saved[i]);
    assert(X_M32(sp)==0x291EF&&X_M32(sp+4)==0x03D07280);
}
