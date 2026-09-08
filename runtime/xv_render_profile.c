#include <stdlib.h>
#include <string.h>
#include "xv_render_profile.h"
#include "xv_log.h"

extern uint64_t xk_os_monotonic_us(void) __attribute__((weak));
extern const unsigned xv_guest_trace_enabled __attribute__((weak));

/* All state belongs to the render pump, including reports. No concurrent
 * reads/resets of 64-bit accumulators from the guest thread are needed. */
static int enabled = -1, active;
static enum xv_render_stage current;
static uint64_t stamp, elapsed[XV_RENDER_STAGE_COUNT];
static unsigned calls[XV_RENDER_STAGE_COUNT], frames;
static uint32_t first_mesh, last_mesh;
static uint64_t call_us[XV_RENDER_CALL_COUNT];
static unsigned call_count[XV_RENDER_CALL_COUNT];
/* Count API invocations, including unsuccessful draws, across every replay
 * path. Summarize once per window so logging does not add per-frame I/O. */
static unsigned frame_draws, min_draws, max_draws, over_500, over_800;
static struct { uint32_t shader; unsigned draws, no_alpha; uint64_t indices; } work[128];
static unsigned work_count, work_overflow;
static unsigned depth_only_draws, cutout_draws;
static uint64_t cutout_indices;
static uint64_t depth_only_indices;
static struct { uint64_t end_us; unsigned count, frame_count, max_per_frame; } targets[10];

uint64_t xv_render_profile_call_begin(void)
{
    return active && current == XV_RENDER_SUBMIT ? xk_os_monotonic_us() + 1 : 0;
}
static uint64_t call_end(enum xv_render_call call, uint64_t token)
{
    if (!token || !active || current != XV_RENDER_SUBMIT || (unsigned)call >= XV_RENDER_CALL_COUNT) return 0;
    uint64_t us = xk_os_monotonic_us() - (token - 1);
    call_us[call] += us;
    call_count[call]++;
    if (call == XV_RENDER_DRAW) frame_draws++;
    return us;
}
void xv_render_profile_call_end(enum xv_render_call call, uint64_t token)
{
    call_end(call, token);
}
void xv_render_profile_scene_end(unsigned target, uint64_t token)
{
    if (!token || !active || current != XV_RENDER_SUBMIT) return;
    uint64_t us = call_end(XV_RENDER_SCENE_END, token);
    if (target >= sizeof targets / sizeof targets[0]) return;
    targets[target].end_us += us; targets[target].count++;
    if (++targets[target].frame_count > targets[target].max_per_frame)
        targets[target].max_per_frame = targets[target].frame_count;
}
void xv_render_profile_work(uint32_t shader, unsigned indices, int no_alpha)
{
    if (!active) return;
    unsigned i;
    for (i = 0; i < work_count; ++i) if (work[i].shader == shader) break;
    if (i == work_count) {
        if (i == sizeof work / sizeof work[0]) { work_overflow++; return; }
        work_count++; work[i].shader = shader;
    }
    work[i].draws++; work[i].indices += indices; work[i].no_alpha += !!no_alpha;
}
void xv_render_profile_cutout(unsigned indices)
{
    if (!active) return;
    cutout_draws++; cutout_indices += indices;
}
void xv_render_profile_depth_only(unsigned indices)
{
    if (!active) return;
    depth_only_draws++; depth_only_indices += indices;
}

void xv_render_profile_begin(uint32_t mesh_frame)
{
    if (enabled < 0) {
        const char *e = getenv("XV_RENDER_PROFILE");
        int traced = &xv_guest_trace_enabled && xv_guest_trace_enabled;
        enabled = (e ? atoi(e) != 0 : traced) && xk_os_monotonic_us;
        if (enabled)
            xv_logf("[render-time] enabled: elapsed pump stages; submit includes API stalls; finish/queue times are waits, not GPU execution times\n");
    }
    if (!enabled) return;
    frame_draws = 0;
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; ++i) targets[i].frame_count = 0;
    if (!frames) first_mesh = mesh_frame;
    last_mesh = mesh_frame;
    current = XV_RENDER_SUBMIT;
    stamp = xk_os_monotonic_us();
    active = 1;
}

void xv_render_profile_stage(enum xv_render_stage stage)
{
    if (!active) return;
    uint64_t now = xk_os_monotonic_us();
    elapsed[current] += now - stamp;
    stamp = now;
    current = stage;
    calls[stage]++;
}

void xv_render_profile_end(void)
{
    if (!active) return;
    elapsed[current] += xk_os_monotonic_us() - stamp;
    active = 0;
    if (!frames || frame_draws < min_draws) min_draws = frame_draws;
    if (frame_draws > max_draws) max_draws = frame_draws;
    over_500 += frame_draws > 500;
    over_800 += frame_draws > 800;
    if (++frames < 60) return;
    uint64_t total = 0;
    for (unsigned i = 0; i < XV_RENDER_STAGE_COUNT; ++i) total += elapsed[i];
    double scale = 1.0 / (1000.0 * frames);
    xv_logf("[render-time] %u frames mesh %u..%u: submit %.3f previous-finish %.3f target-finish %.3f frame-finish %.3f display-queue %.3f retire %.3f total %.3f ms/frame; finish-calls %u/%u/%u queue-calls %u (elapsed pump time)\n",
        frames, first_mesh, last_mesh, elapsed[0]*scale, elapsed[1]*scale,
        elapsed[2]*scale, elapsed[3]*scale, elapsed[4]*scale, elapsed[5]*scale,
        total*scale, calls[XV_RENDER_PREVIOUS_FINISH], calls[XV_RENDER_TARGET_FINISH],
        calls[XV_RENDER_FRAME_FINISH], calls[XV_RENDER_DISPLAY_QUEUE]);
    uint64_t attributed = 0;
    for (unsigned i = 0; i < XV_RENDER_CALL_COUNT; ++i) attributed += call_us[i];
    xv_logf("[render-submit] mesh %u..%u: begin %.3f end %.3f vertex-uniform %.3f fragment-uniform %.3f draw %.3f shader %.3f other %.3f ms/frame; calls %u/%u/%u/%u/%u/%u (subsets of submit; API stalls included)\n",
        first_mesh, last_mesh, call_us[0]*scale, call_us[1]*scale, call_us[2]*scale,
        call_us[3]*scale, call_us[4]*scale, call_us[5]*scale,
        (elapsed[XV_RENDER_SUBMIT] > attributed ? elapsed[XV_RENDER_SUBMIT] - attributed : 0)*scale,
        call_count[0], call_count[1], call_count[2], call_count[3], call_count[4], call_count[5]);
    xv_logf("[render-draws] mesh %u..%u: %u sceGxmDraw calls / %u frames; avg %.2f min %u max %u; frames >500 %u >800 %u (API invocations, including errors)\n",
        first_mesh, last_mesh, call_count[XV_RENDER_DRAW], frames,
        (double)call_count[XV_RENDER_DRAW] / frames, min_draws, max_draws, over_500, over_800);
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; ++i)
        if (targets[i].count)
            xv_logf("[render-target] mesh %u..%u target %u: scenes %u max %u/frame end %.3f ms/frame (subset of EndScene)\n",
                first_mesh, last_mesh, i, targets[i].count, targets[i].max_per_frame, targets[i].end_us*scale);
    /* Most submitted indices first: workload attribution, not a GPU cost rank. */
    for (unsigned n = 0; n < 8 && n < work_count; ++n) {
        unsigned best = work_count;
        for (unsigned i = 0; i < work_count; ++i)
            if (work[i].draws && (best == work_count || work[i].indices > work[best].indices)) best = i;
        if (best == work_count) break;
        xv_logf("[render-work] mesh %u..%u shader %08X: draws %u indices %llu no-alpha %u\n",
            first_mesh, last_mesh, work[best].shader, work[best].draws,
            (unsigned long long)work[best].indices, work[best].no_alpha);
        work[best].indices = 0; work[best].draws = 0;
    }
    if (work_overflow) xv_logf("[render-work] untracked draws %u (128 shader limit)\n", work_overflow);
    xv_logf("[depth-only] mesh %u..%u: %u draws / %llu indices used coverage-equivalent untextured shader\n",
        first_mesh,last_mesh,depth_only_draws,(unsigned long long)depth_only_indices);
    xv_logf("[cutout-work] mesh %u..%u: %u draws / %llu indices use dedicated GREATER alpha test\n",
        first_mesh,last_mesh,cutout_draws,(unsigned long long)cutout_indices);
    cutout_draws=0; cutout_indices=0;
    depth_only_draws=0; depth_only_indices=0;
    frames = 0;
    min_draws = max_draws = over_500 = over_800 = 0;
    memset(elapsed, 0, sizeof elapsed);
    memset(calls, 0, sizeof calls);
    memset(call_us, 0, sizeof call_us); memset(call_count, 0, sizeof call_count);
    memset(work, 0, sizeof work); work_count = work_overflow = 0;
    memset(targets, 0, sizeof targets);
}
