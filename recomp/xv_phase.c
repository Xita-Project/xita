/* Bounded phase accounting on the serialized guest baton. No worker samples
 * these stacks. Scheduled elapsed includes native blocking/preemption; it is
 * not CPU cycles. Parked elapsed is measured across explicit guest handoffs. */
#include "xv_phase.h"
#include <stdlib.h>
#include <string.h>

uint64_t xk_os_monotonic_us(void);
void xk_os_log(const char *fmt, ...);
extern const xv_phase_target xv_phase_targets[] __attribute__((weak));
extern const unsigned xv_phase_target_count __attribute__((weak));

int xv_phase_enabled;
typedef struct {
    void *context;
    xv_phase_scope *top;
    uint64_t last;
    unsigned generation, depth, parked;
} phase_owner;
typedef struct {
    uint64_t active, self, parked, parked_self, calls;
} phase_stat;
static phase_owner owners[XV_PHASE_MAX_OWNERS];
static phase_stat stats[XV_PHASE_MAX_TARGETS];
static unsigned target_count, frames, dropped, invalid;
static uint64_t window_start;

static phase_owner *find_owner(void *context, int create)
{
    phase_owner *free_owner = NULL;
    for (unsigned i = 0; i < XV_PHASE_MAX_OWNERS; i++) {
        if (owners[i].context == context) return &owners[i];
        if (!owners[i].context && !free_owner) free_owner = &owners[i];
    }
    if (create && free_owner) {
        free_owner->context = context;
        free_owner->generation++;
        free_owner->top = NULL;
        free_owner->depth = free_owner->parked = 0;
        return free_owner;
    }
    return NULL;
}

static void account(phase_owner *owner, uint64_t now)
{
    if (now < owner->last) { invalid++; owner->last = now; return; }
    uint64_t elapsed = now - owner->last;
    owner->last = now;
    if (!owner->top) return;
    phase_stat *top = &stats[owner->top->id];
    if (owner->parked) top->parked_self += elapsed;
    else top->self += elapsed;
    for (xv_phase_scope *s = owner->top; s; s = s->previous) {
        if (owner->parked) stats[s->id].parked += elapsed;
        else stats[s->id].active += elapsed;
    }
}

void xv_phase_init(void)
{
    const char *e = getenv("XV_PHASE_TIMING");
    target_count = &xv_phase_target_count ? xv_phase_target_count : 0;
    xv_phase_enabled = e && atoi(e) != 0 && xv_phase_targets &&
        target_count && target_count <= XV_PHASE_MAX_TARGETS;
    xk_os_log("[guest-phase] %s; %u compiled scopes; active includes native waits, parked is guest handoff time; inclusive rows overlap\n",
        xv_phase_enabled ? "on" : "off", target_count);
    if (xv_phase_enabled) window_start = xk_os_monotonic_us();
}

void xv_phase_begin(xv_phase_scope *scope, void *context, unsigned id)
{
    scope->owner = NULL;
    if (!xv_phase_enabled) return;
    if (!context || id >= target_count) { dropped++; return; }
    phase_owner *owner = find_owner(context, 1);
    if (!owner || owner->depth == XV_PHASE_MAX_DEPTH) { dropped++; return; }
    uint64_t now = xk_os_monotonic_us();
    account(owner, now);
    scope->owner = owner; scope->id = id;
    scope->generation = owner->generation;
    scope->previous = owner->top;
    owner->top = scope; owner->depth++;
    stats[id].calls++;
}

void xv_phase_end(xv_phase_scope *scope)
{
    phase_owner *owner = scope->owner;
    scope->owner = NULL;
    if (!owner || scope->generation != owner->generation) return;
    account(owner, xk_os_monotonic_us());
    if (owner->top != scope) {
        /* Never retain a pointer to an unwound native stack. */
        invalid++; owner->top = NULL; owner->depth = 0;
        owner->context = NULL; owner->generation++; return;
    }
    owner->top = scope->previous; owner->depth--;
    if (!owner->top) { owner->context = NULL; owner->generation++; }
}

void xv_phase_suspend(void *context)
{
    if (!xv_phase_enabled || !context) return;
    phase_owner *owner = find_owner(context, 0);
    if (!owner) return;
    account(owner, xk_os_monotonic_us()); owner->parked = 1;
}

void xv_phase_resume(void *context)
{
    if (!xv_phase_enabled || !context) return;
    phase_owner *owner = find_owner(context, 0);
    if (!owner) return;
    account(owner, xk_os_monotonic_us()); owner->parked = 0;
}

void xv_phase_forget(void *context)
{
    if (!xv_phase_enabled || !context) return;
    phase_owner *owner = find_owner(context, 0);
    if (!owner) return;
    account(owner, xk_os_monotonic_us());
    owner->context = NULL; owner->top = NULL; owner->depth = 0;
    owner->generation++;
}

void xv_phase_frame(unsigned end_frame)
{
    if (!xv_phase_enabled || ++frames < 60) return;
    uint64_t now = xk_os_monotonic_us();
    for (unsigned i = 0; i < XV_PHASE_MAX_OWNERS; i++)
        if (owners[i].context) account(&owners[i], now);
    xk_os_log("[guest-phase] %u frames end-frame %u window-us %llu dropped %u invalid %u; us below are window totals; self excludes selected children\n",
        frames, end_frame, (unsigned long long)(now >= window_start ? now - window_start : 0), dropped, invalid);
    for (unsigned i = 0; i < target_count; i++) {
        phase_stat *s = &stats[i];
        if (!s->calls && !s->active && !s->parked) continue;
        xk_os_log("[guest-phase] %08X %s calls %llu active-us %llu self-us %llu parked-us %llu parked-self-us %llu\n",
            xv_phase_targets[i].address, xv_phase_targets[i].name,
            (unsigned long long)s->calls, (unsigned long long)s->active,
            (unsigned long long)s->self, (unsigned long long)s->parked,
            (unsigned long long)s->parked_self);
    }
    memset(stats, 0, sizeof stats); frames = dropped = invalid = 0;
    /* Exclude this report's own writes from open scopes. The separately logged
     * report cost lets readers see the diagnostic perturbation of frame time. */
    uint64_t after = xk_os_monotonic_us();
    xk_os_log("[guest-phase] report-us %llu\n", (unsigned long long)(after >= now ? after - now : 0));
    window_start = after;
    for (unsigned i = 0; i < XV_PHASE_MAX_OWNERS; i++)
        if (owners[i].context) owners[i].last = after;
}
