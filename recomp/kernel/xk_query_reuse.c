#include "xk.h"
#include "xk_query_reuse.h"
#include "xk_query_cpu.h"
#include "xk_query_memory.h"
#include <string.h>
#include <limits.h>

extern unsigned xv_object_world_run_admit(xctx *);
#if XV_QUERY_UNLOCK
extern int xv_object_world_query_release(xctx *);
extern void xv_object_world_query_reacquire(int);
#endif
extern void xv_object_math_report_check(void);
extern int xv_watch_n, xv_trace_funcs;
extern unsigned xv_collision_vertices_state, xv_segment_sphere_mode;
extern const unsigned xv_collision_traversal_mode;
extern void query_fused_172c95_171f94(xctx *);
extern void query_captured_172c95_171f94(XvQueryCpu *, XvQueryMemory *, xctx *);

#define REUSE_ENTRIES 64u
#define REUSE_RECORDS 16u
#define REUSE_OBSERVATIONS 8u
#define REUSE_COST 32u
#define REUSE_CAPTURE_INTERVAL 32u
#define REUSE_COOLDOWN 64u

typedef struct {
    uint32_t r[8], fsp, fpscr, stack[3], center[3], config[3];
    uint16_t fsw, fcw;
} ReuseKey;
typedef struct {
    ReuseKey key;
    unsigned used, finished, observations, last_observation, cost;
    unsigned cooling, cooldown_epoch;
    unsigned record; /* UINT_MAX when this history has no captured transaction. */
} ReuseEntry;
typedef struct {
    unsigned owner; /* History index plus one; zero is available. */
    XvQueryCpu cpu;
    XvQueryMemory memory;
} ReuseRecord;
typedef struct {
    uintptr_t arena, pages, image;
    uint32_t size;
} ReuseRoots;
#define REUSE_COUNTS(F) F(calls) F(lookup) F(cheap) F(repeats) F(promotable) \
    F(captures) F(finished) F(abandoned) F(cpu_reject) F(memory_reject) F(hits) \
    F(evictions) F(root_resets) F(busy) F(saved_consumed) F(short_budget) F(declines) \
    F(record_evictions) F(observed_ready) F(cost_ready) F(cpu_reasons) F(memory_reasons) \
    F(max_blocks) F(max_mappings) F(total_blocks) F(total_mappings)
typedef struct {
#define FIELD(n) uint32_t n;
    REUSE_COUNTS(FIELD)
#undef FIELD
} ReuseCounts;
static ReuseEntry entries[REUSE_ENTRIES];
static ReuseRecord records[REUSE_RECORDS];
static ReuseRoots roots;
static ReuseCounts counts;
static unsigned next_entry, epoch = 1, attempted, last_attempt;
/* The actor guard owns entries/epoch. It may be released inside callbacks.
 * busy protects borrowed entry storage across that release, never blocks an
 * original query, and remains set until the outer call has resumed admission.
 * Failed resume publishes reset_pending without dereferencing borrowed state.
 * Counts are atomic because declined/busy calls need not hold the same guard.
 */
static unsigned busy, reset_pending;
#define COUNT(n) ((void)__atomic_fetch_add(&counts.n, 1u, __ATOMIC_RELAXED))

#if defined(XV_QUERY_REUSE_TEST)
extern uint32_t xv_query_reuse_test_fpscr_get(void);
extern void xv_query_reuse_test_fpscr_set(uint32_t);
static uint32_t fp_get(void) { return xv_query_reuse_test_fpscr_get(); }
static void fp_set(uint32_t value) { xv_query_reuse_test_fpscr_set(value); }
#elif defined(__arm__)
static uint32_t fp_get(void)
{ uint32_t value; __asm__ volatile("vmrs %0,fpscr":"=r"(value)::"memory"); return value; }
static void fp_set(uint32_t value)
{ __asm__ volatile("vmsr fpscr,%0"::"r"(value):"memory"); }
#else
static uint32_t fp_get(void) { return 0; }
static void fp_set(uint32_t value) { (void)value; }
#endif

/* Optional whole-adapter elapsed attribution. One writer owns busy, including
 * across callback guard releases. Reporting requires joined workers. No clocks
 * occur at individual guest accesses. Native FP state survives clock calls. */
enum { PROFILE_ORIGINAL, PROFILE_CAPTURE, PROFILE_REPLAY, PROFILE_KINDS };
#if XV_QUERY_REUSE_PROFILE
typedef struct { uint64_t us, maximum; unsigned calls, invalid; } ReuseTime;
static ReuseTime profile_times[PROFILE_KINDS];
static uint64_t profile_started;
static unsigned profile_kind;
static uint64_t profile_clock(void)
{
    uint32_t fp = fp_get();
    uint64_t now = xk_os_monotonic_us();
    fp_set(fp); return now;
}
static void profile_begin(void)
{ profile_kind = PROFILE_ORIGINAL; profile_started = profile_clock(); }
static void profile_select(unsigned kind) { profile_kind = kind; }
static void profile_finish(void)
{
    uint64_t now = profile_clock();
    ReuseTime *t = &profile_times[profile_kind]; ++t->calls;
    if (now < profile_started) { ++t->invalid; return; }
    uint64_t elapsed = now - profile_started;
    t->us += elapsed;
    if (elapsed > t->maximum) t->maximum = elapsed;
}
static void profile_report(unsigned frames)
{
    static const char *const names[PROFILE_KINDS] = {"original", "capture", "replay"};
    for (unsigned i = 0; i < PROFILE_KINDS; ++i) {
        ReuseTime *t = &profile_times[i];
        XK_LOG("[query-reuse-cost] %u frames path %s calls %u elapsed-us %llu max-us %llu invalid %u; whole-adapter elapsed includes scheduling/callbacks, original includes declined records, capture includes failures\n",
            frames, names[i], t->calls, (unsigned long long)t->us,
            (unsigned long long)t->maximum, t->invalid);
    }
    memset(profile_times, 0, sizeof profile_times);
}
#else
#define profile_begin() ((void)0)
#define profile_select(kind) ((void)0)
#define profile_finish() ((void)0)
#define profile_report(frames) ((void)0)
#endif

static void config_get(uint32_t out[3])
{
    out[0] = __atomic_load_n(&xv_collision_vertices_state, __ATOMIC_ACQUIRE) & 1u;
    out[1] = __atomic_load_n(&xv_segment_sphere_mode, __ATOMIC_ACQUIRE) & 1u;
    out[2] = xv_collision_traversal_mode;
}
static ReuseRoots roots_get(void)
{
    ReuseRoots out;
    memset(&out, 0, sizeof out);
    out.arena = (uintptr_t)g_xram; out.pages = (uintptr_t)g_xpt;
    out.image = (uintptr_t)g_img_base; out.size = xk_mem_arena_size();
    return out;
}
static int roots_valid(const ReuseRoots *r)
{
    return r->arena && r->pages && r->image && r->size >= 8192u &&
           !((r->size - 4096u) & 4095u);
}
static int roots_equal(const ReuseRoots *a, const ReuseRoots *b)
{
    return a->arena == b->arena && a->pages == b->pages &&
           a->image == b->image && a->size == b->size;
}
static void reset_entries(void)
{
    /* Clear ownership only, not the large transaction payloads. */
    for (unsigned i = 0; i < REUSE_ENTRIES; ++i) entries[i].used = entries[i].finished = 0;
    for (unsigned i = 0; i < REUSE_RECORDS; ++i) records[i].owner = 0;
    next_entry = attempted = last_attempt = 0;
}
static XvQueryMemoryView view_get(const ReuseRoots *r)
{
    XvQueryMemoryView view = {(uint8_t *)r->arena, r->size - 4096u,
                             (const uint32_t *)r->pages, 1u << 20};
    return view;
}
/* Coarse key only. Guest byte reads split safely over page mappings, reject
 * trash/unaligned PTEs, and never invoke fault/MMIO policy. The transaction's
 * full byte/mapping validation is still required for every hit. */
static int key_copy(const XvQueryMemoryView *v, uint32_t address, void *out, unsigned n)
{
    uint8_t *dst = out;
    if (!n || address > UINT32_MAX - (n - 1u)) return 0;
    while (n) {
        uint32_t physical = v->pages[address >> 12], offset = address & 4095u;
        unsigned bytes = 4096u - offset;
        if (bytes > n) bytes = n;
        if ((physical & 4095u) || physical > v->usable_size - 4096u) return 0;
        memcpy(dst, v->arena + physical + offset, bytes);
        dst += bytes; n -= bytes;
        if (n) address += bytes;
    }
    return 1;
}
static int key_get(ReuseKey *key, const xctx *c, uint32_t fp, const XvQueryMemoryView *view)
{
    memset(key, 0, sizeof *key);
    memcpy(key->r, c->r, sizeof key->r);
    key->fsp = c->fsp; key->fsw = c->fsw; key->fcw = c->fcw; key->fpscr = fp;
    config_get(key->config);
    return key_copy(view, c->r[4], key->stack, sizeof key->stack) &&
           key_copy(view, key->stack[1], key->center, sizeof key->center);
}
static void release_record(ReuseEntry *e)
{
    if (e->record < REUSE_RECORDS && records[e->record].owner == (unsigned)(e - entries) + 1u)
        records[e->record].owner = 0;
    e->record = UINT_MAX;
}
static void cool(ReuseEntry *e)
{
    release_record(e);
    e->finished = 0; e->observations = e->cost = 0;
    e->cooling = 1; e->cooldown_epoch = epoch; e->last_observation = epoch;
}
/* Ordinary misses can replace only lightweight history. A completed record's
 * history remains pinned until full dependency rejection or record pressure. */
static ReuseEntry *new_history(void)
{
    for (unsigned n = 0; n < REUSE_ENTRIES; ++n) {
        ReuseEntry *e = &entries[next_entry];
        next_entry = (next_entry + 1u) % REUSE_ENTRIES;
        if (!e->used || !e->finished) return e;
    }
    return NULL; /* Fail closed if an ownership invariant is ever violated. */
}
static ReuseRecord *new_record(ReuseEntry *e)
{
    unsigned selected = 0, oldest = 0;
    for (unsigned i = 0; i < REUSE_RECORDS; ++i) {
        if (!records[i].owner) { selected = i; break; }
        ReuseEntry *owner = &entries[records[i].owner - 1u];
        unsigned age = epoch - owner->last_observation;
        if (i == 0 || age > oldest) { selected = i; oldest = age; }
    }
    ReuseRecord *r = &records[selected];
    if (r->owner) {
        cool(&entries[r->owner - 1u]);
        COUNT(record_evictions);
    }
    r->owner = (unsigned)(e - entries) + 1u;
    e->record = selected;
    return r;
}
static void release_busy(void)
{ profile_finish(); __atomic_store_n(&busy, 0u, __ATOMIC_RELEASE); }
static int resume(xctx *c)
{
    if (xv_object_world_run_admit(c)) return 1;
    __atomic_store_n(&reset_pending, 1u, __ATOMIC_RELEASE);
    release_busy(); return 0;
}
static void original(xctx *c, ReuseEntry *entry)
{
    int32_t before = c->preempt;
    query_fused_172c95_171f94(c);
    if (!resume(c)) return;
    ReuseRoots after = roots_get();
    if (!roots_equal(&after, &roots)) {
        reset_entries(); roots = after; COUNT(root_resets);
    } else if (entry && entry->used && c->preempt > 0 && before >= c->preempt) {
        /* A heuristic only: callback/refill cannot create an unsafe hit because
         * every actual capture independently abandons those execution paths. */
        entry->cost = (unsigned)before - (unsigned)c->preempt;
    }
    release_busy();
}

void xv_query_reuse_run(xctx *c)
{
#if XV_QUERY_UNLOCK
    {
        /* Admitted lanes run the fused query without the actor guard and skip
         * the reuse cache/world-run helpers, which assume a held transaction. */
        int unlocked=xv_object_world_query_release(c);
        if(unlocked) { query_fused_172c95_171f94(c);xv_object_world_query_reacquire(unlocked);return; }
    }
#endif
    uint32_t fp = fp_get();
    COUNT(calls);
    if (!xv_object_world_run_admit(c)) {
        COUNT(declines); query_fused_172c95_171f94(c); return;
    }
    if (__atomic_exchange_n(&busy, 1u, __ATOMIC_ACQ_REL)) {
        COUNT(busy); query_fused_172c95_171f94(c); return;
    }
    profile_begin();
    if (__atomic_exchange_n(&reset_pending, 0u, __ATOMIC_ACQ_REL)) reset_entries();
    ReuseRoots current = roots_get();
    if (!roots_equal(&current, &roots)) {
        reset_entries(); roots = current; COUNT(root_resets);
    }
    if (xv_watch_n || xv_trace_funcs || !roots_valid(&roots) || c->fsp >= 8 ||
        (fp & XV_QUERY_CPU_TRAPS) || c->preempt <= 0) {
        COUNT(declines); original(c, NULL); return;
    }
    XvQueryMemoryView view = view_get(&roots);
    ReuseKey key;
    if (!key_get(&key, c, fp, &view)) { COUNT(cheap); original(c, NULL); return; }
    COUNT(lookup);
    ReuseEntry *e = NULL;
    for (unsigned i = 0; i < REUSE_ENTRIES; ++i)
        if (entries[i].used && !memcmp(&entries[i].key, &key, sizeof key)) {
            e = &entries[i]; COUNT(repeats); break;
        }
    if (!e) {
        e = new_history();
        if (!e) { COUNT(cheap); original(c, NULL); return; }
        if (e->used) COUNT(evictions);
        e->key = key; e->used = 1; e->finished = e->cooling = e->cost = 0;
        e->record = UINT_MAX;
        e->observations = 1; e->last_observation = epoch;
    } else if (e->last_observation != epoch) {
        e->last_observation = epoch;
        if (e->observations < REUSE_OBSERVATIONS) ++e->observations;
    }
    if (e->finished) {
        ReuseRecord *r = &records[e->record];
        /* A temporarily short budget should not discard an otherwise valid
         * record. Original budget/callback behavior is retained. */
        if ((uint32_t)c->preempt <= r->cpu.consumed) {
            COUNT(short_budget); original(c, NULL); return;
        }
        if (!xv_query_cpu_validate(&r->cpu, c, fp)) {
            COUNT(cpu_reject); cool(e); original(c, e); return;
        }
        if (!xv_query_memory_validate(&r->memory, &view)) {
            COUNT(memory_reject); cool(e); original(c, e); return;
        }
        /* Both validates precede any replay writes. Under the existing actor
         * guard there is no callback or root mutation between these calls.
         * Replay functions repeat validation but cannot newly fail here. */
        profile_select(PROFILE_REPLAY);
        xv_query_memory_replay(&r->memory, &view);
        xv_query_cpu_replay(&r->cpu, c, &fp);
        fp_set(fp); COUNT(hits);
        __atomic_fetch_add(&counts.saved_consumed, r->cpu.consumed, __ATOMIC_RELAXED);
        release_busy(); return;
    }
    if (e->cooling && epoch - e->cooldown_epoch >= REUSE_COOLDOWN) e->cooling = 0;
    if (!e->cooling && e->observations >= REUSE_OBSERVATIONS) COUNT(observed_ready);
    if (!e->cooling && e->cost >= REUSE_COST) COUNT(cost_ready);
    if (!e->cooling && e->observations >= REUSE_OBSERVATIONS && e->cost >= REUSE_COST) {
        COUNT(promotable);
        if (!attempted || epoch - last_attempt >= REUSE_CAPTURE_INTERVAL) {
            attempted = 1; last_attempt = epoch; COUNT(captures);
            profile_select(PROFILE_CAPTURE);
            ReuseRecord *r = new_record(e);
            int cpu_ok = xv_query_cpu_begin(&r->cpu, c, fp);
            int memory_ok = xv_query_memory_begin(&r->memory, &view);
            if (cpu_ok && memory_ok) query_captured_172c95_171f94(&r->cpu, &r->memory, c);
            else query_fused_172c95_171f94(c);
            uint32_t exit_fp = fp_get();
            if (!resume(c)) { COUNT(abandoned); return; }
            /* Capture may already be invalid; occupancy still identifies the
             * exhausted limit. The actor guard serializes these updates. */
            if (r->memory.block_count > counts.max_blocks) counts.max_blocks = r->memory.block_count;
            if (r->memory.mapping_count > counts.max_mappings) counts.max_mappings = r->memory.mapping_count;
            counts.total_blocks += r->memory.block_count;
            counts.total_mappings += r->memory.mapping_count;
            ReuseRoots after = roots_get(); uint32_t config[3]; config_get(config);
            if (!roots_equal(&after, &roots)) {
                COUNT(abandoned); reset_entries(); roots = after; COUNT(root_resets);
                release_busy(); return;
            }
            if (cpu_ok && memory_ok && !xv_watch_n && !xv_trace_funcs &&
                !memcmp(config, key.config, sizeof config)) {
                cpu_ok = xv_query_cpu_finish(&r->cpu, c, exit_fp);
                memory_ok = xv_query_memory_finish(&r->memory, &view);
            } else cpu_ok = memory_ok = 0;
            if (cpu_ok && memory_ok) { e->finished = 1; COUNT(finished); }
            else {
                __atomic_fetch_or(&counts.cpu_reasons, r->cpu.reason, __ATOMIC_RELAXED);
                __atomic_fetch_or(&counts.memory_reasons, r->memory.reason, __ATOMIC_RELAXED);
                cool(e); COUNT(abandoned);
            }
            release_busy(); return;
        }
    }
    original(c, e);
}

void xv_query_reuse_epoch(void)
{
    xv_object_math_report_check();
    if (__atomic_load_n(&busy, __ATOMIC_ACQUIRE)) return;
    if (__atomic_exchange_n(&reset_pending, 0u, __ATOMIC_ACQ_REL)) reset_entries();
    if (epoch == UINT_MAX) { reset_entries(); epoch = 1; }
    else ++epoch;
}
void xv_query_reuse_report(unsigned frames)
{
    xv_object_math_report_check();
    if (!frames || __atomic_load_n(&busy, __ATOMIC_ACQUIRE)) return;
    profile_report(frames);
    ReuseCounts n;
#define TAKE(name) n.name = __atomic_exchange_n(&counts.name, 0u, __ATOMIC_RELAXED);
    REUSE_COUNTS(TAKE)
#undef TAKE
    unsigned retained = 0;
    for (unsigned i = 0; i < REUSE_RECORDS; ++i) retained += records[i].owner != 0;
    XK_LOG("[query-reuse-detail] history/record capacity %u/%u retained %u; observed/cost ready %u/%u record-evict %u; CPU/memory reason-mask %02x/%02x; blocks/mappings max %u/%u total %u/%u capacity %u/%u\n",
        REUSE_ENTRIES, REUSE_RECORDS, retained, n.observed_ready, n.cost_ready,
        n.record_evictions, n.cpu_reasons, n.memory_reasons, n.max_blocks, n.max_mappings,
        n.total_blocks, n.total_mappings, XV_QUERY_MEMORY_BLOCKS, XV_QUERY_MEMORY_MAPPINGS);
    /* 32-bit per-window counts; no timing claims. */
    XK_LOG("[query-reuse] %u frames calls/lookup/cheap/repeat/promotable %u/%u/%u/%u/%u; capture/finished/abandoned %u/%u/%u; CPU/memory reject %u/%u hits %u saved-budget %u; evict/root/busy/short/decline %u/%u/%u/%u/%u\n",
        frames, n.calls, n.lookup, n.cheap, n.repeats, n.promotable,
        n.captures, n.finished, n.abandoned, n.cpu_reject, n.memory_reject,
        n.hits, n.saved_consumed, n.evictions, n.root_resets, n.busy, n.short_budget, n.declines);
}
