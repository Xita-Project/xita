/* Synthetic packets through the actual shared decoder, no owned data. */
#define main old_stream_tests
#include "audio_buffer_test.c"
#undef main
#include "audio_stream_cursor.h"
static xctx global_stream_description(void)
{
    const uint8_t format[18]={1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,0,0};
    uint32_t fields[6]={0x40000000,2,0x4ff9,0x335d82,0x6fe6,0x8ffd};
    uint32_t routes[2]={1,0xaffe},pair[2]={27,0};
    x_guest_write(0x3ffd,fields,24);x_guest_write(0x4ff9,format,18);
    x_guest_write(0x8ffd,routes,8);x_guest_write(0xaffe,pair,8);
    xctx c=context(0x3ffd,0x6ffe,0,0);X_M32(c.r[4])=0x335ed8;return c;
}
int main(void)
{
    g_xram=calloc(1,0x200000);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);assert(g_xram&&g_xpt);
    for(unsigned i=0;i<1u<<20;++i)g_xpt[i]=0x1ff000;
    for(unsigned i=1;i<16;++i)g_xpt[i]=(i-1)*4096;
    g_xpt[3]=0x8000;g_xpt[7]=0x9000;g_xpt[0x386]=0xf000;X_M32(0x386B0C)=0;
    xctx c=context(0,0x6200,0,0);call(&c,0x37D797,0,3);
    c=global_stream_description();call(&c,0x37D835,0,2);uint32_t h=read32(0x6ffe);
    h2_audio_stream *s=stream_live(&c,0x37D835,h);xa_voice *v=&g_v[s->voice];
    c=context(h,0,0,0);call(&c,0x37B818,0,2);
    int16_t input[160];for(unsigned i=0;i<160;++i)input[i]=(int16_t)(i*97-7000);
    x_guest_write(0xa000,input,sizeof input);x_guest_write(0xb000,input,sizeof input);
    assert(!xk_audio_stream_push(s->voice,0xa000,320)&&!xk_audio_stream_push(s->voice,0xb000,320));
    h2_stream_cursor cur;unsigned read_ahead=0,ready_at=0,drained_at=0;
    for(unsigned f=1;f<=2048;++f){
        int16_t output[2];xk_audio_mix(output,1);assert(h2_stream_cursor_from_voice(v,&cur));
        uint64_t expected=(uint64_t)f*10922/65536;if(expected>320)expected=320;
        assert(cur.source_frames==expected);
        if(v->q[v->qhead].consumed && cur.source_frames<160)++read_ahead;
        if(!ready_at && cur.source_frames>=162)ready_at=f;
        if(!drained_at && cur.drained)drained_at=f;
        if(f==1024){
            assert(cur.source_frames==170 && v->q[v->qhead].consumed);
            assert(xk_audio_stream_pop_consumed(s->voice)==1);
            assert(h2_stream_cursor_from_voice(v,&cur)&&cur.source_frames==10);
            /* Restore queue head for the remaining cumulative oracle. */
            --v->qhead;if(v->qhead<0)v->qhead+=XA_MAX_PKTS;++v->nq;++v->rd;
        }
    }
    assert(read_ahead && ready_at==973 && drained_at==1927 && cur.drained);
    assert(xk_audio_stream_pop_consumed(s->voice)==1 && xk_audio_stream_pop_consumed(s->voice)==1);
    assert(h2_stream_cursor_from_voice(v,&cur)&&!cur.source_frames&&cur.drained);
    xa_voice bad=*v;h2_stream_cursor untouched={123,7},out=untouched;
    bad.ncarry=128;assert(!h2_stream_cursor_from_voice(&bad,&out)&&!memcmp(&out,&untouched,sizeof out));
    bad=*v;bad.rate=44100;assert(!h2_stream_cursor_from_voice(&bad,&out));
    bad=*v;bad.blk_i=65;assert(!h2_stream_cursor_from_voice(&bad,&out));
    bad=*v;bad.rd=1;assert(!h2_stream_cursor_from_voice(&bad,&out));
    bad=*v;bad.volume=0.5f;assert(!h2_stream_cursor_from_voice(&bad,&out));
    xk_audio_voice_stop(s->voice);c=context(h,0,0,0);call(&c,0x37AB87,0,1);
    uint32_t dev=read32(0x6200);c=context(dev-8,0,0,0);call(&c,0x37C70F,0,1);
    free(g_xram);free(g_xpt);
    puts("Halo 2 stream cursor: real decoder read-ahead, every resampler step, queue removal, drain and invalid-state isolation pass");return 0;
}
