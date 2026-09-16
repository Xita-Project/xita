/* Included by xd3d.c so the packet uses the production rs_method semantics.
 * Exact Halo 3925 state islands: see games/halo_ce_3925/material_packets.py.
 * Preserve every ordered scratch store: even the guest stack may alias a
 * material byte, image mirror or deferred texture-state table through g_xpt.
 * No callback, preemption point, texture resolution or draw is moved here. */
#include "xk_material_packet.h"
#include "xk_object_jobs.h"

#ifdef XV_MATERIAL_PACKET
static int material_packet_configured = -1, material_packet_policy = -1;
static int material_packet_diagnostics;
static xv_material_packet_counters material_packet_counts;
extern int xv_trace_enabled __attribute__((weak));
extern int xv_trace_funcs __attribute__((weak));
extern int xv_watch_n __attribute__((weak));
extern const unsigned xv_guest_trace_enabled __attribute__((weak));
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
/* Shared actual-thread predicate supplied by the object-worker owner. An
 * older object-jobs build without it declines rather than trusting a marker. */
extern int xv_object_is_worker_thread(void) __attribute__((weak));
#endif

void xv_material_packet_override(int enabled)
{ material_packet_policy = enabled < 0 ? -1 : !!enabled; }

void xv_material_packet_read_counters(xv_material_packet_counters *out, int reset)
{
    if (out) *out = material_packet_counts;
    if (reset) memset(&material_packet_counts, 0, sizeof material_packet_counts);
}

static void material_packet_increment(uint32_t *counter)
{ if (*counter != UINT32_MAX) ++*counter; }

static void material_packet_rs(xctx *c, uint32_t back, uint32_t method, uint32_t value)
{
    X_M32(c->r[4] - 4) = back;
    rs_method(method, value);
}

static void material_packet_common(xctx *c)
{
    X_M32(c->r[4] - 4) = 0x901;
    X_M32(c->r[4] - 8) = 0x70377;
    xd3d_state.cull = X_M32(c->r[4] - 4);
    material_packet_rs(c, 0x70386, 0x40358, 0x10101);
    c->r[7] = (uint8_t)c->r[3];
    X_IMG32(0x18F48C) = 0x10101;
    material_packet_rs(c, 0x7039F, 0x40304, c->r[7]);
    X_IMG32(0x18F46C) = c->r[7];
    material_packet_rs(c, 0x703B4, 0x40344, 0x302);
    X_IMG32(0x18F478) = 0x302;
    material_packet_rs(c, 0x703CD, 0x40348, 0x303);
    X_IMG32(0x18F47C) = 0x303;
    material_packet_rs(c, 0x703E6, 0x40350, 0x8006);
    { uint8_t v = (uint8_t)c->r[3]; X_FLAGS(XK_LOGIC, 0, 0, v, 8); }
    X_IMG32(0x18F4A8) = 0x8006;
    if (!XF_Z(c)) goto disable_alpha;
    /* Keep this load after the preceding stores, including physical aliases. */
    { uint8_t v = X_M8(c->r[5] + 0x28) & 4; X_FLAGS(XK_LOGIC, 0, 0, v, 8); }
    if (!XF_Z(c)) goto disable_alpha;
    c->r[7] = 1;
    goto alpha;
disable_alpha:
    { uint32_t old = c->r[7]; X_FLAGS(XK_LOGIC, old, old, 0, 32); c->r[7] = 0; }
alpha:
    material_packet_rs(c, 0x7040F, 0x40300, c->r[7]);
    X_IMG32(0x18F470) = c->r[7];
    material_packet_rs(c, 0x70424, 0x40340, 0x7F);
    c->r[1] = 0x40340;
    c->r[2] = 0x7F;
    /* These arguments belong to the next, unchanged 80360 callback. */
    X_PUSH32(c->r[6]); X_PUSH32(1); X_PUSH32(0);
    X_IMG32(0x18F474) = 0x7F;
}

static void material_packet_texture(xctx *c, unsigned stage)
{
    static const uint32_t backs[4][6] = {
        {0x7044E, 0x7045C, 0x7046A, 0x70478, 0x70486},
        {0x704A9, 0x704BA, 0x704CB, 0x704DC, 0x704ED},
        {0x70510, 0x70521, 0x70532, 0x70543, 0x70554},
        {0x70577, 0x70588, 0x70599, 0x705AA, 0x705BB, 0x705CC},
    };
    unsigned back = 0;
    for (unsigned state = 10; state <= 15; ++state) {
        if (state == 12 && stage < 3) continue;
        uint32_t value = state < 13 ? (stage < 3 ? 1 : 3) : 2;
        X_M32(c->r[4] - 4) = value;
        X_M32(c->r[4] - 8) = backs[stage][back++];
        X_M32(D3D_G_TEXTURESTATE + (stage * 32 + state) * 4) = X_M32(c->r[4] - 4);
    }
    c->r[1] = stage;
    c->r[2] = 15;
    /* No lazy-flag writes occur in the emitted reference texture islands. */
}

static int material_packet_compatible(void)
{
    if (material_packet_configured < 0) {
        const char *e = getenv("XV_MATERIAL_PACKET");
        material_packet_configured = e && atoi(e) != 0;
        material_packet_diagnostics = getenv("XV_D3D_HIST") != NULL ||
            getenv("XV_DS_CHECK") != NULL || getenv("XV_LOG_RS") != NULL ||
            getenv("XV_WATCH_FN") != NULL || getenv("XV_PROF") != NULL;
    }
    if (material_packet_diagnostics ||
        (&xv_trace_enabled && xv_trace_enabled) ||
        (&xv_trace_funcs && xv_trace_funcs) ||
        (&xv_watch_n && xv_watch_n > 0) ||
        (&xv_guest_trace_enabled && xv_guest_trace_enabled) || xd3d_hist_active()) return 0;
    return 1;
}
int xv_material_packet_available(void)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if (!xv_object_is_worker_thread || xv_object_is_worker_thread()) return 0;
#endif
    return material_packet_compatible();
}

/* Called by the existing post-Present owner report with object jobs joined.
 * No worker or GXM consumer uses these counters. No per-call clocks/logging. */
static void material_packet_report(unsigned frames)
{
    xv_material_packet_counters counts;
    xv_material_packet_read_counters(&counts, 1);
    int available = xv_material_packet_available();
    int enabled = available && (material_packet_policy < 0 ? material_packet_configured : material_packet_policy);
    D3DLOG("[material-packet] %u frames: enabled %d available %d; eligible %u/%u/%u/%u/%u accepted %u/%u/%u/%u/%u; islands 7036D/70440/70498/704FF/70566\n",
        frames, enabled, available, counts.eligible[0], counts.eligible[1], counts.eligible[2],
        counts.eligible[3], counts.eligible[4], counts.accepted[0], counts.accepted[1],
        counts.accepted[2], counts.accepted[3], counts.accepted[4]);
}

int xv_material_packet(xctx *c, unsigned island)
{
    if (!c || island >= XV_MATERIAL_PACKET_ISLANDS) return 0;
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if (xv_is_object_job(c) || !xv_object_is_worker_thread ||
        xv_object_is_worker_thread()) return 0;
#endif
    if (!material_packet_compatible()) return 0;
    material_packet_increment(&material_packet_counts.eligible[island]);
    if (!(material_packet_policy < 0 ? material_packet_configured : material_packet_policy)) return 0;
    if (island) material_packet_texture(c, island - 1);
    else material_packet_common(c);
    material_packet_increment(&material_packet_counts.accepted[island]);
    return 1;
}
#else
/* Ordinary builds contain no guest hook call and no report/logging overhead.
 * Keep the drained-owner control ABI available as an unavailable experiment. */
void xv_material_packet_override(int enabled) { (void)enabled; }
void xv_material_packet_read_counters(xv_material_packet_counters *out, int reset)
{ (void)reset; if (out) memset(out, 0, sizeof *out); }
int xv_material_packet_available(void) { return 0; }
int xv_material_packet(xctx *c, unsigned island) { (void)c; (void)island; return 0; }
static void material_packet_report(unsigned frames) { (void)frames; }
#endif
