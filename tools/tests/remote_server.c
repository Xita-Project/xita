#include <assert.h>
#include "../../runtime/xv_remote.c"
static unsigned benchmark;
uint32_t xv_benchmark_status(void) {return LOAD(&benchmark);}
unsigned xv_benchmark_remote_busy(void) {return LOAD(&benchmark)!=0;}
int xv_benchmark_remote_request(unsigned kind)
{unsigned expected=0;return kind>=1&&kind<=XV_BENCH_LOG_WRITER&&__atomic_compare_exchange_n(&benchmark,&expected,kind,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)?0:-1;}
void xv_logf(const char *fmt,...) {(void)fmt;}
void xv_log_get_status(xv_log_status *out)
{
    /* Worst-width status values exercise bounded JSON formatting while the
     * endpoint remains usable during a failed logger drain. */
    memset(out,0,sizeof *out);out->state=XV_LOG_ERROR;out->error=-77;
    out->queued=4;out->open_report=1;out->accepted=UINT64_MAX;
    out->enabled=out->transition=UINT32_MAX;
    out->written=UINT64_MAX-1;out->synced=UINT64_MAX-2;
    out->failed_sequence=UINT64_MAX;out->failed_file_offset=32767;
    out->pending_report=UINT64_MAX;out->pending_frame=out->pending_chunk=UINT32_MAX;
    out->accepted_bytes=UINT64_MAX;out->written_bytes=UINT64_MAX-1;out->synced_bytes=UINT64_MAX-2;
}
int main(void)
{
    unsigned n=99;
    assert(!uint_value("",100,&n) && !uint_value("-1",100,&n));
    assert(!uint_value("4294967296",0xffffffffu,&n));
    assert(uint_value("4294967295",0xffffffffu,&n)&&n==0xffffffffu);
    assert(!uint_value("1",0,&n)&&uint_value("0",0,&n));
    STORE(&enabled,1);STORE(&pad_axes,0x648080c0);STORE(&pad_buttons,8);
    STORE(&pad_deadline,(uint32_t)(remote_now()+2000000));
    uint32_t buttons=0;uint8_t lx=128,ly=128,rx=128,ry=128;
    xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(buttons==8&&lx==192&&ly==128&&rx==128&&ry==100);
    buttons=0x10000;lx=ly=rx=ry=128;
    xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(buttons==0x10008&&lx==192&&ry==100);
    buttons=0x10010;lx=ly=rx=ry=128;
    xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(buttons==0x10010&&lx==128);
    buttons=16;lx=ly=rx=ry=128;xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(buttons==16&&lx==128);
    buttons=0;lx=0;xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(!buttons&&lx==0);
    lx=128;STORE(&pad_deadline,(uint32_t)(remote_now()-1));xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(!buttons&&lx==128);
    buttons=0x10000;xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(buttons==0x10000&&lx==128);
    buttons=0;
    STORE(&pad_deadline,(uint32_t)(remote_now()+2000000));STORE(&pad_seq,1);
    xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);assert(!buttons&&lx==128);
    STORE(&enabled,0);STORE(&pad_seq,0);STORE(&pad_deadline,0);
    xv_update_init();
    xv_update_confirm(0);
    xv_remote_start();
    if(!LOAD(&enabled)) {puts("DISABLED");return 0;}
    puts("READY");fflush(stdout);
    uint32_t *pixels=malloc(WIDTH*HEIGHT*4);assert(pixels);
    for(unsigned y=0;y<HEIGHT;y++)for(unsigned x=0;x<WIDTH;x++)pixels[y*WIDTH+x]=0xff550000|(y&255)<<8|(x&255);
    remote_nonblock(STDIN_FILENO);
    int frames=1;
    for(;;) {
        char command;int got=read(STDIN_FILENO,&command,1);
        if(got==0)break;
        if(got>0) {
            if(command=='q')break;
            if(command=='t') {printf("BOOT %d\n",xv_update_boot());fflush(stdout);continue;}
            if(command=='c') {printf("CONFIRM %d\n",xv_update_confirm(1));fflush(stdout);continue;}
            if(command=='h') {
                xv_update_progress(XV_UPDATE_GPU_DRAIN);
                xv_update_progress(XV_UPDATE_REQUESTED);
                xv_update_progress(XV_UPDATE_HANDOFF_COUNT);
            }
            if(command=='b')STORE(&benchmark,1);
            if(command=='n')STORE(&benchmark,0);
            if(command=='f')frames=!frames;
            if(command=='p') {
                buttons=0;lx=ly=rx=ry=128;xv_remote_pad(&buttons,&lx,&ly,&rx,&ry);
                printf("PAD %u %u %u %u %u\n",buttons,lx,ly,rx,ry);fflush(stdout);
            } else {puts("ACK");fflush(stdout);}
        }
        if(frames)xv_remote_frame(pixels,WIDTH,HEIGHT,WIDTH);
        remote_sleep(5000);
    }
    xv_remote_stop();free(pixels);assert(!screen&&!LOAD(&enabled)&&listener<0);return 0;
}
