/* Included by the production-extracting object worker test runner. */
#include "kernel/xk.h"
#include "kernel/xk_object_jobs.h"
#include "kernel/dsound_state.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
void object_test_audio_owner(void);
static unsigned callbacks;
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
