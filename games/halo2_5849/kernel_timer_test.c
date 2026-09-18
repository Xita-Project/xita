#include "kernel_timer.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static xk_obj objects[64];
static unsigned object_count, callbacks, kicks, creations, signals;
static uint64_t monotonic = 1000000, wall = 0x01DD0000D0000000ull;
static xk_thread fake_worker;
static xctx guest, worker_context;
static jmp_buf fault;
static uint32_t expected_dpc, expected_a1, expected_a2;
static int callback_rearm, callback_bad_stack, callback_yield;
static void (*worker_entry)(xctx *, void *);
static unsigned service_mode, service_calls, service_yields, service_limit;
static unsigned service_kicks, service_sleeps;
static uint32_t service_order[300], service_arguments[300][2];
static uint64_t catchup_due;
static jmp_buf service_done;
extern int xk_game_timer_consume(xk_obj *);
extern void xk_game_yield_check(xk_thread *);

uint32_t xk_mem_arena_size(void) { return 0x20000; }
uint64_t xk_uptime_100ns(void) { return monotonic; }
uint64_t xk_time_100ns(void) { return wall; }
void xv_logf(const char *format, ...) { (void)format; }
void h2_timer_fault(xctx *c, const char *message, uint32_t a, uint32_t b)
{ (void)c; (void)message; (void)a; (void)b; longjmp(fault, 1); }
xk_obj *xk_obj_from_guest(uint32_t address)
{
    for (unsigned i = 0; i < object_count; ++i) if (objects[i].guest == address) return &objects[i];
    return NULL;
}
void __real_xk_KeInitializeTimerEx(xctx *c)
{
    uint32_t t = X_ARG(0); xk_obj *o = xk_obj_from_guest(t);
    if (!o) { assert(object_count < 64); o = &objects[object_count++]; o->guest = t; o->type = XO_TIMER; }
    X_M8(t) = 8 + (X_ARG(1) & 1); X_M32(t + 4) = 0; X_RET(2);
}
xk_thread *xk_thread_create_host(void (*entry)(xctx *, void *), void *arg)
{ assert(entry && !arg); worker_entry=entry; ++creations; return &fake_worker; }
void xk_thread_kick(xk_thread *t) { assert(t == &fake_worker); ++kicks; }
void xk_yield(void)
{
    assert(service_mode && service_calls == 64 * (++service_yields));
    assert(kicks == service_kicks && X_M8(worker_context.fs_base + KPCR_IRQL) == 0);
    xk_game_yield_check(&fake_worker); /* No callback may still be dispatching. */
}
void xk_sleep_us(uint64_t us)
{
    assert(service_mode && service_calls == service_limit && us > 0);
    assert(X_M8(worker_context.fs_base + KPCR_IRQL) == 0 && kicks == service_kicks);
    ++service_sleeps; longjmp(service_done,1);
}
void xk_signal_check(void) { ++signals; }
static void arguments(unsigned n, const uint32_t *args)
{
    memset(&guest, 0xA6, sizeof guest); guest.r[4] = 0x1000;
    X_M32(guest.r[4]) = 0x12345678;
    for (unsigned i = 0; i < n; ++i) X_M32(guest.r[4] + 4 + i * 4) = args[i];
}
static void invoke(void (*fn)(xctx *), unsigned n, const uint32_t *args, int has_return, uint32_t result)
{
    arguments(n, args); xctx expected = guest; uint8_t saved[24];
    memcpy(saved, g_xram + 0x1000, (n + 1) * 4);
    expected.r[4] += (n + 1) * 4; if (has_return) expected.r[0] = result;
    fn(&guest);
    assert(!memcmp(&guest, &expected, sizeof guest));
    assert(!memcmp(saved, g_xram + 0x1000, (n + 1) * 4));
}
static void arm(uint32_t timer, uint64_t due, uint32_t dpc, unsigned inserted)
{
    uint32_t args[] = {timer, (uint32_t)due, (uint32_t)(due >> 32), dpc};
    invoke(__wrap_xk_KeSetTimer, 4, args, 1, inserted);
}
static void rejected(void (*fn)(xctx *), unsigned n, const uint32_t *args)
{
    arguments(n, args); xctx expected = guest;
    uint8_t *memory = malloc(0x20000); assert(memory); memcpy(memory, g_xram, 0x20000);
    if (!setjmp(fault)) { fn(&guest); abort(); }
    assert(!memcmp(&guest, &expected, sizeof guest) && !memcmp(memory, g_xram, 0x20000));
    free(memory);
}
void xv_call(xctx *c, uint32_t routine)
{
    assert(routine == 0x4000 && X_M32(c->r[4]) == 0xDEAD0002);
    if (service_mode) {
        unsigned n=service_calls++;
        assert(n<300 && X_ARG(1)==0x87654321 && X_M8(c->fs_base+KPCR_IRQL)==2);
        uint32_t dpc=X_ARG(0); assert(!X_M8(dpc+2));
        service_order[n]=dpc; service_arguments[n][0]=X_ARG(2); service_arguments[n][1]=X_ARG(3);
        if (service_mode==1 && dpc==0x3000) {
            catchup_due+=200000; /* Original 332B6D advances its stored deadline. */
            arm(0x2000,catchup_due,dpc,0);
        } else if (service_mode==2 && n+1<service_limit) {
            uint32_t args[]={dpc,n+1,0xBB000000u+n+1};
            invoke(__wrap_xk_KeInsertQueueDpc,3,args,1,1);
            args[1]=0xBAD; invoke(__wrap_xk_KeInsertQueueDpc,3,args,1,0);
        }
        uint32_t sp=c->r[4]; memset(c,0x5C,sizeof *c);c->r[4]=sp+20;
        return;
    }
    assert(X_ARG(0) == expected_dpc && X_ARG(1) == 0x87654321);
    assert(X_ARG(2) == expected_a1 && X_ARG(3) == expected_a2);
    assert(X_M8(c->fs_base + KPCR_IRQL) == 2 && X_M8(expected_dpc + 2) == 0);
    xctx irql_context = *c;
    __wrap_xk_KeRaiseIrqlToDpcLevel(&irql_context);
    assert(irql_context.r[0] == 2 && irql_context.r[4] == c->r[4] + 4);
    irql_context.r[1] = 2; __wrap_xk_KfLowerIrql(&irql_context);
    assert(X_M8(c->fs_base + KPCR_IRQL) == 2);
    ++callbacks;
    if (callback_yield) xk_game_yield_check(&fake_worker);
    if (callback_rearm) arm(0x2000, (uint64_t)-200000ll, expected_dpc, 0);
    uint32_t sp = c->r[4]; memset(c, 0x5C, sizeof *c); c->r[4] = sp + (callback_bad_stack ? 16 : 20);
}
static void poll(unsigned expected_count)
{
    xctx saved = worker_context;
    h2_timer_poll(&worker_context);
    assert(!memcmp(&worker_context, &saved, sizeof saved));
    assert(callbacks == expected_count && X_M8(worker_context.fs_base + KPCR_IRQL) == 0);
}
int main(void)
{
    g_xram = malloc(0x20000); g_img_base = g_xram; g_xpt = malloc((1u << 20) * 4);
    assert(g_xram && g_xpt); memset(g_xram, 0xCC, 0x20000);
    for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x1F000;
    for (unsigned i = 0; i < 16; ++i) g_xpt[i] = i * 4096;
    memset(&worker_context, 0xA5, sizeof worker_context); worker_context.r[4] = 0x8000;
    worker_context.fs_base = 0x5000; X_M8(0x5000 + KPCR_IRQL) = 0;
    fake_worker.ctx=worker_context;
    uint32_t init[] = {0x2000,0}; invoke(__wrap_xk_KeInitializeTimerEx,2,init,0,0);
    assert(X_M8(0x2000) == 8 && X_M8(0x2002) == 10 && !X_M32(0x2004));
    assert(X_M32(0x2008) == 0x2008 && X_M32(0x200C) == 0x2008);
    uint32_t dpc[] = {0x3000,0x4000,0x87654321}; invoke(__wrap_xk_KeInitializeDpc,3,dpc,0,0);
    assert(X_M16(0x3000) == 0x13 && !X_M8(0x3002));
    assert(X_M32(0x3004) == 0xCCCCCCCC && X_M32(0x3008) == 0xCCCCCCCC);
    assert(X_M32(0x300C) == 0x4000 && X_M32(0x3010) == 0x87654321);
    expected_dpc=0x3000;
    /* Absolute due time's low word lies inside the guarded high virtual range.
     * It is an inline value, never dereferenced. No timer fires early. */
    arm(0x2000, wall + 200000, 0x3000, 0);
    assert(X_M32(0x2010) == monotonic + 200000 && X_M8(0x2003) == 1);
    assert(creations == 1); poll(0); monotonic += 199999; wall += 199999; poll(0);
    ++monotonic; ++wall; expected_a1=wall; expected_a2=wall >> 32; poll(1);
    assert(!X_M8(0x2003) && X_M32(0x2004) == 1);
    xk_obj *o=xk_obj_from_guest(0x2000); assert(xk_game_timer_consume(o) && o->u.timer.signaled == 1);
    uint32_t cancel[]={0x2000}; invoke(__wrap_xk_KeCancelTimer,1,cancel,1,0);
    arm(0x2000, (uint64_t)-400000ll,0x3000,0);
    arm(0x2000, (uint64_t)-200000ll,0x3000,1);
    invoke(__wrap_xk_KeCancelTimer,1,cancel,1,1);
    monotonic += 500000; wall += 500000; poll(1);
    /* Already elapsed deadline queues, but does not inline-execute, a DPC.
     * Cancelling its timer does not remove that queued callback. */
    arm(0x2000, wall-1,0x3000,0); assert(callbacks==1 && X_M8(0x3002));
    invoke(__wrap_xk_KeCancelTimer,1,cancel,1,0);
    expected_a1=wall; expected_a2=wall >> 32; poll(2);
    /* Original callback can rearm the timer before returning. */
    callback_rearm=1; arm(0x2000,0,0x3000,0); poll(3); callback_rearm=0;
    monotonic+=199999; wall+=199999; poll(3); ++monotonic; ++wall;
    expected_a1=wall; expected_a2=wall >> 32; poll(4);
    /* Independent synchronization/periodic timer; one period is rescheduled
     * from expiration, without replaying an unbounded backlog. */
    init[0]=0x2100; init[1]=1; invoke(__wrap_xk_KeInitializeTimerEx,2,init,0,0);
    uint32_t periodic[]={0x2100,0,0,20,0}; invoke(__wrap_xk_KeSetTimerEx,5,periodic,1,0);
    xk_obj *sync=xk_obj_from_guest(0x2100); assert(sync->u.timer.signaled && X_M8(0x2103));
    assert(xk_game_timer_consume(sync) && !sync->u.timer.signaled);
    monotonic+=199999; wall+=199999; poll(4); assert(!sync->u.timer.signaled);
    ++monotonic; ++wall; poll(4); assert(sync->u.timer.signaled);
    cancel[0]=0x2100; invoke(__wrap_xk_KeCancelTimer,1,cancel,1,1);
    /* Queue coalescing preserves first arguments; removal has real state. */
    uint32_t queue[]={0x3000,0x11223344,0x55667788};
    invoke(__wrap_xk_KeInsertQueueDpc,3,queue,1,1); queue[1]=3;
    invoke(__wrap_xk_KeInsertQueueDpc,3,queue,1,0);
    expected_a1=0x11223344; expected_a2=0x55667788; poll(5);
    invoke(__wrap_xk_KeRemoveQueueDpc,1,queue,1,0);
    invoke(__wrap_xk_KeInsertQueueDpc,3,queue,1,1);
    invoke(__wrap_xk_KeRemoveQueueDpc,1,queue,1,1); poll(5);
    /* More than 64 real callbacks is not record exhaustion. The original
     * absolute schedule catches up without rewriting/skipping any deadline.
     * An already queued peer keeps its FIFO position ahead of the rearm. */
    dpc[0]=0x3100; invoke(__wrap_xk_KeInitializeDpc,3,dpc,0,0);
    service_mode=1; service_limit=195; catchup_due=wall-193*200000ull;
    arm(0x2000,catchup_due,0x3000,0);
    uint32_t peer[]={0x3100,0x123,0x456};invoke(__wrap_xk_KeInsertQueueDpc,3,peer,1,1);
    service_kicks=kicks; xctx saved_worker=worker_context;
    if (!setjmp(service_done)) worker_entry(&worker_context,NULL);
    assert(service_calls==195 && service_yields==3 && service_sleeps==1);
    assert(!memcmp(&worker_context,&saved_worker,sizeof worker_context));
    assert(service_order[0]==0x3000 && service_order[1]==0x3100);
    for (unsigned n=0;n<service_calls;++n) {
        assert(service_order[n]==(n==1?0x3100u:0x3000u));
        assert(service_arguments[n][0]==(n==1?0x123u:(uint32_t)wall));
        assert(service_arguments[n][1]==(n==1?0x456u:(uint32_t)(wall>>32)));
    }
    assert(catchup_due==wall+200000 && X_M8(0x2003) && !X_M8(0x3002) && !X_M8(0x3102));
    service_mode=0; cancel[0]=0x2000; invoke(__wrap_xk_KeCancelTimer,1,cancel,1,1);
    /* Direct self-insertion also remains queued across budget boundaries;
     * duplicate insertions cannot overwrite arguments or consume callbacks. */
    service_mode=2;service_calls=service_yields=service_sleeps=0;service_limit=193;
    queue[1]=0;queue[2]=0xBB000000;invoke(__wrap_xk_KeInsertQueueDpc,3,queue,1,1);
    service_kicks=kicks;
    if (!setjmp(service_done)) worker_entry(&worker_context,NULL);
    assert(service_calls==193 && service_yields==3 && service_sleeps==1);
    assert(!memcmp(&worker_context,&saved_worker,sizeof worker_context));
    for (unsigned n=0;n<service_calls;++n) {
        assert(service_order[n]==0x3000 && service_arguments[n][0]==n && service_arguments[n][1]==0xBB000000u+n);
    }
    assert(!X_M8(0x3002)); service_mode=0;
    /* Guard/wrap/type/unknown-object/period failures happen before mutation. */
    uint32_t invalid[]={0xD0000000,0,0,0,0}; rejected(__wrap_xk_KeInitializeTimerEx,2,invalid);
    invalid[0]=0xFFFFFFF0; rejected(__wrap_xk_KeInitializeDpc,3,invalid);
    invalid[0]=0x2000; invalid[1]=2; rejected(__wrap_xk_KeInitializeTimerEx,2,invalid);
    invalid[0]=0x2200; rejected(__wrap_xk_KeSetTimer,4,invalid);
    invalid[0]=0x2000; invalid[3]=0xD0000000; rejected(__wrap_xk_KeSetTimer,4,invalid);
    invalid[3]=0xFFFFFFFF; rejected(__wrap_xk_KeSetTimerEx,5,invalid);
    uint64_t saved_time=monotonic; monotonic=UINT64_MAX-1;
    invalid[1]=0xFFFFFF00; invalid[2]=0xFFFFFFFF; invalid[3]=0;
    rejected(__wrap_xk_KeSetTimer,4,invalid); monotonic=saved_time;
    /* Stack imbalance and DPC blocking remain explicit faults. */
    queue[1]=expected_a1; queue[2]=expected_a2;
    invoke(__wrap_xk_KeInsertQueueDpc,3,queue,1,1); callback_bad_stack=1;
    if (!setjmp(fault)) { h2_timer_poll(&worker_context); abort(); }
    callback_bad_stack=0; worker_context.r[4]=0x8000; worker_context.fs_base=0x5000;
    invoke(__wrap_xk_KeInsertQueueDpc,3,queue,1,1); callback_yield=1;
    if (!setjmp(fault)) { h2_timer_poll(&worker_context); abort(); }
    assert(creations == 1 && kicks && signals);
    free(g_xpt); free(g_xram);
    puts("H2 timers: ABI/deadlines, 195-call overdue catch-up, 193-call self-requeue, FIFO/arguments across passive service yields, coalescing, cancellation, strict guards and full CPU isolation passed.");
    return 0;
}
