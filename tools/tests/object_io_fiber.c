/* Exercise the production selected-fiber handoff and real APC/wait machinery. */
#include "kernel/xk.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
uint32_t xv_game_tls_dir;
static unsigned heap=0x10000,joins,reads,completions;
static xk_thread *io;
static xk_obj *event;
static xk_fiber *caller;
int xk_object_io_step(void);
void xk_KeDelayExecutionThread(xctx *c);
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int top)
{(void)a;(void)l;(void)h;(void)top;unsigned p=heap;heap=(heap+n+4095)&~4095u;assert(heap<0x100000);return p;}
uint32_t xk_kalloc(uint32_t n) {return xk_mem_alloc(n,0,0,0,0);}
void xk_file_release(xk_obj *o) {(void)o;assert(0);}
void xv_object_jobs_join(void) {joins++;}
void xv_call(xctx *c,uint32_t target)
{
    assert(target==0x32EC0u && xk_cur==io);
    assert(X_ARG(0)==0x12345678&&X_ARG(1)==0x11223344&&X_ARG(2)==0);
    completions++;c->r[4]+=16;
}
static void file_entry(void *arg)
{
    (void)arg;assert(xk_cur==io);
    for(;;) {
        assert(xk_wait(&event,1,0,1,NULL)==STATUS_SUCCESS);
        reads++;
        xk_apc_queue(io,0x32EC0u,0x12345678,0x11223344,0);
        /* SleepEx(0, true) must deliver the completion on this fiber, then the
         * next event wait returns to our selected caller without a global join. */
        xctx *c=&io->ctx;uint32_t sp=c->r[4];
        X_M32(0x200000)=0;X_M32(0x200004)=0;
        X_PUSH32(0x200000);X_PUSH32(1);X_PUSH32(1);X_PUSH32(0x12E6Eu);
        xk_KeDelayExecutionThread(c);assert(c->r[0]==STATUS_USER_APC&&c->r[4]==sp);
    }
}
static void owner_entry(void *arg)
{
    xk_thread *owner=arg;xk_cur=owner;
    io=xk_thread_create(65536,0,0x33AF0,0,0,0);assert(io);
    xk_os_fiber_destroy(io->fiber);io->fiber=xk_os_fiber_create(file_entry,NULL,512*1024);
    event=xk_obj_new(XO_EVENT);event->u.event.manual=0;
    X_M32(0x2E2D0C)=xk_handle_create(io->obj);
    xk_fiber *own=xk_os_fiber_current();
    /* First step parks in the actual unsignaled event wait. */
    assert(xk_object_io_step()==1&&io->state==1&&reads==0&&joins==0);
    assert(xk_cur==owner&&xk_os_fiber_current()==own);
    assert(xk_object_io_step()==0&&reads==0);
    for(unsigned i=0;i<100;i++) {
        event->u.event.signaled=1;
        assert(xk_object_io_step()==1);
        assert(reads==i+1&&completions==i+1&&io->napc==0&&io->state==1);
        assert(xk_cur==owner&&xk_os_fiber_current()==own&&joins==0);
    }
    unsigned handle=X_M32(0x2E2D0C);X_M32(0x2E2D0C)=0;
    assert(xk_object_io_step()==-1);X_M32(0x2E2D0C)=handle;
    io->start_routine=0xDEADBEEF;assert(xk_object_io_step()==-1);io->start_routine=0x33AF0;
    io->state=2;assert(xk_object_io_step()==-1);io->state=1;
    xk_os_fiber_switch(caller);abort();
}
int main(void)
{
    g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=4096*i;
    xk_thread owner={0};owner.id=4;owner.ctx.r[4]=0x1F0000;
    caller=xk_os_fiber_current();xk_fiber *f=xk_os_fiber_create(owner_entry,&owner,512*1024);
    xk_os_fiber_switch(f);
    assert(joins==0&&reads==100&&completions==100);
    xk_os_fiber_destroy(f);xk_os_fiber_destroy(io->fiber);
    puts("PASS: 100 selected file-fiber event/simulated-read/APC cycles, real wait states, preserved guest/fiber owner, no recursive job join; missing/wrong/suspended target rejected");
    return 0;
}
