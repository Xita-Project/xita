#include <assert.h>
#include "../kernel/xk_thread.c"
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static uint64_t clock_us;
uint64_t xk_os_monotonic_us(void) { return clock_us; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
int main(void)
{
    xk_thread sleeper={0},ready={0};
    g_threads=&sleeper;
    sleeper.next=&ready;ready.state=0;
    for(unsigned all=0;all<2;all++) {
        clock_us=1;
        sleeper.state=1;sleeper.wait_n=0;sleeper.wait_all=all;sleeper.wait_until=1000;
        assert(!try_satisfy(&sleeper));
        xk_signal_check();assert(sleeper.state==1);
        assert(pick_next(NULL)==&ready&&sleeper.state==1);
        clock_us=99;assert(pick_next(NULL)==&ready&&sleeper.state==1);
        clock_us=100;assert(pick_next(NULL)==&sleeper&&sleeper.state==0&&sleeper.wait_result==-1);
        sleeper.state=1;sleeper.wait_until=100000;
        xk_thread_kick(&sleeper);assert(pick_next(NULL)==&sleeper); /* Explicit wake remains valid. */
    }
    uint32_t completion=3;
    sleeper.wait_word=&completion;sleeper.wait_value=4;sleeper.wait_n=0;
    sleeper.state=1;sleeper.wait_until=100000;clock_us=100;
    assert(!try_satisfy(&sleeper));
    xk_thread_kick(&sleeper);assert(pick_next(NULL)==&ready&&sleeper.state==1);
    __atomic_store_n(&completion,2,__ATOMIC_RELEASE);assert(!try_satisfy(&sleeper));
    __atomic_store_n(&completion,4,__ATOMIC_RELEASE);xk_signal_check();assert(sleeper.state==0);
    sleeper.state=1;completion=5;clock_us=10000;
    assert(pick_next(NULL)==&sleeper&&sleeper.wait_result==-1);
    sleeper.wait_word=NULL;
    xk_obj a={.type=XO_EVENT},b={.type=XO_EVENT};
    a.u.event.manual=b.u.event.manual=1;
    sleeper.wait_n=2;sleeper.wait_objs[0]=&a;sleeper.wait_objs[1]=&b;
    sleeper.wait_all=1;sleeper.state=1;a.u.event.signaled=1;
    assert(!try_satisfy(&sleeper));b.u.event.signaled=1;assert(try_satisfy(&sleeper));
    sleeper.wait_all=0;a.u.event.signaled=0;assert(try_satisfy(&sleeper)&&sleeper.wait_result==1);
    puts("PASS: sleeps after WaitAll retain deadlines, runnable peers continue, explicit kicks and real WaitAll/WaitAny objects still work");
}
