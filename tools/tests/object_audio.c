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
static unsigned frequency_writes,last_frequency;
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
void xk_audio_voice_set_frequency(int voice,uint32_t frequency)
{
    object_test_audio_owner();assert(voice==3);
    frequency_writes++;last_frequency=frequency;
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
    uint32_t back=X_M32(sp);
    assert((back==0x291EF||back==0x28BB6)&&X_M32(sp+4)==0x03D07280);
    xv_hle_IDirectSound_CommitDeferredSettings(c);
    assert(c->r[0]==0&&c->r[4]==sp+8);
    for(unsigned i=1;i<8;i++)if(i!=4)assert(c->r[i]==saved[i]);
    assert(X_M32(sp)==back&&X_M32(sp+4)==0x03D07280);
}

/* Initial refill calls can also complete an old packet and reenter both stream
 * methods through Halo's original completion callback. Prepare the fixture only
 * on the parked owner, then invoke the actual GetStatus/Process implementations. */
static void prepare_refill(void)
{
    object_test_audio_owner();memset(g_ds_streams,0,sizeof g_ds_streams);
    ds_stream *s=&g_ds_streams[0];
    s->obj=1;s->callback=0x29590;s->cb_context=0xABCD;s->max_pkts=4;
    s->bytes_per_sec=192000;s->nq=1;s->voice=0;
    s->q[0]=(ds_pkt){.completed_ptr=0xA0000,.status_ptr=0xA0004,.size=4096,.context=0xBEEF};
    X_M32(0xA0000)=0;X_M32(0xA0004)=1;
}
void object_test_stream_status(xctx *c)
{
#ifdef XV_OBJECT_HOLD_PROFILE
    extern unsigned xv_object_motion_begin(xctx *,unsigned);
    assert(!xv_object_motion_begin(c,5)); /* owner borrowing a marked context */
#endif
    unsigned sp=c->r[4],obj=X_M32(sp+4),before=callbacks;
    prepare_refill();xv_hle_CDirectSoundStream_GetStatus(c);
    assert(c->r[0]==0&&c->r[4]==sp+12&&callbacks==before+(obj==1));
    if(obj==1)assert(g_ds_streams[0].nq==1);
}
void object_test_stream_packet(xctx *c)
{
    unsigned sp=c->r[4],obj=X_M32(sp+4),before=callbacks;
    prepare_refill();xv_hle_CDirectSoundStream_Process(c);
    assert(c->r[0]==0&&c->r[4]==sp+16&&callbacks==before+(obj==1));
    if(obj==1)assert(g_ds_streams[0].nq==2&&g_ds_streams[0].q[1].context==0xABAB);
}

/* Real lookup/stop code, including native wrappers and empty/unknown handles.
 * No fixture state is touched until the owner has parked every worker. */
void object_test_voice_stop(xctx *c)
{
    object_test_audio_owner();
    unsigned sp=c->r[4],before=callbacks;
    uint32_t saved[8];memcpy(saved,c->r,sizeof saved);
    uint32_t object=X_M32(sp+4);
    assert(X_M32(sp)==0x28745);
    memset(g_ds_buffers,0,sizeof g_ds_buffers);
    memset(g_ds_streams,0,sizeof g_ds_streams);
    ds_buffer *b=&g_ds_buffers[0];b->obj=0xB1000;
    ds_stream *s=&g_ds_streams[0];s->obj=0xB2000;s->nq=2;
    uint64_t now=xk_os_monotonic_us();
    b->state=(ds_state){.start_us=now,.end_us=now+1000000,.duration_us=1000000};
    s->state=b->state;
    s->q[0]=(ds_pkt){.report_due_us=now+1000000,.due_us=now+2000000,.context=0xABCD};
    s->q[1]=s->q[0];
    X_M32(0xB3000+0x24)=b->obj;X_M32(0xB4000+0x24)=s->obj;
    X_M32(0xB5000+0x24)=0;X_M32(0xB6000+0x24)=0xB8000;
    X_M32(0xB7000+0x24)=0;
    xv_hle_DSoundVoiceStop(c);
    assert(c->r[0]==0&&c->r[4]==sp+8&&callbacks==before);
    for(unsigned i=1;i<8;i++)if(i!=4)assert(c->r[i]==saved[i]);
    assert(X_M32(sp)==0x28745&&X_M32(sp+4)==object);
    assert(b->state.stopped==(object==0xB1000||object==0xB3000));
    int stream=object==0xB2000||object==0xB4000;
    assert(s->state.stopped==stream&&s->nq==2);
    for(unsigned i=0;i<2;i++) {
        assert(s->q[i].report_due_us==(stream?0:now+1000000));
        assert(s->q[i].due_us==now+2000000&&s->q[i].context==0xABCD);
    }
}

void object_test_stream_frequency(xctx *c)
{
    object_test_audio_owner();
    unsigned sp=c->r[4],before=frequency_writes,cb=callbacks;
    uint32_t saved[8];memcpy(saved,c->r,sizeof saved);
    uint32_t object=X_M32(sp+4),frequency=X_M32(sp+8);
    assert(X_M32(sp)==0x29898);
    memset(g_ds_streams,0,sizeof g_ds_streams);
    ds_stream *s=&g_ds_streams[0];s->obj=0x1234;s->voice=3;s->nq=2;
    uint64_t now=xk_os_monotonic_us();
    s->state=(ds_state){.rate=48000,.align=4,.size=192000,.start_us=now,
        .end_us=now+1000000,.duration_us=1000000};
    s->q[0]=(ds_pkt){.report_due_us=now+500000,.due_us=now+750000,.context=0xABCD};
    s->q[1]=(ds_pkt){.report_due_us=now-1,.due_us=now-1,.context=0xDCBA};
    ds_stream original=*s;
    xv_hle_CDirectSoundStream_SetFrequency(c);
    uint64_t after=xk_os_monotonic_us();
    assert(c->r[0]==0&&c->r[4]==sp+12&&callbacks==cb);
    for(unsigned i=1;i<8;i++)if(i!=4)assert(c->r[i]==saved[i]);
    assert(X_M32(sp)==0x29898&&X_M32(sp+4)==object&&X_M32(sp+8)==frequency);
    assert(frequency_writes==before+(object==0x1234));
    if(object!=0x1234) {assert(!memcmp(s,&original,sizeof *s));return;}
    unsigned valid=frequency>=100&&frequency<=192000;
    unsigned rate=valid?frequency:48000;
    assert(last_frequency==frequency&&s->state.frequency==(valid?frequency:0));
    assert(s->state.duration_us==(192000ull*1000000+4*rate-1)/(4*rate));
    /* Bound the retimed deadline using timestamps around the real handler,
     * rather than replacing its clock or changing scheduling in the test. */
    uint64_t start=now+500000ull*48000/rate;
    uint64_t end=after+(now+500000-after)*48000/rate;
    assert(s->q[0].report_due_us>=(start<end?start:end));
    assert(s->q[0].report_due_us<=(start>end?start:end));
    assert(s->q[0].due_us==original.q[0].due_us&&s->q[0].context==0xABCD);
    assert(!memcmp(&s->q[1],&original.q[1],sizeof s->q[1])&&s->nq==2);
}

/* Current spatial methods are DS_OK stubs. Keep those production handlers and
 * their distinct argument counts; do not add another success stub in the RPC. */
void object_test_stream_parameter(xctx *c)
{
    object_test_audio_owner();
    xv_fn_t fn=NULL;unsigned args=3,sp=c->r[4];
    switch(X_M32(sp)) {
    case 0x298E7:fn=xv_hle_IDirectSoundStream_SetMaxDistance;break;
    case 0x2992A:fn=xv_hle_IDirectSoundStream_SetMinDistance;break;
    case 0x2999D:fn=xv_hle_IDirectSoundStream_SetConeAngles;args=4;break;
    case 0x299F0:fn=xv_hle_IDirectSoundStream_SetConeOutsideVolume;break;
    case 0x2979A:fn=xv_hle_IDirectSoundStream_SetI3DL2Source;break;
    }
    assert(fn);uint32_t saved[8],stack[5];memcpy(saved,c->r,sizeof saved);
    for(unsigned i=0;i<=args;i++)stack[i]=X_M32(sp+4*i);
    fn(c);
    assert(c->r[0]==0&&c->r[4]==sp+4*(args+1));
    for(unsigned i=1;i<8;i++)if(i!=4)assert(c->r[i]==saved[i]);
    for(unsigned i=0;i<=args;i++)assert(stack[i]==X_M32(sp+4*i));
}
