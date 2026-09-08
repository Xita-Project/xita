#include "../kernel/xk_os_host.c"
#include <assert.h>
#include <sched.h>
static uint32_t request,completed;
static void *worker(void *unused)
{
    for(unsigned i=1;i<=100000;i++) {
        while(__atomic_load_n(&request,__ATOMIC_ACQUIRE)!=i)sched_yield();
        __atomic_store_n(&completed,i,__ATOMIC_RELEASE);xk_os_scheduler_notify();
    }
    return NULL;
}
int main(void)
{
    assert(xk_os_scheduler_prepare());
    xk_os_scheduler_notify();xk_os_scheduler_notify();xk_os_scheduler_wait(100000);
    assert(!g_scheduler_notified);
    pthread_t producer;assert(!pthread_create(&producer,NULL,worker,NULL));
    for(unsigned i=1;i<=100000;i++) {
        __atomic_store_n(&request,i,__ATOMIC_RELEASE);
        while(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)!=i)xk_os_scheduler_wait(100000);
    }
    assert(!pthread_join(producer,NULL));
    xk_os_scheduler_wait(1); /* Drain a final sticky notification, if present. */
    uint64_t started=xk_os_monotonic_us();xk_os_scheduler_wait(1000);
    assert(xk_os_monotonic_us()-started>=1000);
    puts("PASS: production scheduler notification, 100000 concurrent handoffs, early/coalesced signals and timeout");
}
