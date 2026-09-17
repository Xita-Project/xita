#include "kernel_timer.h"
#include "log_budget.h"
#include <string.h>
static uint32_t dispatch_logs;

typedef struct {
    uint32_t address, dpc, period;
    uint64_t deadline;
    xk_obj *object;
    uint8_t active, synchronization;
} timer_record;
typedef struct { uint32_t address, arg1, arg2; uint64_t order; } dpc_record;
static timer_record timers[64];
static dpc_record dpcs[64];
static uint64_t queue_order;
static xk_thread *worker;
static int dispatching;
extern void __real_xk_KeInitializeTimerEx(xctx *);
extern void xv_logf(const char *, ...);

static int mapped(uint32_t address, uint32_t bytes)
{
    if (!address || !bytes || (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + bytes - 1) >> 12, limit = xk_mem_arena_size() - XK_PAGE;
    for (uint32_t p = address >> 12; p <= last; ++p)
        if ((g_xpt[p] & (XK_PAGE - 1)) || (uint64_t)g_xpt[p] + XK_PAGE > limit) return 0;
    return 1;
}
static int arguments(xctx *c, unsigned words)
{
    if (mapped(c->r[4], (words + 1) * 4)) return 1;
    h2_timer_fault(c, "unmapped arguments", c->r[4], words); return 0;
}
static timer_record *find_timer(uint32_t address)
{
    for (unsigned i = 0; i < 64; ++i) if (timers[i].address == address) return &timers[i];
    return NULL;
}
static dpc_record *find_dpc(uint32_t address)
{
    for (unsigned i = 0; i < 64; ++i) if (dpcs[i].address == address) return &dpcs[i];
    return NULL;
}
static dpc_record *first_queued_dpc(void)
{
    dpc_record *first = NULL;
    for (unsigned i = 0; i < 64; ++i)
        if (dpcs[i].order && (!first || dpcs[i].order < first->order)) first = &dpcs[i];
    return first;
}
static int valid_dpc(uint32_t address)
{
    return find_dpc(address) && mapped(address, 28) && X_M16(address) == 0x13 &&
           mapped(X_M32(address + 12), 1);
}
static void queue_dpc(dpc_record *d, uint32_t a1, uint32_t a2)
{
    if (d->order) return;
    d->arg1 = a1; d->arg2 = a2; d->order = ++queue_order;
    X_M8(d->address + 2) = 1;
    X_M32(d->address + 20) = a1; X_M32(d->address + 24) = a2;
}
static void expire(timer_record *t, uint64_t now, uint64_t wall)
{
    t->active = t->period != 0;
    if (t->active) t->deadline = now + (uint64_t)t->period * 10000;
    X_M8(t->address + 3) = t->active;
    if (t->active) {
        X_M32(t->address + 16) = (uint32_t)t->deadline;
        X_M32(t->address + 20) = (uint32_t)(t->deadline >> 32);
    }
    t->object->u.timer.signaled = 1; X_M32(t->address + 4) = 1;
    if (t->dpc) queue_dpc(find_dpc(t->dpc), (uint32_t)wall, (uint32_t)(wall >> 32));
}
static void worker_entry(xctx *c, void *opaque)
{
    (void)opaque;
    for (;;) {
        h2_timer_poll(c);
        /* A callback may legitimately requeue itself at an overdue absolute
         * deadline. Retain that work and its FIFO position across service
         * turns; yield only after its complete return restored PASSIVE IRQL. */
        if (first_queued_dpc()) { xk_yield(); continue; }
        uint64_t now = xk_uptime_100ns(), wait = 10000000; /* 1 second, arm/cancel kicks it */
        for (unsigned i = 0; i < 64; ++i) if (timers[i].active) {
            uint64_t remaining = timers[i].deadline > now ? timers[i].deadline - now : 0;
            if (remaining < wait) wait = remaining;
        }
        xk_sleep_us(wait / 10 + 1);
    }
}
static int ensure_worker(xctx *c)
{
    if (!worker) worker = xk_thread_create_host(worker_entry, NULL);
    if (worker) return 1;
    h2_timer_fault(c, "timer worker allocation failed", 0, 0); return 0;
}
void __wrap_xk_KeInitializeTimerEx(xctx *c)
{
    if (!arguments(c, 2)) return;
    uint32_t address = X_ARG(0), type = X_ARG(1);
    timer_record *t = find_timer(address);
    if (!t) for (unsigned i = 0; i < 64; ++i) if (!timers[i].address) { t = &timers[i]; break; }
    xk_obj *existing = xk_obj_from_guest(address);
    if (!mapped(address, 40) || type > 1 || !t || (t->address && t->active) ||
        (existing && existing->type != XO_TIMER)) {
        h2_timer_fault(c, "invalid timer initialization", address, type); return;
    }
    __real_xk_KeInitializeTimerEx(c); /* retains the shared wait-object identity */
    xk_obj *object = xk_obj_from_guest(address);
    if (!object || object->type != XO_TIMER) { h2_timer_fault(c, "timer registration failed", address, 0); return; }
    *t = (timer_record){.address=address, .object=object, .synchronization=type};
    memset(&object->u.timer, 0, sizeof object->u.timer);
    X_M8(address + 2) = 10; X_M8(address + 3) = 0;
    X_M32(address + 8) = address + 8; X_M32(address + 12) = address + 8;
    X_M32(address + 16) = 0; X_M32(address + 20) = 0;
    X_M32(address + 24) = 0; X_M32(address + 28) = 0; X_M32(address + 36) = 0;
}
void __wrap_xk_KeInitializeDpc(xctx *c)
{
    if (!arguments(c, 3)) return;
    uint32_t address = X_ARG(0), routine = X_ARG(1), context = X_ARG(2);
    dpc_record *d = find_dpc(address);
    if (!d) for (unsigned i = 0; i < 64; ++i) if (!dpcs[i].address) { d = &dpcs[i]; break; }
    if (!mapped(address, 28) || !mapped(routine, 1) || !d || (d->address && d->order)) {
        h2_timer_fault(c, "invalid DPC initialization", address, routine); return;
    }
    *d = (dpc_record){.address=address};
    X_M16(address) = 0x13; X_M8(address + 2) = 0;
    X_M32(address + 12) = routine; X_M32(address + 16) = context;
    X_RET(3);
}
static void set_timer(xctx *c, int extended)
{
    unsigned words = extended ? 5 : 4;
    if (!arguments(c, words)) return;
    uint32_t address = X_ARG(0), period = extended ? X_ARG(3) : 0, dpc = X_ARG(words - 1);
    uint64_t due = (uint64_t)X_ARG(1) | (uint64_t)X_ARG(2) << 32;
    timer_record *t = find_timer(address);
    uint64_t now = xk_uptime_100ns(), wall = xk_time_100ns();
    uint64_t delta = (due >> 63) ? 0ull - due : (due > wall ? due - wall : 0);
    if (!t || !mapped(address, 40) || (int32_t)period < 0 ||
        (dpc && !valid_dpc(dpc)) || UINT64_MAX - now < delta ||
        UINT64_MAX - now < (uint64_t)period * 10000) {
        h2_timer_fault(c, "invalid timer arm", address, dpc); return;
    }
    if (!ensure_worker(c)) return;
    unsigned inserted = t->active;
    t->deadline = now + delta; t->active = 1; t->dpc = dpc; t->period = period;
    t->object->u.timer.signaled = 0; /* expiry belongs to this queue, not shared due polling */
    X_M8(address + 1) = !(due >> 63); X_M8(address + 3) = 1; X_M32(address + 4) = 0;
    X_M32(address + 16) = (uint32_t)t->deadline; X_M32(address + 20) = (uint32_t)(t->deadline >> 32);
    X_M32(address + 32) = dpc; X_M32(address + 36) = period;
    if (!delta) { expire(t, now, wall); xk_signal_check(); }
    static unsigned arms;                              /* heartbeat re-arms ~100/s: log first 400, then every 1000th */
    if (++arms <= 400 || !(arms % 1000))
        xv_logf("[h2/timer] arm timer=%08X due=%08X%08X remaining_us=%llu dpc=%08X period=%u inserted=%u\n",
                address, (uint32_t)(due >> 32), (uint32_t)due, (unsigned long long)(delta / 10), dpc, period, inserted);
    if (!dispatching) xk_thread_kick(worker);
    c->r[0] = inserted; X_RET(words);
}
void __wrap_xk_KeSetTimer(xctx *c) { set_timer(c, 0); }
void __wrap_xk_KeSetTimerEx(xctx *c) { set_timer(c, 1); }
void __wrap_xk_KeCancelTimer(xctx *c)
{
    if (!arguments(c, 1)) return;
    timer_record *t = find_timer(X_ARG(0));
    if (!t || !mapped(t->address, 40)) { h2_timer_fault(c, "unknown timer cancellation", X_ARG(0), 0); return; }
    c->r[0] = t->active; t->active = 0; X_M8(t->address + 3) = 0;
    if (worker && !dispatching) xk_thread_kick(worker);
    X_RET(1); /* cancellation does not remove an already queued DPC */
}
void __wrap_xk_KeInsertQueueDpc(xctx *c)
{
    if (!arguments(c, 3)) return;
    uint32_t address = X_ARG(0);
    if (!valid_dpc(address)) { h2_timer_fault(c, "unknown DPC insertion", address, 0); return; }
    if (!ensure_worker(c)) return;
    dpc_record *d = find_dpc(address); unsigned inserted = !d->order;
    if (inserted) queue_dpc(d, X_ARG(1), X_ARG(2));
    if (!dispatching) xk_thread_kick(worker);
    c->r[0] = inserted; X_RET(3);
}
void __wrap_xk_KeRemoveQueueDpc(xctx *c)
{
    if (!arguments(c, 1)) return;
    uint32_t address = X_ARG(0);
    if (!valid_dpc(address)) { h2_timer_fault(c, "unknown DPC removal", address, 0); return; }
    dpc_record *d = find_dpc(address); c->r[0] = d->order != 0;
    d->order = 0; X_M8(address + 2) = 0; X_RET(1);
}
/* Optional shared-kernel hooks; absent for Halo CE and all ordinary targets. */
int xk_game_timer_consume(xk_obj *object)
{
    timer_record *t = find_timer(object->guest);
    if (!t || t->object != object) return 0;
    if (t->synchronization) object->u.timer.signaled = 0;
    return 1;
}
void xk_game_yield_check(xk_thread *thread)
{
    uint64_t address = (uint64_t)thread->ctx.fs_base + KPCR_IRQL;
    if (dispatching || (address <= UINT32_MAX && mapped(address, 1) && X_M8(address) >= 2))
        h2_timer_fault(&thread->ctx, "elevated IRQL cannot block or preempt", 0, 0);
}
static void change_irql(xctx *c, unsigned level, int raise)
{
    if (!arguments(c, 0)) return;
    uint64_t address = (uint64_t)c->fs_base + KPCR_IRQL;
    if (address > UINT32_MAX || !mapped(address, 1)) {
        h2_timer_fault(c, "unmapped IRQL", c->fs_base, level); return;
    }
    uint8_t old = X_M8(address);
    /* This queue supports PASSIVE/APC/DISPATCH only, not device interrupt levels. */
    if (old > 2 || level > 2 || (raise ? level < old : level > old)) {
        h2_timer_fault(c, "unsupported IRQL transition", old, level); return;
    }
    X_M8(address) = level;
    if (raise) c->r[0] = old;
    X_RET(0);
}
void __wrap_xk_KfRaiseIrql(xctx *c) { change_irql(c, c->r[1] & 255, 1); }
void __wrap_xk_KfLowerIrql(xctx *c) { change_irql(c, c->r[1] & 255, 0); }
void __wrap_xk_KeRaiseIrqlToDpcLevel(xctx *c) { change_irql(c, 2, 1); }
void h2_timer_poll(xctx *c)
{
    uint64_t now = xk_uptime_100ns(), wall = xk_time_100ns();
    for (unsigned count = 0; count < 64; ++count) {
        timer_record *t = NULL;
        for (unsigned i = 0; i < 64; ++i)
            if (timers[i].active && timers[i].deadline <= now &&
                (!t || timers[i].deadline < t->deadline)) t = &timers[i];
        if (!t) break;
        if (!mapped(t->address, 40) || (t->dpc && !valid_dpc(t->dpc)) ||
            UINT64_MAX - now < (uint64_t)t->period * 10000) {
            h2_timer_fault(c, "invalid timer expiration", t->address, t->dpc); return;
        }
        expire(t, now, wall);
    }
    xk_signal_check();
    for (unsigned count = 0; count < 64; ++count) {
        dpc_record *d = first_queued_dpc();
        if (!d) return;
        if (!valid_dpc(d->address) || c->r[4] < 20 || !mapped(c->r[4] - 20, 20) ||
            c->fs_base > UINT32_MAX - KPCR_IRQL || !mapped(c->fs_base + KPCR_IRQL, 1)) {
            h2_timer_fault(c, "invalid DPC dispatch", d->address, c->r[4]); return;
        }
        uint32_t address = d->address, routine = X_M32(address + 12), context = X_M32(address + 16);
        xctx saved = *c; uint8_t irql = X_M8(c->fs_base + KPCR_IRQL);
        d->order = 0; X_M8(address + 2) = 0; X_M8(c->fs_base + KPCR_IRQL) = 2;
        X_PUSH32(d->arg2); X_PUSH32(d->arg1); X_PUSH32(context); X_PUSH32(address); X_PUSH32(0xDEAD0002u);
        dispatching = 1; c->preempt = 0x7FFFFFFF;
        if (h2_log_budget(&dispatch_logs, 256, 20000)) xv_logf("[h2/timer] dispatch dpc=%08X routine=%08X args=%08X,%08X\n", address, routine, d->arg1, d->arg2);
        xv_call(c, routine);
        dispatching = 0;
        if (c->r[4] != saved.r[4]) { h2_timer_fault(c, "DPC stack imbalance", address, c->r[4]); return; }
        X_M8(saved.fs_base + KPCR_IRQL) = irql; *c = saved;
    }
    /* 64 is a cooperative service budget, not a limit on callback lifetime.
     * Do not remove, reinsert, rewrite or execute the next queued callback. */
    dpc_record *pending = first_queued_dpc();
    if (pending) xv_logf("[h2/timer] service budget=64 retained dpc=%08X order=%llu args=%08X,%08X\n",
                        pending->address, (unsigned long long)pending->order, pending->arg1, pending->arg2);
}
