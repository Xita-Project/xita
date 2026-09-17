/* Experimental H2-only replacement of hardware setup by a real synchronous
 * command consumer. Original allocation, DMA objects, RAMHT, state generation
 * and submission remain guest code. Unsupported commands and display work stop. */
#include "log_budget.h"
#include "host_channel_runtime.h"
#include "host_channel.h"
#include "host_tiles.h"
#include "gpu_bus.h"
#include "nv2a_regs.h"
#include "instance_memory.h"
#include "scanout.h"
#include "fp_environment.h"
#if H2_QUAD_RENDER
#include "quad_gxm.h"
#endif
#include <string.h>
#include <stdlib.h>

/* Diagnostic wall-clock accounting for the per-60-flip [h2/perf] line. The time
 * source is weak: absent in the host tests, where nothing is ever reported. */
extern void xv_logf(const char *, ...);
extern uint64_t h2_platform_time_us(void) __attribute__((weak));
static uint64_t perf_now(void) { return h2_platform_time_us ? h2_platform_time_us() : 0; }
static uint64_t perf_submit_us, perf_submits, perf_flip_us, perf_flips_completed, perf_present_us, perf_pass_us;
static h2_host_channel channel;   /* tentative; defined with the runtime state below */
static void perf_report(uint32_t serial)
{
    extern void h2_menu_gxm_perf(uint64_t out[12]) __attribute__((weak));
    extern void h2_audio_backend_perf(uint64_t out[24]) __attribute__((weak));
    extern void xk_wait_stats_request(void) __attribute__((weak));
    static uint64_t last_wall, last[8], last_gxm[12], last_audio[24];
    uint64_t now = perf_now(), cur[8] = {perf_submit_us, perf_submits, perf_flip_us, perf_flips_completed, perf_present_us,
                                         channel.commands.methods, channel.clear.completed_clears, perf_pass_us}, gxm[12] = {0}, audio[24] = {0};
    if (h2_menu_gxm_perf) h2_menu_gxm_perf(gxm);
    if (h2_audio_backend_perf) h2_audio_backend_perf(audio);
    if (now && last_wall) {
#define PD(cur, last, i) ((unsigned long long)((cur)[i] - (last)[i]))
#define PDMS(cur, last, i) ((unsigned long long)(((cur)[i] - (last)[i]) / 1000))
        xv_logf("[h2/perf] serial=%u flips=60 wall_ms=%llu submit_ms=%llu submits=%llu flipwait_ms=%llu completed=%llu present_ms=%llu methods=%llu clears=%llu pass_ms=%llu"
                " | gxm draws=%llu render_ms=%llu flushes=%llu flush_ms=%llu open_ms=%llu tex_ms=%llu gxmdraw_ms=%llu fallbacks=%llu hash_kb=%llu hits=%llu misses=%llu unhashed=%llu"
                " | audio grains=%llu nonzero=%llu compute_ms=%llu max_us=%llu misses=%llu hold_ms=%llu hold_max_us=%llu"
                " compute_iters=%llu idle_iters=%llu guest_locks=%llu guest_wait_ms=%llu error=%08llX"
                " | fx frames=%llu sources_ms=%llu dsp_ms=%llu dsp_instr=%llu | lock sites L%llu=%llu L%llu=%llu L%llu=%llu L%llu=%llu\n",
                serial, (unsigned long long)((now - last_wall) / 1000), PDMS(cur, last, 0), PD(cur, last, 1), PDMS(cur, last, 2), PD(cur, last, 3),
                PDMS(cur, last, 4), PD(cur, last, 5), PD(cur, last, 6), PDMS(cur, last, 7),
                PD(gxm, last_gxm, 0), PDMS(gxm, last_gxm, 1), PD(gxm, last_gxm, 2), PDMS(gxm, last_gxm, 3), PDMS(gxm, last_gxm, 4),
                PDMS(gxm, last_gxm, 5), PDMS(gxm, last_gxm, 6), PD(gxm, last_gxm, 7), (unsigned long long)((gxm[8] - last_gxm[8]) / 1024), PD(gxm, last_gxm, 9), PD(gxm, last_gxm, 10), PD(gxm, last_gxm, 11),
                PD(audio, last_audio, 0), PD(audio, last_audio, 1), PDMS(audio, last_audio, 2), (unsigned long long)audio[3], PD(audio, last_audio, 4),
                PDMS(audio, last_audio, 5), (unsigned long long)audio[6], PD(audio, last_audio, 7), PD(audio, last_audio, 8),
                PD(audio, last_audio, 10), PDMS(audio, last_audio, 9), (unsigned long long)audio[11],
                PD(audio, last_audio, 12), PDMS(audio, last_audio, 13), PDMS(audio, last_audio, 14), PD(audio, last_audio, 15),
                (unsigned long long)audio[16], (unsigned long long)audio[17], (unsigned long long)audio[18], (unsigned long long)audio[19],
                (unsigned long long)audio[20], (unsigned long long)audio[21], (unsigned long long)audio[22], (unsigned long long)audio[23]);
        if (xk_wait_stats_request) xk_wait_stats_request();   /* the kernel dumps [wait] at the next guest yield */
#undef PD
#undef PDMS
    }
    last_wall = now; memcpy(last, cur, sizeof last); memcpy(last_gxm, gxm, sizeof last_gxm); memcpy(last_audio, audio, sizeof last_audio);
}

#define DEVICE 0x404FE0u
#define MINIPORT (DEVICE + 0x1C28u)
#define PHYSICAL_BYTES 0x4000000u
#define BAR 0xFD000000u
static h2_host_channel channel;
static h2_host_tiles tiles;
static xctx *active_context;
static int miniport_ready, channel_ready;
static int software_active, initialization_flip_done, initialization_flip_queued;
static int active_flip_queued, active_flip_retiring;
static uint32_t active_flip_address, active_flip_due, active_flip_serial;
static unsigned active_crtc_writes, active_read_increments;
static uint8_t scanout_gamma[768]; /* interleaved R/G/B DAC entries */
static unsigned gamma_cursor;
const h2_host_channel *h2_host_channel_current(void)
{ return channel_ready ? &channel : NULL; }
static int software_flip(void *opaque, uint32_t value, uint32_t source);
static int channel_idle(void);
static int complete_timed_mode_vblank(xctx *c, uint32_t source);
static int queue_active_flip(uint32_t value, uint32_t source);
static int complete_active_flip(xctx *c, uint32_t source);
#if H2_MENU_RENDER
static void vblank_pace_in_render(void);
#endif
extern void xv_logf(const char *, ...);
extern volatile uint32_t xv_cur_fn;
extern uint32_t xk_mem_arena_size(void);
#if H2_QUAD_RENDER
static h2_quad_draw movie_quad;
#if H2_SCREEN_RENDER
static h2_screen_draw screen_quad;
#endif
#if H2_BC1_RENDER
static h2_bc1_draw bc1_quad;
#endif
#if H2_COMPOSITION_RENDER
static h2_composition_draw composition_quad;
#endif
#if H2_THRESHOLD_RENDER
static h2_threshold_draw threshold_quad;
#endif
#if H2_BLUR_RENDER
static h2_blur_draw blur_quad;
#endif
#if H2_BLEND_RENDER
static h2_blend_draw blend_quad;
#endif
#if H2_LUMA_RENDER
static h2_luma_draw luma_quad;
#endif
#if H2_SPRITE_RENDER
static h2_sprite_draw sprite_quad;
#endif
#if H2_MENU_RENDER
#include "menu_draw.h"
#include "menu_render.h"

static h2_menu_draw menu_quad;
#endif
static int geometry_method(void *opaque, uint8_t sub, uint16_t method,
                            uint32_t value, uint32_t source)
{
    (void)opaque;
    int screen_active = 0, bc1_active = 0, composition_active = 0, threshold_active = 0, blur_active = 0, blend_active = 0, luma_active = 0, sprite_active = 0, menu_active = 0;
#if H2_MENU_RENDER
    menu_active = menu_quad.active;
#endif
#if H2_SPRITE_RENDER
    sprite_active = sprite_quad.active;
#endif
#if H2_LUMA_RENDER
    luma_active = luma_quad.active;
#endif
#if H2_BLEND_RENDER
    blend_active = blend_quad.active;
#endif
#if H2_BLUR_RENDER
    blur_active = blur_quad.active;
#endif
#if H2_THRESHOLD_RENDER
    threshold_active = threshold_quad.active;
#endif
#if H2_COMPOSITION_RENDER
    composition_active = composition_quad.active;
#endif
#if H2_BC1_RENDER
    bc1_active = bc1_quad.active;
#endif
#if H2_SCREEN_RENDER
    screen_active = screen_quad.active;
#endif
    if (!sprite_active && !movie_quad.active && !screen_active && !bc1_active && !composition_active && !threshold_active && !luma_active && !blend_active && !blur_active && !menu_active && method != 0x17FC) return -1;
    uint32_t fpscr = h2_platform_fpscr_read();
    /* Pass time is sampled on every 64th method: per-method clock reads (an HLE
     * call each) cost more than most methods, so the sample is scaled instead. */
    static unsigned perf_sample; uint64_t perf_t0 = (++perf_sample & 63) ? 0 : perf_now();
    uint64_t before = movie_quad.completed;
    int movie_active = movie_quad.active;
    int result = (screen_active || bc1_active || composition_active || threshold_active || blur_active || blend_active || luma_active || sprite_active) ? 0 : h2_quad_method(&movie_quad, &channel.commands, &channel.clear, sub, method, value);
#if H2_SCREEN_RENDER
    if (!sprite_active && !luma_active && !blend_active && !blur_active && !threshold_active && !composition_active && !bc1_active && (screen_active || (!movie_active && !result))) {
        uint64_t screen_before = screen_quad.completed;
        result = h2_screen_method(&screen_quad, &channel.commands, &channel.clear, sub, method, value);
        if (screen_before != screen_quad.completed)
            xv_logf("[h2/screen] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)screen_quad.completed, source, channel.clear.color_offset);
    }
#else
    (void)movie_active;
#endif
#if H2_BC1_RENDER
    if (!sprite_active && !luma_active && !blend_active && !blur_active && !threshold_active && !composition_active && (bc1_active || (!movie_active && !screen_active && !result))) {
        uint64_t bc1_before = bc1_quad.completed;
        result = h2_bc1_method(&bc1_quad, &channel.commands, &channel.clear, sub, method, value);
        if (bc1_before != bc1_quad.completed)
            xv_logf("[h2/bc1] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)bc1_quad.completed, source, channel.clear.color_offset);
    }
#endif
#if H2_COMPOSITION_RENDER
    if (!sprite_active && !luma_active && !blend_active && !blur_active && !threshold_active && (composition_active || (!movie_active && !screen_active && !bc1_active && !result))) {
        uint64_t composition_before = composition_quad.completed;
        result = h2_composition_method(&composition_quad, &channel.commands, &channel.clear, sub, method, value);
        if (composition_before != composition_quad.completed)
            xv_logf("[h2/composition] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)composition_quad.completed, source, channel.clear.color_offset);
    }
#endif
#if H2_THRESHOLD_RENDER
    if (!sprite_active && !luma_active && !blend_active && !blur_active && (threshold_active || (!movie_active && !screen_active && !bc1_active && !composition_active && !result))) {
        uint64_t threshold_before = threshold_quad.completed;
        result = h2_threshold_method(&threshold_quad, &channel.commands, &channel.clear, sub, method, value);
        if (threshold_before != threshold_quad.completed)
            xv_logf("[h2/threshold] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)threshold_quad.completed, source, channel.clear.color_offset);
    }
#endif
#if H2_BLUR_RENDER
    if (!sprite_active && !luma_active && !blend_active && (blur_active || (!movie_active && !screen_active && !bc1_active && !composition_active && !threshold_active && !result))) {
        uint64_t blur_before = blur_quad.completed;
        result = h2_blur_method(&blur_quad, &channel.commands, &channel.clear, sub, method, value);
        if (blur_before != blur_quad.completed)
            xv_logf("[h2/blur] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)blur_quad.completed, source, channel.clear.color_offset);
    }
#endif
#if H2_BLEND_RENDER
    if (!sprite_active && !luma_active && (blend_active || (!movie_active && !screen_active && !bc1_active && !composition_active && !threshold_active && !blur_active && !result))) {
        uint64_t blend_before = blend_quad.completed;
        result = h2_blend_method(&blend_quad, &channel.commands, &channel.clear, sub, method, value);
        if (blend_before != blend_quad.completed)
            xv_logf("[h2/blend] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)blend_quad.completed, source, channel.clear.color_offset);
    }
#endif
#if H2_LUMA_RENDER
    if (!sprite_active && (luma_active || (!movie_active && !screen_active && !bc1_active && !composition_active && !threshold_active && !blur_active && !blend_active && !result))) {
        uint64_t luma_before = luma_quad.completed;
        result = h2_luma_method(&luma_quad, &channel.commands, &channel.clear, sub, method, value);
        if (luma_before != luma_quad.completed)
            xv_logf("[h2/luma] completed=%u original_vertices=4 source=%08X color=%08X RGBA committed; not yet presented\n",
                    (unsigned)luma_quad.completed, source, channel.clear.color_offset);
    }
#endif
#if H2_SPRITE_RENDER
    if (sprite_active || (!movie_active && !screen_active && !bc1_active && !composition_active && !threshold_active && !blur_active && !blend_active && !luma_active && !result)) {
        uint64_t sprite_before=sprite_quad.completed;
        result = h2_sprite_method(&sprite_quad,&channel.commands,&channel.clear,sub,method,value);
        if(sprite_before!=sprite_quad.completed)
            xv_logf("[h2/sprite] completed=%u original_inline_vertices=4 source=%08X color=%08X RGB committed; not yet presented\n",
                    (unsigned)sprite_quad.completed,source,channel.clear.color_offset);
    }
#endif
#if H2_MENU_RENDER
    /* Lowest-priority general geometry: claims the BEGIN_END draws the pinned
     * intro modules reject. Assembles vertices/indices and renders through the
     * menu backend; with no backend the draw rejects and the run still stops. */
    if (menu_active || (!movie_active && !screen_active && !bc1_active && !composition_active &&
                        !threshold_active && !blur_active && !blend_active && !luma_active &&
                        !sprite_active && !result)) {
        uint64_t menu_done = menu_quad.completed, menu_rej = menu_quad.rejected;
        int menu_result = h2_menu_method(&menu_quad, &channel.commands, &channel.clear, sub, method, value);
        if (menu_result == -1)
            /* Interleaved state inside an active menu draw: apply it to command
             * state and keep the draw open (the draw reads it at END). */
            menu_result = h2_command_method(&channel.commands, &channel.clear, sub, method, value, source) ? 1 : 0;
        if (menu_result != -1) result = menu_result;
        /* One line per completed draw was ~740 lines per menu frame; keep the first
         * 200 and then every 100th (the renderer logs its own sampled detail). */
        if (menu_done != menu_quad.completed) vblank_pace_in_render();
        if (menu_done != menu_quad.completed && (menu_quad.completed <= 200 || !(menu_quad.completed % 100)))
            xv_logf("[h2/menu] completed=%u primitive=%u vertices=%u indices=%u arrays=%u source=%08X color=%08X committed; not yet presented\n",
                    (unsigned)menu_quad.completed, menu_quad.primitive, menu_quad.vertex_count,
                    menu_quad.index_count, menu_quad.array_count, source, channel.clear.color_offset);
        else if (menu_rej != menu_quad.rejected)
            xv_logf("[h2/menu] rejected=%u primitive=%u vertices=%u indices=%u arrays=%u overflow=%u source=%08X; draw not backed\n",
                    (unsigned)menu_quad.rejected, menu_quad.primitive, menu_quad.vertex_count,
                    menu_quad.index_count, menu_quad.array_count, menu_quad.overflow, source);
    }
#endif
#if H2_COMPOSITION_RENDER
    if (!sprite_active && !movie_active && !screen_active && !bc1_active && !threshold_active && !luma_active && !blend_active && !blur_active && !result && method == 0x17FC && value == 7) {
        xv_logf("[h2/composition] rejected BEGIN contract=%u render=%u diagnostic=%u\n",
                composition_quad.contract != NULL, composition_quad.render != NULL,
                h2_composition_probe(&composition_quad, &channel.commands, &channel.clear));
        /* The original descriptors and tracked tiles explain a resource
         * rejection; reading them does not grant additional permissions. */
        const uint32_t instances[] = {channel.commands.dma[1], channel.clear.dma_color, channel.clear.dma_zeta};
        for (unsigned i = 0; i < 3 && channel.clear.read_instance; ++i) {
            uint32_t words[4] = {0}; unsigned valid = 0;
            for (unsigned k = 0; k < 4; ++k)
                if (channel.clear.read_instance(channel.clear.opaque, instances[i] + k * 4, &words[k])) valid |= 1u << k;
            xv_logf("[h2/composition] dma%u instance=%08X valid=%X words=%08X,%08X,%08X,%08X\n",
                    i, instances[i], valid, words[0], words[1], words[2], words[3]);
        }
        for (unsigned i = 0; i < 8; ++i) if (tiles.entries[i].enabled) {
            const h2_host_tile *t = &tiles.entries[i];
            xv_logf("[h2/composition] tile%u address=%08X bytes=%08X pitch=%u flags=%08X\n",
                    i, t->address, t->bytes, t->pitch, t->flags);
        }
    }
#endif
    if (movie_quad.completed != before && (movie_quad.completed <= 4 || !(movie_quad.completed % 60)))
        xv_logf("[h2/quad] completed=%u original_vertices=4 source=%08X color=%08X texture=%08X RGB committed; not yet presented\n",
                (unsigned)movie_quad.completed, source, channel.clear.color_offset,
                channel.commands.setup[0x1B00 / 4]);
    if (perf_t0) perf_pass_us += (perf_now() - perf_t0) * 64;
    h2_platform_fpscr_write(fpscr);
    return result;
}
#endif
extern void f_003FE165(xctx *), f_003FE190(xctx *);
extern void f_00401C33(xctx *), f_00401D96(xctx *);
extern void f_003FF240(xctx *), f_003FECC0(xctx *), f_0012B2A0(xctx *);
extern void __real_xk_KeWaitForSingleObject(xctx *), xk_KeSetEvent(xctx *);

static void reject(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{
    h2_graphics_stop(c, ip, address, value, 1, H2_NV2A_UNSUPPORTED_OPERATION);
}
static int guest_span_valid(uint32_t address, uint32_t bytes)
{
    uint32_t arena = xk_mem_arena_size();
    if (!bytes || arena < 4096 || (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t page = address >> 12; page <= last; ++page) {
        uint32_t offset = g_xpt[page];
        if ((offset & 4095) || (uint64_t)offset + 4096 > arena - 4096) return 0;
    }
    return 1;
}
static void check_stack(xctx *c, uint32_t ip, unsigned args)
{
    if ((c->r[4] & 3) || !guest_span_valid(c->r[4], 4 + args * 4))
        reject(c, ip, c->r[4], args);
}
static void call_guest(xctx *c, void (*function)(xctx *), uint32_t ip)
{
    uint32_t stack = c->r[4];
    if (stack < 256 || !guest_span_valid(stack - 256, 256)) reject(c, ip, stack, 256);
    X_PUSH32(ip);
    function(c);
    if (c->r[4] != stack) reject(c, ip, c->r[4], stack);
}
static int game_vblank_inputs_valid(void)
{
    return guest_span_valid(0x485AB0, 16) && guest_span_valid(0x55E6B8, 4) &&
           guest_span_valid(0x4E6400, 32) && X_M16(0x4E6400) < 15 &&
           X_M32(MINIPORT + 0x190) == 0x12B2A0;
}
static void signal_game_vblank(xctx *c, uint32_t count, uint32_t swaps, uint32_t flags,
                                uint32_t source)
{
    /* Callers validate the complete stack and fixed original callback data
     * before waiting or mutating anything. KeSetEvent precedes the callback. */
    uint64_t before = X_M32(0x485AB0) | (uint64_t)X_M32(0x485AB4) << 32;
    xctx interrupt = *c; interrupt.r[4] -= 16;
    X_W32(interrupt.r[4]) = 0x3FEE5E;
    X_W32(interrupt.r[4] + 4) = MINIPORT + 0x194;
    X_W32(interrupt.r[4] + 8) = 1; X_W32(interrupt.r[4] + 12) = 0;
    xk_KeSetEvent(&interrupt);
    if (interrupt.r[4] != c->r[4]) reject(c, source, interrupt.r[4], c->r[4]);
    interrupt = *c; interrupt.preempt = 100000; interrupt.r[4] -= 12;
    uint32_t record = interrupt.r[4];
    X_W32(record) = count; X_W32(record + 4) = swaps; X_W32(record + 8) = flags;
    interrupt.r[4] -= 4; X_W32(interrupt.r[4]) = record;
    call_guest(&interrupt, f_0012B2A0, 0x3FEE87);
    uint64_t after = X_M32(0x485AB0) | (uint64_t)X_M32(0x485AB4) << 32;
    if (after != before + 1) reject(c, source, 0x485AB0, (uint32_t)after);
    /* At real-time pacing this is 30 lines/s: keep the first 200 and every 100th. */
    if (after <= 200 || !(after % 100))
        xv_logf("[h2/vblank] original callback=0012B2A0 record=%u,%u,%u game_count=%llu->%llu\n",
                count, swaps, flags, (unsigned long long)before, (unsigned long long)after);
}
static uint32_t freerun_swaps;    /* free-running vblank swap count shared by both pacing paths */
static void *map_physical_raw(uint32_t address, uint32_t bytes)
{
    if (!bytes || address >= PHYSICAL_BYTES || bytes > PHYSICAL_BYTES - address) return NULL;
    /* Verify the entire cached physical alias and its host contiguity. This
     * excludes the separately mapped XBE image and the unmapped trash page. */
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t page = address >> 12; page <= last; ++page)
        if (g_xpt[0x80000u + page] != page * 4096u) return NULL;
    return g_xram + address;
}
extern void xv_mark_written(const void *host, uint32_t bytes) __attribute__((weak));
extern uint32_t xv_write_epoch __attribute__((weak));
static void *map_physical(void *opaque, uint32_t address, uint32_t bytes)
{
    (void)opaque;
    if (!h2_host_tiles_span(&tiles, address, bytes)) return NULL;
    void *pointer = map_physical_raw(address, bytes);
    /* Consumers may write through this mapping: stamp the span now (the
     * multi-method consumers stamp again when they actually store). */
    if (pointer && xv_mark_written) xv_mark_written(pointer, bytes);
    return pointer;
}
/* The texture reader's mapping: a read that must not stamp its own source. */
void *h2_map_physical_read(uint32_t address, uint32_t bytes)
{
    if (!h2_host_tiles_span(&tiles, address, bytes)) return NULL;
    return map_physical_raw(address, bytes);
}
static int read_physical(void *opaque, uint32_t address, uint32_t *word)
{
    void *pointer = map_physical(opaque, address, 4);
    if (!pointer) return 0;
    memcpy(word, pointer, 4);
    return 1;
}
static int check_attachment(void *opaque, uint32_t address, uint32_t bytes,
                              uint32_t pitch, int zeta, uint32_t format)
{
    (void)opaque;
    return h2_host_tiles_attachment(&tiles, address, bytes, pitch, zeta, format);
}
static int read_instance(void *opaque, uint32_t offset, uint32_t *word)
{
    (void)opaque;
    if ((offset & 3) || offset < 0x10000u ||
        (uint64_t)offset + 4 > 0x10000u + h2_instance_bytes()) return 0;
    uint32_t physical = (PHYSICAL_BYTES - 64 - (offset & ~63u)) | (offset & 63u);
    if (!map_physical_raw(physical, 4)) return 0;
    *word = h2_bus_read32(active_context, 0, BAR + 0x700000u + offset);
    return 1;
}
static void instance_write(xctx *c, uint32_t offset, uint32_t value)
{ h2_bus_write32(c, 0x4026CEu, BAR + 0x700000u + offset, value); }

void h2_host_miniport_init(xctx *c)
{
    check_stack(c, 0x3FE005u, 0);
    uint32_t mini = c->r[0], saved[8];
    memcpy(saved, c->r, sizeof saved);
    if (miniport_ready || mini != MINIPORT || X_M32(0x407488u) != DEVICE)
        reject(c, 0x3FE005u, mini, miniport_ready);
    /* Keep the driver's software DPC/list/gamma state. This backend executes
     * commands synchronously; it does not register physical NV2A interrupts. */
    X_W32(mini + 0x88) = 0x3FEBE0u;
    X_W32(mini + 0x8C) = mini;
    X_W32(mini + 0x1AC) = X_W32(mini + 0x1B0) = mini + 0x1AC;
    X_W32(mini + 0x19C) = X_W32(mini + 0x1A0) = mini + 0x19C;
    X_W8(mini + 0x1A4) = X_W8(mini + 0x194) = 0;
    X_W8(mini + 0x1A6) = X_W8(mini + 0x196) = 4;
    X_W32(mini + 0x1A8) = X_W32(mini + 0x198) = 1;
    c->r[0] = mini; call_guest(c, f_003FE165, 0x3FE070u);
    if (!c->r[0]) reject(c, 0x3FE005u, mini, 0);
    c->r[0] = mini; call_guest(c, f_003FE190, 0x3FE082u);
    if (!c->r[0]) reject(c, 0x3FE005u, mini, 0);
    X_W32(mini + 0x164) = 0x3FF490u;
    X_W32(mini + 0x168) = 0;
    h2_bus_write32(c, 0x3FE1D9u, BAR + 0x1830, 0);
    h2_bus_write32(c, 0x3FE1E3u, BAR + 0x180C, 0xF800);
    c->r[6] = mini; call_guest(c, f_00401C33, 0x3FE1F2u);
    X_W32(mini + 0xB4) = 1;
    h2_bus_write32(c, 0x3FE1FCu, BAR + 0x9200, 0xDE86);
    h2_bus_write32(c, 0x3FE206u, BAR + 0x9210, 0x1DCD);
    h2_bus_write32(c, 0x3FE210u, BAR + 0x9420, UINT32_MAX);
    c->r[6] = mini; call_guest(c, f_00401D96, 0x3FE21Fu);
    if (h2_instance_bytes() != 0x5000 || X_M32(mini + 0x130) != 0x710000 ||
        X_M32(mini + 0x128) != 0x711000 || X_M32(mini + 0x140) != 0x110A)
        reject(c, 0x3FE005u, mini, h2_instance_bytes());
    X_W32(mini + 0x134) = 2;
    instance_write(c, X_M32(mini + 0x140) * 16, 0);
    instance_write(c, X_M32(mini + 0x140) * 16 + 4, 0);
    X_W32(mini + 0x120) = 0xFF;
    X_W32(mini + 0x124) = 0x800000;
    X_W32(mini + 0x11C) = 0x1111111;
    for (unsigned table = 0; table < 6; ++table)
        for (unsigned i = 0; i < 256; ++i)
            X_W8(mini + 0x1DC + table * 256 + i) = (uint8_t)i;
    /* The virtual device has a command consumer but no active display/IRQ. */
    X_W32(mini + 0xA0) = 1;
    miniport_ready = 1;
    xv_logf("[h2/channel] virtual miniport initialized, instance=%08X; display/IRQ unsupported\n",
            h2_instance_bytes());
    memcpy(c->r, saved, sizeof saved);
    c->r[0] = 1;
    X_RET(0);
}

void h2_host_channel_configure(xctx *c)
{
    check_stack(c, 0x4026CEu, 3);
    uint32_t mini = c->r[2], descriptor = X_ARG(2);
    if (!miniport_ready || channel_ready || mini != MINIPORT || X_M32(mini + 0x100))
        reject(c, 0x4026CEu, mini, channel_ready);
    if ((descriptor & 3) || !guest_span_valid(descriptor, 16)) reject(c, 0x4026CEu, descriptor, 16);
    active_context = c;
    uint32_t context = X_M32(mini + 0x10C), dma_instance = X_M32(descriptor + 12);
    uint32_t ring = X_M32(DEVICE + 0x24), end = X_M32(DEVICE + 0x28);
    h2_dma_object dma;
    if (ring < 0x80000000u || end <= ring || end > 0x84000000u ||
        context < 0x1112u || (uint64_t)context * 16 + 0x37F0 > 0x15000u ||
        X_M32(mini + 0x160) != context + 0x37Fu ||
        X_M32(mini + 0x128) != 0x711000u || X_M32(mini + 0x140) != 0x110Au ||
        dma_instance < 0x1112u || dma_instance >= context ||
        !map_physical_raw(ring - 0x80000000u, end - ring) ||
        !map_physical_raw(PHYSICAL_BYTES - 0x10000u - h2_instance_bytes(), h2_instance_bytes()) ||
        !h2_dma_load(read_instance, NULL, dma_instance * 16, &dma) ||
        !h2_host_channel_init(&channel, &dma, ring - 0x80000000u, end - ring,
                              read_physical, read_instance, map_physical, NULL, PHYSICAL_BYTES))
        reject(c, 0x4026CEu, descriptor, dma_instance);
    for (uint32_t i = 0; i < 0x37F0; i += 4) instance_write(c, context * 16 + i, 0);
    instance_write(c, X_M32(mini + 0x140) * 16, context);
    X_W32(mini + 0x138) = context;
    uint32_t ramfc = X_M32(mini + 0x128) - 0x700000u;
    for (unsigned i = 0; i < 64; i += 4) instance_write(c, ramfc + i, 0);
    instance_write(c, ramfc + 12, dma_instance);
    uint32_t burst = X_ARG(0), time = X_ARG(1), priority = c->r[0];
    if (burst < 8) burst = 8;
    if (burst > 256) burst = 256;
    if (time < 32) time = 32;
    if (time > 256) time = 256;
    if (priority > 15) priority = 15;
    uint32_t schedule = ((((priority << 3) | ((time >> 5) - 1)) << 10) | ((burst >> 3) - 1)) << 3;
    instance_write(c, ramfc + 20, schedule);
    X_M32(mini + 0x104) |= 1;
    X_M32(mini + 0x108) |= 1;
    channel_ready = 1;
    channel.clear.check_attachment = check_attachment;
    channel.software_flip = software_flip;
#if H2_QUAD_RENDER
    memset(&movie_quad, 0, sizeof movie_quad);
    uint32_t contract_fpscr = h2_platform_fpscr_read();
    movie_quad.contract = h2_quad_gxm_contract();
#if H2_SCREEN_RENDER
    memset(&screen_quad, 0, sizeof screen_quad);
    screen_quad.contract = h2_screen_gxm_contract();
    screen_quad.render = h2_screen_gxm_render;
#endif
#if H2_BC1_RENDER
    memset(&bc1_quad, 0, sizeof bc1_quad);
    bc1_quad.contract = h2_bc1_gxm_contract();
    bc1_quad.render = h2_bc1_gxm_render;
#endif
#if H2_COMPOSITION_RENDER
    memset(&composition_quad, 0, sizeof composition_quad);
    composition_quad.contract = h2_composition_gxm_contract();
    composition_quad.render = h2_composition_gxm_render;
#endif
#if H2_THRESHOLD_RENDER
    memset(&threshold_quad, 0, sizeof threshold_quad);
    threshold_quad.contract = h2_threshold_gxm_contract();
    threshold_quad.render = h2_threshold_gxm_render;
#endif
#if H2_BLUR_RENDER
    memset(&blur_quad, 0, sizeof blur_quad);
    blur_quad.contract = h2_blur_gxm_contract();
    blur_quad.render = h2_blur_gxm_render;
#endif
#if H2_BLEND_RENDER
    memset(&blend_quad, 0, sizeof blend_quad);
    blend_quad.contract = h2_blend_gxm_contract();
    blend_quad.render = h2_blend_gxm_render;
#endif
#if H2_SPRITE_RENDER
    memset(&sprite_quad,0,sizeof sprite_quad);
    sprite_quad.contract=h2_sprite_gxm_contract();
    sprite_quad.render=h2_sprite_gxm_render;
#endif
#if H2_MENU_RENDER
    memset(&menu_quad, 0, sizeof menu_quad);
    menu_quad.render = h2_menu_software_render; /* software transform + rasterize into back buffer */
#endif
#if H2_LUMA_RENDER
    memset(&luma_quad, 0, sizeof luma_quad);
    luma_quad.contract = h2_luma_gxm_contract();
    luma_quad.render = h2_luma_gxm_render;
#endif
    h2_platform_fpscr_write(contract_fpscr);
    movie_quad.render = h2_quad_gxm_render;
    channel.geometry_method = geometry_method;
#endif
    xv_logf("[h2/channel] configured channel=0 DMA=%08X context=%08X ring=%08X+%X schedule=%08X\n",
            dma_instance, context, channel.stream.base, channel.stream.bytes, schedule);
    X_RET(3);
}

void h2_host_memory_barrier(xctx *c)
{
    check_stack(c, 0x3FADE0u, 0);
    if (!miniport_ready) reject(c, 0x3FADE0u, 0, 0);
    /* The synchronous CPU consumer shares normal host RAM with generated code. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    X_RET(0);
}
static h2_host_av_config av_config;
static unsigned mode_vblanks;
static int display_mode_set, screen_blanked;
void __wrap_xk_KeWaitForSingleObject(xctx *c)
{
    check_stack(c, 0, 5);
    if (X_ARG(0) != MINIPORT + 0x194) { __real_xk_KeWaitForSingleObject(c); return; }
    uint32_t ip = X_M32(c->r[4]), event = MINIPORT + 0x194;
    if (mode_vblanks == 1) {
        if (ip != 0x3F9BF7 || X_ARG(1) != 6 || X_ARG(2) != 1 || X_ARG(3) || X_ARG(4) ||
            !complete_timed_mode_vblank(c, ip)) reject(c, ip, event, 0);
        __real_xk_KeWaitForSingleObject(c);
        return;
    }
    /* Audited first mode-transition wait, before scanout is enabled. Service
     * one real host vblank; general IRQs, callbacks and asynchronous flips are
     * still unsupported. Other kernel waits use the original implementation. */
    if (!initialization_flip_done || mode_vblanks || !channel_idle() ||
        ip != 0x3F9BF7 || X_ARG(1) != 6 || X_ARG(2) != 1 || X_ARG(3) || X_ARG(4) ||
        c->r[4] < 16 || !guest_span_valid(c->r[4] - 16, 16) ||
        !guest_span_valid(MINIPORT, 0x1DC) || X_M32(event) != 0x00040000 ||
        X_M32(event + 4) || X_M32(MINIPORT + 0x1B8) != 1 || X_M32(MINIPORT + 0x190) ||
        X_M32(MINIPORT + 0x1BC) != 1 || X_M32(MINIPORT + 0x1CC) != 1 ||
        X_M32(MINIPORT + 0x174) || X_M32(MINIPORT + 0x180) ||
        X_M32(MINIPORT + 0x1C0) || X_M32(MINIPORT + 0x1C4) ||
        X_M32(MINIPORT + 0x1D4) || X_M32(MINIPORT + 0x1D8) ||
        X_M32(MINIPORT + 8) != 0x88070701) reject(c, ip, event, 0);
    uint32_t before, after;
    int status = h2_platform_wait_vblank(&before, &after);
    if (status < 0 || before == after) reject(c, ip, event, (uint32_t)status);
    /* The driver's empty-queue progressive vblank path increments its count,
     * records the same RDTSC timebase and signals this notification event. */
    X_W32(MINIPORT + 0x1C0) = 1;
    X_W32(MINIPORT + 0x1D8) = (uint32_t)x_rdtsc();
    X_W32(MINIPORT + 0x1D0) = 0;
    xctx signal = *c; signal.r[4] -= 16;
    X_W32(signal.r[4]) = 0x3FEE5E;
    X_W32(signal.r[4] + 4) = event; X_W32(signal.r[4] + 8) = 1; X_W32(signal.r[4] + 12) = 0;
    xk_KeSetEvent(&signal);
    if (signal.r[4] != c->r[4]) reject(c, ip, signal.r[4], c->r[4]);
    ++mode_vblanks;
    xv_logf("[h2/vblank] real Vita vcount=%u->%u delivered event=%08X; scanout disabled\n", before, after, event);
    __real_xk_KeWaitForSingleObject(c);
}
h2_host_av_config h2_host_av_configuration(void) { return av_config; }
void __wrap_xk_AvSendTVEncoderOption(xctx *c)
{
    check_stack(c, 0, 4);
    uint32_t base = X_ARG(0), option = X_ARG(1), param = X_ARG(2), output = X_ARG(3);
    if (!miniport_ready || (base && base != BAR))
        reject(c, X_M32(c->r[4]), base, option);
    if (option == 6 && !param && output && !(output & 3) && guest_span_valid(output, 4)) {
        /* Virtual NTSC-M, 60 Hz, normal aspect, HDTV 480p capabilities. */
        X_W32(output) = 0x00480104u;
        xv_logf("[h2/av] virtual AV capabilities=00480104 output=%08X; no display mode applied\n", output);
    } else if (option == 11 && !output && (param == 5 || (display_mode_set && param <= 5))) {
        av_config.flicker_filter = param; av_config.has_flicker = 1;
        xv_logf("[h2/av] retained flicker-filter request=%u; inactive for selected progressive scanout\n", param);
    } else if (option == 14 && !output && (!param || (display_mode_set && param == 1))) {
        /* Retain the Boolean analog soft-filter preference. The implemented
         * output is digital progressive RGB, before analog TV encoding. See
         * Conexant 100381B sections 1.3.45 and E.9 for the HDTV/VGA DAC path;
         * no SD luma filtering or analog reconstruction is simulated here. */
        av_config.luma_filter = param; av_config.has_luma = 1;
        xv_logf("[h2/av] retained analog luma-filter request=%u; digital progressive output\n", param);
    } else if (option == 15 && display_mode_set && base == BAR && !param &&
               output && !(output & 3) && guest_span_valid(output, 4)) {
        /* The only implemented scanout is progressive: a single field, index
         * zero. This is not an analog encoder/interlaced-field approximation. */
        X_W32(output) = 0;
        xv_logf("[h2/av] progressive field index=0 output=%08X\n", output);
    } else if (option == 9 && display_mode_set && base == BAR && param <= 1 && !output) {
        if (screen_blanked != (int)param) {
            int result = h2_platform_blank(param);
            if (result < 0) reject(c, X_M32(c->r[4]), base, (uint32_t)result);
            screen_blanked = param;
        }
        xv_logf("[h2/av] real progressive display blank=%u\n", param);
    } else reject(c, X_M32(c->r[4]), output, option);
    X_RET(4);
}
void __wrap_xk_AvSetDisplayMode(xctx *c)
{
    check_stack(c, 0, 6);
    uint32_t ip = X_M32(c->r[4]), address = X_ARG(5);
    if (!initialization_flip_done || (mode_vblanks != 1 && mode_vblanks != 2) ||
        display_mode_set || !channel_idle() ||
        X_ARG(0) != BAR || X_ARG(1) || X_ARG(2) != 0x88070701 || X_ARG(3) != 0x12 || X_ARG(4) != 2560 ||
        (address & 15) || gamma_cursor != 768 || !av_config.has_flicker || av_config.flicker_filter != 5 ||
        !av_config.has_luma || av_config.luma_filter ||
        !check_attachment(NULL, address, H2_SCANOUT_BYTES, 2560, 0, 0x128))
        reject(c, ip, address, X_ARG(2));
    const uint8_t *pixels = map_physical_raw(address, H2_SCANOUT_BYTES);
    if (!pixels) reject(c, ip, address, H2_SCANOUT_BYTES);
    uint32_t vcount;
    int result = h2_platform_present(pixels, H2_SCANOUT_BYTES, scanout_gamma, &vcount);
    if (result < 0) reject(c, ip, address, (uint32_t)result);
    display_mode_set = 1;
    screen_blanked = 0;
    xv_logf("[h2/display] presented linear ARGB8 address=%08X 640x480 pitch=2560 vcount=%u; progressive gamma scanout, interlaced flicker filter inactive, luma filter disabled\n",
            address, vcount);
    c->r[0] = 0; X_RET(6);
}

static int channel_idle(void)
{ return channel_ready && !channel.bootstrap && !channel.stream.remaining && channel.put == channel.stream.get; }

/* Halo CE implements xd3d_vblank_kick in xd3d.c to advance the frame-pacing vblank when the game yields
 * its wait loop; xd3d.c is not linked into the host-channel (H2) build, so the weak hook in
 * xk_NtYieldExecution / xk_wait was inert here. Post-intro the game advances its 64-bit frame counter
 * (0x485AB0 - the engine clock the menu build and its resource pacing read all over 0x12Bxxx) ONLY
 * through the flip-completion vblank. At the menu the game submits no flip, so the counter freezes and
 * the frame-paced loop (and the streaming/task handshake it drives) deadlocks. A real display's vblank
 * fires regardless of flips; mirror that with a real-time-paced free-running vblank - but only once the
 * counter has actually stalled with the channel idle and no flip in flight, so the intro (which flips
 * every frame and advances the counter that way) is untouched. Opt-in (XV_MENU_VBLANK=1) while under
 * test so default builds are byte-identical. */
int xd3d_vblank_kick(xctx *c, uint32_t eip)
{
    extern uint64_t xk_os_monotonic_us(void);
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_MENU_VBLANK"); on = e ? atoi(e) : 0; }
    if (!on || !c || !initialization_flip_done || active_flip_queued || software_active || !channel_idle())
        return 0;
    static uint64_t frozen_us; static uint32_t seen_count;
    uint64_t now = xk_os_monotonic_us();
    uint32_t count = X_M32(0x485AB0);
    if (count != seen_count) { seen_count = count; frozen_us = now; return 0; } /* counter still advancing */
    if (!frozen_us || now - frozen_us < 33000u) return 0;                       /* only once stalled ~2 frames */
    frozen_us = now;
    signal_game_vblank(c, count + 1u, ++freerun_swaps, 1u, eip);
    return 1;
}
/* While the guest thread is inside the software menu renderer nothing yields, so
 * xd3d_vblank_kick (hooked at yield/wait sites) never fires and game time crawled
 * at ~1/80 real time (438 vblanks in a 15-minute run). Deliver the original vblank
 * callback at real-time 60 Hz from between draws as well, as the hardware
 * interrupt would mid-frame. Same gate (XV_MENU_VBLANK), same callback and record. */
#if H2_MENU_RENDER
uint64_t h2_menu_yield_us, h2_menu_yields;   /* time handed to other guest fibers from the render tick */
static void vblank_pace_in_render(void)
{
    extern uint64_t xk_os_monotonic_us(void);
    static int on = -1, busy;
    static uint64_t last_us;
    if (on < 0) { const char *e = getenv("XV_MENU_VBLANK"); on = e ? atoi(e) : 0; }
    if (!on || busy || !active_context || !initialization_flip_done) return;
    uint64_t now = xk_os_monotonic_us();
    if (!last_us) last_us = now;
    /* The vblank interrupt is a real-time 60 Hz event: deliver one callback per elapsed 16.667 ms,
     * catching up after stretches with no hook (the game's own CPU work between frames, GXM flush).
     * Measured before this (run sig274): ~9 kicks/s -> the game's UI clock (dt = vblanks/60 at
     * 0x12B0E0) ran at 0.12-0.15 of wall time and every menu transition was 7-8x too slow.
     * Capped per hook so a long stall cannot flood the handler; the remainder carries over. */
    uint64_t elapsed = now - last_us;
    unsigned pending = (unsigned)(elapsed / 16667u);
    if (!pending) return;
    if (pending > 120u) { last_us = now - (elapsed - 120u * 16667u) % 16667u; pending = 120u; }
    else last_us += (uint64_t)pending * 16667u;
    busy = 1;
    /* The handler (0x14280) always increments the frame counter and treats a
     * record whose swap count equals its last-seen value (ds:[0x55E6B8]) as a
     * plain vblank; flags are not read. Pass the game's own last-seen swap count
     * so no swap is fabricated while a real flip is queued. */
    for (unsigned i = 0; i < pending; ++i) {
        uint32_t count = X_M32(0x485AB0);
        signal_game_vblank(active_context, count + 1u, X_M32(0x55E6B8), 0u, 0x3FAC58u);
    }
    /* Guest threads are cooperative fibers: nothing else runs while this fiber
     * is inside the rasterizer, and the next menu screen streams its resources
     * on the loader thread. Hand the scheduler a turn once per vblank. */
    static int yield_every = -1, yield_burst = -1;
    static unsigned ticks;
    if (yield_every < 0) { const char *e = getenv("XV_MENU_YIELD"); yield_every = e ? atoi(e) : 1; }
    if (yield_burst < 0) { const char *e = getenv("XV_MENU_YIELD_BURST"); yield_burst = e ? atoi(e) : 8; }
    if (yield_every > 0 && !(++ticks % (unsigned)yield_every)) {
        /* One yield = one scheduler round = one step of each other fiber. The
         * menu's map-cache copy task reads 0x800 bytes per step, so a single
         * round per vblank capped it at ~30 reads/s; give it a burst of rounds. */
        extern void xk_yield(void);
        xctx *saved = active_context;
        uint64_t t0 = xk_os_monotonic_us();
        for (int i = 0; i < (yield_burst > 0 ? yield_burst : 1); ++i) xk_yield();
        h2_menu_yield_us += xk_os_monotonic_us() - t0; ++h2_menu_yields;
        active_context = saved;
    }
    busy = 0;
}
/* Preemption hook (xv_preempt calls it on every cooperative slice of any fiber). The vblank
 * interrupt is real time on hardware; between the menu's draw hooks and the kernel yield hooks
 * nothing advanced the counter while the game ran pure CPU code, so a multiplayer level load
 * spun forever in the D3D vblank wait (0x12B2E0: busy-loop on 0x485AB0 until the next count,
 * run mp283). Deliver the same 60 Hz catch-up here, only on the fiber that owns the game's
 * D3D context (the callback runs as a nested guest call on that context). */
void xd3d_lockstep_preempt(xctx *c)
{
    if (c && c == active_context) vblank_pace_in_render();
}
/* Rasterizer row hook (calling thread only): big triangles and full-screen
 * passes take hundreds of ms each, so pacing only between draws missed most
 * vblanks (7 Hz); this keeps game time at real rate inside them too. */
void h2_menu_render_tick(void) { vblank_pace_in_render(); }
#endif
void h2_host_miniport_shutdown(xctx *c)
{
    const uint32_t ip = 0x3FE4CBu, mini = c->r[0];
    check_stack(c, ip, 0);
    /* Paired with our virtual miniport initialization. The original caller
     * already flushed and blanked the display and released its resources.
     * No physical IRQ/shutdown callback was registered by this backend. */
    if (mini != MINIPORT || !guest_span_valid(mini, 0x81C) ||
        X_M32(0x407488) != DEVICE || X_M32(mini) != BAR || !miniport_ready ||
        !channel_idle() || software_active || active_flip_queued || !display_mode_set || !screen_blanked ||
        X_M32(mini + 0x818) || X_M32(mini + 0x174) || X_M32(mini + 0x180) ||
        X_M32(mini + 0x18C) || X_M32(mini + 0x190) ||
        X_M32(mini + 0x100) || X_M32(mini + 0x104) != 1 || X_M32(mini + 0x108) != 1 ||
        X_M32(mini + 0x128) != 0x711000 || X_M32(mini + 0x134) != 2 ||
        X_M32(mini + 0x14C) != 3 || X_M32(mini + 0x150) != 0x01039000 ||
        X_M32(mini + 0x154) != 0x01110000 || X_M32(mini + 0x158) != 0 ||
        h2_instance_bytes() != 0x5000 ||
        !map_physical_raw(PHYSICAL_BYTES - 0x10000u - h2_instance_bytes(), h2_instance_bytes()))
        reject(c, ip, mini, channel_ready);
    /* Retire the synchronous channel, preserving its final guest-visible
     * RAMFC cursors. There is no pending DMA fetch or serialized GPU context.
     * Guest RAM and allocation ownership remain with the original destructor. */
    instance_write(c, 0x11000, channel.put);
    instance_write(c, 0x11004, channel.put);
    instance_write(c, 0x11008, 0);
    instance_write(c, 0x11010, 0);
    instance_write(c, 0x11044, h2_bus_read32(c, ip, BAR + 0x711040));
    instance_write(c, 0x11050, 0);
    /* Restore the modeled entry gates saved by the original clock/memory
     * helpers. Fixed PFB geometry already matches the validated saved values.
     * Retire RAMFC first: disabling FIFO resets its descriptor registers. */
    h2_bus_write32(c, 0x3FE64Fu, BAR + 0x200, X_M32(mini + 0x154));
    h2_bus_write32(c, 0x3FE65Bu, BAR + 0x140, X_M32(mini + 0x158));
    xv_logf("[h2/channel] shutdown idle PUT=GET=%08X; host state retired, guest allocations retained\n", channel.put);
    memset(&channel, 0, sizeof channel);
    memset(&tiles, 0, sizeof tiles);
    miniport_ready = channel_ready = initialization_flip_done = initialization_flip_queued = 0;
    mode_vblanks = gamma_cursor = 0;
    active_flip_queued = active_flip_retiring = 0;
    display_mode_set = 0;
    memset(scanout_gamma, 0, sizeof scanout_gamma);
    active_context = NULL;
    X_RET(0);
}
void h2_host_tile_remove(xctx *c)
{
    check_stack(c, 0x3FE86Au, 2);
    uint32_t index = c->r[3], mini = X_ARG(0), clear_zoffset = X_ARG(1);
    if (mini != MINIPORT || !channel_idle() || !h2_host_tile_disable(&tiles, index))
        reject(c, 0x3FE86Au, mini, index);
    /* The canonical representation has no physical compression offset. */
    xv_logf("[h2/tiles] disabled index=%u clear_zoffset=%08X\n", index, clear_zoffset);
    c->r[0] = 1;
    X_RET(2);
}
void h2_host_tile_configure(xctx *c)
{
    check_stack(c, 0x3FE67Fu, 7);
    uint32_t index = c->r[0], mini = X_ARG(0), address = X_ARG(1), bytes = X_ARG(2);
    uint32_t pitch = X_ARG(3), flags = X_ARG(4), zstart = X_ARG(5), zoffset = X_ARG(6);
    xv_logf("[h2/tiles] assign index=%u address=%08X bytes=%08X pitch=%u flags=%08X zstart=%08X zoffset=%08X\n",
            index, address, bytes, pitch, flags, zstart, zoffset);
    if (mini != MINIPORT || !channel_idle() ||
        !map_physical_raw(address, bytes) ||
        !h2_host_tile_assign(&tiles, index, address, bytes, pitch, flags, zstart, zoffset, PHYSICAL_BYTES))
        reject(c, 0x3FE67Fu, address, flags);
    if (flags == 0x84000001u)
        xv_logf("[h2/tiles] depth uses canonical logical Z24S8 bytes; hardware compression/tags are not emulated\n");
    c->r[0] = 1;
    X_RET(7);
}

static int software_flip(void *opaque, uint32_t value, uint32_t source)
{
    (void)opaque;
    if (display_mode_set) return queue_active_flip(value, source);
    uint32_t address = (value >> 5) & ~15u;
    int queued = (value & 0x1FF) == 0x21; /* interval one, non-immediate */
    /* First audited flips: scanout disabled, an empty software queue, no
     * timing or swap callback, identity gamma pending. The interval-one form
     * only queues work; no vblank or completion is fabricated.
     * Validate every possible guest input/write span before invoking its body. */
    if (software_active || initialization_flip_done || initialization_flip_queued ||
        (!queued && (value & 0x1FF) != 0x101) ||
        !active_context || active_context->r[4] < 256 ||
        !guest_span_valid(active_context->r[4] - 256, 256) ||
        !guest_span_valid(MINIPORT, 0x7E4) || !guest_span_valid(0x408650, 4) ||
        channel.clear.format != 0x128 || channel.clear.clip_horizontal != (640u << 16) ||
        channel.clear.clip_vertical != (480u << 16) ||
        (channel.clear.pitch & 0xFFFF) != 2560 ||
        !map_physical_raw(address, 640 * 480 * 4) ||
        !check_attachment(NULL, address, 640 * 480 * 4, 2560, 0, 0x128) ||
        X_M32(MINIPORT) != BAR || X_M32(MINIPORT + 0x1B8) != 1 ||
        X_M32(MINIPORT + 0x18C) || X_M32(MINIPORT + 0x1D4) ||
        X_M32(MINIPORT + 0x7DC) != 1 || X_M32(MINIPORT + 0x7E0) ||
        channel.commands.flip_read != 0 || channel.commands.flip_write != 1 ||
        channel.commands.flip_modulo != 2) return 0;
    for (unsigned offset = 0x174; offset <= 0x188; offset += 4)
        if (X_M32(MINIPORT + offset)) return 0;
    for (unsigned offset = 0x1BC; offset <= 0x1D8; offset += 4)
        if (X_M32(MINIPORT + offset)) return 0;
    for (unsigned component = 0; component < 3; ++component)
        for (unsigned i = 0; i < 256; ++i)
            if (X_M8(MINIPORT + 0x1DC + component * 256 + i) != i) return 0;
    /* The software interrupt uses a private CPU context. Original queue/global
     * RAM updates remain visible; all interrupted CPU/FP/control state survives. */
    xctx *interrupted = active_context;
    uint32_t interrupted_fn = xv_cur_fn;
    uint32_t interrupted_fpscr = h2_platform_fpscr_read();
    uint32_t old_framebuffer = X_M32(0x408650);
    xctx interrupt = *interrupted;
    interrupt.r[0] = value; interrupt.r[1] = MINIPORT;
    interrupt.preempt = 100000; /* bounded interrupt body cannot yield mid-command */
    software_active = 1; gamma_cursor = 0;
    call_guest(&interrupt, f_003FF240, source);
    active_context = interrupted;
    xv_cur_fn = interrupted_fn;
    software_active = 0;
    if (queued) {
        if (gamma_cursor || channel.commands.flip_read ||
            X_M32(0x408650) != old_framebuffer || X_M32(MINIPORT + 0x174) != 1 ||
            X_M32(MINIPORT + 0x178) != 1 || X_M32(MINIPORT + 0x17C) != address ||
            X_M32(MINIPORT + 0x1BC) || X_M32(MINIPORT + 0x1C0) ||
            X_M32(MINIPORT + 0x1C4) != 2 || X_M32(MINIPORT + 0x1C8) != 1 ||
            X_M32(MINIPORT + 0x1CC) != 1 || X_M32(MINIPORT + 0x7DC) != 1)
            reject(active_context, source, address, value);
        initialization_flip_queued = 1;
        xv_logf("[h2/flip] original interval-one handler queued address=%08X due_vblank=1; no completion or presentation\n", address);
        h2_platform_fpscr_write(interrupted_fpscr);
        return 1;
    }
    if (gamma_cursor != 768 || channel.commands.flip_read != 1 ||
        X_M32(0x408650) != address || X_M32(MINIPORT + 0x1BC) != 1 ||
        X_M32(MINIPORT + 0x1CC) != 1 || X_M32(MINIPORT + 0x7DC))
        reject(active_context, source, address, value);
    initialization_flip_done = 1;
    xv_logf("[h2/flip] original initialization handler completed address=%08X gamma_bytes=%u read=%u; scanout disabled, no presentation\n",
            address, gamma_cursor, channel.commands.flip_read);
    h2_platform_fpscr_write(interrupted_fpscr);
    return 1;
}

static int active_display_inputs(xctx *c, uint32_t address)
{
    return initialization_flip_done && !initialization_flip_queued && !software_active &&
        display_mode_set && !screen_blanked && mode_vblanks == 2 && c && c->r[4] >= 512 &&
        guest_span_valid(c->r[4] - 512, 512) && guest_span_valid(MINIPORT, 0x7E4) &&
        guest_span_valid(0x408650, 4) && game_vblank_inputs_valid() &&
        X_M32(MINIPORT) == BAR && X_M32(MINIPORT + 4) == 2560 &&
        X_M32(MINIPORT + 8) == 0x88070701 && X_M32(MINIPORT + 0x1B4) == 0x00480104 &&
        !X_M32(MINIPORT + 0x1B8) && !X_M32(MINIPORT + 0x18C) &&
        X_M32(MINIPORT + 0x190) == 0x12B2A0 && !X_M32(MINIPORT + 0x1D0) &&
        X_M32(MINIPORT + 0x194) == 0x00040000 && X_M32(MINIPORT + 0x198) <= 1 &&
        !X_M32(MINIPORT + 0x7DC) && !X_M32(MINIPORT + 0x7E0) && gamma_cursor == 768 &&
        channel.commands.flip_modulo == 2 && channel.clear.format == 0x128 &&
        channel.clear.clip_horizontal == (640u << 16) && channel.clear.clip_vertical == (480u << 16) &&
        (channel.clear.pitch & 0xFFFF) == 2560 && !(address & 15) &&
        map_physical_raw(address, H2_SCANOUT_BYTES) &&
        check_attachment(NULL, address, H2_SCANOUT_BYTES, 2560, 0, 0x128);
}

static int queue_active_flip(uint32_t value, uint32_t source)
{
    uint32_t address = (value >> 5) & ~15u;
    if (active_flip_queued || (value & 0x1FF) != 0x21 ||
        !active_display_inputs(active_context, address)) return 0;
    uint32_t count = X_M32(MINIPORT + 0x1C0), serial = X_M32(MINIPORT + 0x1CC);
    if (count < 2 || count > 0x7FFFFFFD || serial > 0x7FFFFFFD ||
        X_M32(MINIPORT + 0x1BC) != serial || X_M32(MINIPORT + 0x1C8) > count ||
        X_M32(MINIPORT + 0x174) || X_M32(MINIPORT + 0x180) ||
        channel.commands.flip_read != (serial & 1) ||
        channel.commands.flip_write != ((serial + 1) & 1)) return 0;
    uint32_t old_framebuffer = X_M32(0x408650), saved_fn = xv_cur_fn;
    uint32_t saved_fpscr = h2_platform_fpscr_read();
    xctx *interrupted = active_context, interrupt = *interrupted;
    interrupt.r[0] = value; interrupt.r[1] = MINIPORT; interrupt.preempt = 100000;
    software_active = 1;
    call_guest(&interrupt, f_003FF240, source);
    active_context = interrupted; xv_cur_fn = saved_fn; software_active = 0;
    uint32_t slot = (serial & 1) * 12;
    if (X_M32(MINIPORT + 0x174 + slot) != 1 ||
        X_M32(MINIPORT + 0x178 + slot) != count + 1 ||
        X_M32(MINIPORT + 0x17C + slot) != address ||
        X_M32(MINIPORT + 0x174 + (slot ^ 12)) ||
        X_M32(MINIPORT + 0x1C0) != count || X_M32(MINIPORT + 0x1C4) != count + 2 ||
        X_M32(MINIPORT + 0x1C8) != count + 1 || X_M32(MINIPORT + 0x1CC) != serial + 1 ||
        X_M32(MINIPORT + 0x1BC) != serial || X_M32(0x408650) != old_framebuffer ||
        channel.commands.flip_read != (serial & 1)) reject(interrupted, source, address, value);
    active_flip_address = address; active_flip_due = count + 1; active_flip_serial = serial;
    active_flip_queued = 1;
    if (serial < 4 || !((serial + 1) % 60))
        xv_logf("[h2/flip] original active-display interval-one queued address=%08X due=%u serial=%u; pending\n",
                address, active_flip_due, serial + 1);
    if (!((serial + 1) % 60)) perf_report(serial + 1);
    h2_platform_fpscr_write(saved_fpscr);
    return 1;
}

static int complete_active_flip(xctx *c, uint32_t source)
{
    if (!active_flip_queued || !active_display_inputs(c, active_flip_address)) return 0;
    uint32_t slot = (active_flip_serial & 1) * 12;
    if (X_M32(MINIPORT + 0x174 + slot) != 1 ||
        X_M32(MINIPORT + 0x178 + slot) != active_flip_due ||
        X_M32(MINIPORT + 0x17C + slot) != active_flip_address ||
        X_M32(MINIPORT + 0x174 + (slot ^ 12)) ||
        X_M32(MINIPORT + 0x1C0) != active_flip_due - 1 ||
        X_M32(MINIPORT + 0x1C4) != active_flip_due + 1 ||
        X_M32(MINIPORT + 0x1C8) != active_flip_due ||
        X_M32(MINIPORT + 0x1BC) != active_flip_serial ||
        X_M32(MINIPORT + 0x1CC) != active_flip_serial + 1 ||
        channel.commands.flip_read != (active_flip_serial & 1) ||
        channel.commands.flip_write != channel.commands.flip_read) return 0;
    uint32_t saved_fpscr = h2_platform_fpscr_read(), before, after;
    {   /* land the GXM menu backend's pending scene: the flip presents guest memory */
        extern void h2_menu_gxm_flush(void) __attribute__((weak));
        if (h2_menu_gxm_flush) h2_menu_gxm_flush();
    }
    /* The original flip retires at a vblank, when the CRTC starts scanning the new
     * buffer. Queue the finished frame for the display's next vblank first and wait
     * for that one vblank: the retirement the guest observes is the vblank at which
     * the frame became visible. (Queueing after the retire vblank and waiting a
     * second one to show it cost a whole extra display period per flip.) */
    static uint32_t queued_serial = UINT32_MAX;   /* each flip's frame is queued once; a retried wait only waits */
    int status;
    if (queued_serial != active_flip_serial) {
        const uint8_t *pixels = map_physical_raw(active_flip_address, H2_SCANOUT_BYTES);
        if (!pixels) reject(c, source, active_flip_address, H2_SCANOUT_BYTES);
        { uint64_t perf_t0 = perf_now(); status = h2_platform_present_queue(pixels, H2_SCANOUT_BYTES, scanout_gamma); perf_present_us += perf_now() - perf_t0; }
        if (status < 0) reject(c, source, active_flip_address, (uint32_t)status);
        queued_serial = active_flip_serial;
    }
    status = h2_platform_wait_vblank(&before, &after);
    if (status < 0 || before == after) { h2_platform_fpscr_write(saved_fpscr); return 0; }
    uint32_t timestamp = (uint32_t)x_rdtsc(), saved_fn = xv_cur_fn;
    X_W32(MINIPORT + 0x1D4) = timestamp - X_M32(MINIPORT + 0x1D8);
    X_W32(MINIPORT + 0x1D8) = timestamp; X_W32(MINIPORT + 0x1C0) = active_flip_due;
    xctx interrupt = *c; interrupt.r[6] = MINIPORT; interrupt.preempt = 100000;
    software_active = active_flip_retiring = 1;
    active_crtc_writes = active_read_increments = 0;
    call_guest(&interrupt, f_003FECC0, source);
    if (interrupt.r[0] != 1 || active_crtc_writes != 1 || active_read_increments != 1 ||
        X_M32(MINIPORT + 0x174) || X_M32(MINIPORT + 0x180) ||
        X_M32(MINIPORT + 0x1BC) != active_flip_serial + 1 ||
        X_M32(0x408650) != active_flip_address ||
        channel.commands.flip_read != ((active_flip_serial + 1) & 1))
        reject(c, source, active_flip_address, interrupt.r[0]);
    signal_game_vblank(c, active_flip_due, active_flip_serial + 1, 1, source);
    active_context = c; xv_cur_fn = saved_fn; software_active = active_flip_retiring = 0;
    uint32_t presented = after;
    active_flip_queued = 0;
    if (&xv_write_epoch) ++xv_write_epoch;   /* a new frame: later writes stamp a newer epoch */
    if (active_flip_serial < 4 || !((active_flip_serial + 1) % 60))
        xv_logf("[h2/display] original recurring flip address=%08X guest_vblank=%u swaps=%u real_wait=%u->%u present_vcount=%u\n",
                active_flip_address, active_flip_due, active_flip_serial + 1, before, after, presented);
    h2_platform_fpscr_write(saved_fpscr);
    return 1;
}

static int complete_initialization_vblank(xctx *c, uint32_t source)
{
    /* Native62: one queued interval-one initialization flip, progressive
     * scanout disabled, before the first vblank of this virtual miniport.
     * Do not generalize this to active display, arbitrary callbacks or IRQs. */
    if (!initialization_flip_queued || initialization_flip_done || software_active ||
        display_mode_set || mode_vblanks || c->r[4] < 512 ||
        !guest_span_valid(c->r[4] - 512, 512) || !guest_span_valid(MINIPORT, 0x7E4) ||
        !guest_span_valid(0x408650, 4) || !game_vblank_inputs_valid() || X_M32(MINIPORT) != BAR ||
        X_M32(MINIPORT + 4) != 2560 || X_M32(MINIPORT + 8) != 0x88070701 ||
        X_M32(MINIPORT + 0x1B4) != 0x00480104 || X_M32(MINIPORT + 0x1B8) != 1 ||
        X_M32(MINIPORT + 0x174) != 1 || X_M32(MINIPORT + 0x178) != 1 ||
        X_M32(MINIPORT + 0x180) || X_M32(MINIPORT + 0x184) || X_M32(MINIPORT + 0x188) ||
        X_M32(MINIPORT + 0x18C) || X_M32(MINIPORT + 0x190) != 0x12B2A0 ||
        X_M32(MINIPORT + 0x194) != 0x00040000 || X_M32(MINIPORT + 0x198) > 1 ||
        X_M32(MINIPORT + 0x1BC) || X_M32(MINIPORT + 0x1C0) ||
        X_M32(MINIPORT + 0x1C4) != 2 || X_M32(MINIPORT + 0x1C8) != 1 ||
        X_M32(MINIPORT + 0x1CC) != 1 || X_M32(MINIPORT + 0x1D0) ||
        X_M32(MINIPORT + 0x1D4) || X_M32(MINIPORT + 0x1D8) ||
        X_M32(MINIPORT + 0x7DC) != 1 || X_M32(MINIPORT + 0x7E0) ||
        channel.commands.flip_read || channel.commands.flip_write || channel.commands.flip_modulo != 2)
        return 0;
    uint32_t address = X_M32(MINIPORT + 0x17C);
    if ((address & 15) || !map_physical_raw(address, H2_SCANOUT_BYTES) ||
        !check_attachment(NULL, address, H2_SCANOUT_BYTES, 2560, 0, 0x128)) return 0;
    for (unsigned component = 0; component < 3; ++component)
        for (unsigned i = 0; i < 256; ++i)
            if (X_M8(MINIPORT + 0x1DC + component * 256 + i) != i) return 0;

    uint32_t saved_fpscr = h2_platform_fpscr_read(), before, after;
    int status = h2_platform_wait_vblank(&before, &after);
    if (status < 0 || before == after) {
        h2_platform_fpscr_write(saved_fpscr);
        return 0; /* guest queue, CPU state and pending parser word untouched */
    }
    uint32_t saved_fn = xv_cur_fn;
    xctx interrupt = *c;
    interrupt.preempt = 100000;
    software_active = 1; gamma_cursor = 0;
    /* Original 3FED90's first progressive-vblank count/time update. The queue
     * consumer below performs the original retirement and gamma writes. */
    X_W32(MINIPORT + 0x1D8) = (uint32_t)x_rdtsc();
    X_W32(MINIPORT + 0x1C0) = 1;
    interrupt.r[6] = MINIPORT;
    call_guest(&interrupt, f_003FECC0, source);
    if (interrupt.r[0] != 1 || gamma_cursor != 768 || channel.commands.flip_read != 1 ||
        X_M32(MINIPORT + 0x174) || X_M32(MINIPORT + 0x1BC) != 1 ||
        X_M32(MINIPORT + 0x7DC) || X_M32(0x408650) != address)
        reject(c, source, address, interrupt.r[0]);
    /* Progressive field zero; no analog field/interrupt register is invented.
     * Signal the actual event before the original cdecl callback, as 3FED90
     * does. Its 12-byte record is {vblank count, retired swap count, flags}. */
    X_W32(MINIPORT + 0x1D0) = 0;
    signal_game_vblank(c, 1, 1, 1, source);
    active_context = c; xv_cur_fn = saved_fn; software_active = 0;
    initialization_flip_queued = 0; initialization_flip_done = 1; mode_vblanks = 1;
    xv_logf("[h2/vblank] real Vita vcount=%u->%u retired initialization address=%08X; scanout disabled\n",
            before, after, address);
    h2_platform_fpscr_write(saved_fpscr);
    return 1;
}
static int complete_timed_mode_vblank(xctx *c, uint32_t source)
{
    /* Native63's caller reset the event after the interval-one flip retired.
     * This is exactly the next empty-queue vblank, before applying 480p mode. */
    if (!initialization_flip_done || initialization_flip_queued || software_active ||
        display_mode_set || mode_vblanks != 1 || !channel_idle() || c->r[4] < 512 ||
        !guest_span_valid(c->r[4] - 512, 512) || !guest_span_valid(MINIPORT, 0x7E4) ||
        !game_vblank_inputs_valid() || X_M32(MINIPORT) != BAR ||
        X_M32(MINIPORT + 8) != 0x88070701 || X_M32(MINIPORT + 0x1B4) != 0x00480104 ||
        X_M32(MINIPORT + 0x174) || X_M32(MINIPORT + 0x180) || X_M32(MINIPORT + 0x18C) ||
        X_M32(MINIPORT + 0x194) != 0x00040000 || X_M32(MINIPORT + 0x198) ||
        X_M32(MINIPORT + 0x1B8) != 1 || X_M32(MINIPORT + 0x1BC) != 1 ||
        X_M32(MINIPORT + 0x1C0) != 1 || X_M32(MINIPORT + 0x1C4) != 2 ||
        X_M32(MINIPORT + 0x1C8) != 1 || X_M32(MINIPORT + 0x1CC) != 1 ||
        X_M32(MINIPORT + 0x1D0) || X_M32(MINIPORT + 0x1D4) || !X_M32(MINIPORT + 0x1D8) ||
        X_M32(MINIPORT + 0x7DC) || X_M32(MINIPORT + 0x7E0) || gamma_cursor != 768 ||
        channel.commands.flip_read != 1 || channel.commands.flip_write || channel.commands.flip_modulo != 2)
        return 0;
    uint32_t saved_fpscr = h2_platform_fpscr_read(), before, after;
    {   /* land the GXM menu backend's pending scene: the flip presents guest memory */
        extern void h2_menu_gxm_flush(void) __attribute__((weak));
        if (h2_menu_gxm_flush) h2_menu_gxm_flush();
    }
    int status = h2_platform_wait_vblank(&before, &after);
    if (status < 0 || before == after) { h2_platform_fpscr_write(saved_fpscr); return 0; }
    uint32_t timestamp = (uint32_t)x_rdtsc(), saved_fn = xv_cur_fn;
    X_W32(MINIPORT + 0x1D4) = timestamp - X_M32(MINIPORT + 0x1D8);
    X_W32(MINIPORT + 0x1D8) = timestamp; X_W32(MINIPORT + 0x1C0) = 2;
    xctx interrupt = *c; interrupt.preempt = 100000; interrupt.r[6] = MINIPORT;
    software_active = 1;
    call_guest(&interrupt, f_003FECC0, source);
    if (interrupt.r[0]) reject(c, source, MINIPORT, interrupt.r[0]);
    /* Original 3FEDE3: no retirement and counter==deadline advances the next
     * requested count and delivers flags=2 to the original game callback. */
    X_W32(MINIPORT + 0x1C4) = 3;
    signal_game_vblank(c, 2, 1, 2, source);
    active_context = c; xv_cur_fn = saved_fn; software_active = 0; mode_vblanks = 2;
    xv_logf("[h2/vblank] real Vita vcount=%u->%u delivered second mode-transition vblank; queue empty, scanout disabled\n", before, after);
    h2_platform_fpscr_write(saved_fpscr);
    return 1;
}
int h2_host_channel_bus(xctx *c, uint32_t ip, uint32_t address, unsigned width,
                         uint32_t *value, int write)
{
    if (address == BAR + 0x600800) {
        if (!software_active || !active_flip_retiring || !write || width != 4 ||
            ip != 0x3FF5BC || active_crtc_writes || *value != active_flip_address)
            reject(c, ip, address, *value);
        ++active_crtc_writes; /* typed scanout selection, presented after retirement */
        return 1;
    }
    if (address == BAR + 0x40071C || address == BAR + 0x6813C8 || address == BAR + 0x6813C9) {
        if (!software_active) reject(c, ip, address, *value);
        if (address == BAR + 0x40071C) {
            if (active_flip_retiring) {
                if (width != 4 || channel.commands.flip_modulo != 2 ||
                    channel.commands.flip_read != (active_flip_serial & 1) ||
                    (write && (*value != 2 || ip != 0x3FED4C || active_read_increments || active_crtc_writes != 1)) ||
                    (!write && ip != 0x3FED43)) reject(c, ip, address, *value);
                if (write) { channel.commands.flip_read = (channel.commands.flip_read + 1) % 2; ++active_read_increments; }
                else *value = 0;
            } else {
                if (width != 4 || (write && (*value != 2 || channel.commands.flip_read != 0)))
                reject(c, ip, address, *value);
                if (write) channel.commands.flip_read = 1; else *value = 0;
            }
        } else {
            if (!write || width != 1 || (address == BAR + 0x6813C8 && (*value || gamma_cursor)) ||
                (address == BAR + 0x6813C9 && gamma_cursor >= 768)) reject(c, ip, address, *value);
            if (address == BAR + 0x6813C9) scanout_gamma[gamma_cursor++] = *value;
        }
        return 1;
    }
    if (address != BAR + 0x800040 && address != BAR + 0x800044 &&
        address != BAR + 0x3240 && address != BAR + 0x3244 && address != BAR + 0x400700) return 0;
    if (!channel_ready || width != 4 || (write && address != BAR + 0x800040))
        reject(c, ip, address, *value);
    /* The consumer may yield to other guest fibers mid-frame (menu pacing); a PUT
     * written by another fiber meanwhile is recorded and consumed by the active
     * submit once it finishes, never by re-entering the consumer. */
    static int consuming, pending;
    static uint32_t pending_put;
    if (write && consuming) { pending_put = *value; pending = 1; return 1; }
    active_context = c;
    if (write) {
        uint32_t put = *value;
        consuming = 1;
        do {
            pending = 0;
            h2_push_fault fault;
            uint64_t perf_t0 = perf_now();
            enum h2_push_result result = h2_host_channel_submit(&channel, put, 1000000, &fault);
            perf_submit_us += perf_now() - perf_t0; ++perf_submits;
            if (result == H2_PUSH_METHOD_REJECTED && fault.method == 0x130 && !fault.word &&
                fault.subchannel < 8 && channel.commands.bound[fault.subchannel] == 1) {
                perf_t0 = perf_now();
                int completed = active_flip_queued ? complete_active_flip(c, fault.address) :
                                                     complete_initialization_vblank(c, fault.address);
                perf_flip_us += perf_now() - perf_t0;
                if (completed) {
                    ++perf_flips_completed; perf_t0 = perf_now();
                    result = h2_host_channel_submit(&channel, put, 1000000, &fault);
                    perf_submit_us += perf_now() - perf_t0; ++perf_submits;
                }
            }
            static uint32_t put_logs;
            if (result != 0 || h2_log_budget(&put_logs, 2000, 5000))
            xv_logf("[h2/channel] PUT=%08X GET=%08X result=%d source=%08X word=%08X sub=%u method=%04X clears=%llu pixels=%llu\n",
                    put, h2_host_channel_get(&channel), result, fault.address, fault.word,
                    fault.subchannel, fault.method, (unsigned long long)channel.clear.completed_clears,
                    (unsigned long long)channel.clear.written_pixels);
            static uint32_t semaphore_logs; if (h2_log_budget(&semaphore_logs, 2000, 5000))
            xv_logf("[h2/channel] semaphore releases=%llu last=%08X value=%08X\n",
                    (unsigned long long)channel.commands.semaphore_releases,
                    channel.commands.last_semaphore_address, channel.commands.last_semaphore_value);
            static uint32_t update_logs; if (h2_log_budget(&update_logs, 2000, 5000))
            xv_logf("[h2/channel] software updates=%llu valid=%X dxt1_noise=%u zcull=%08X rop=%08X\n",
                    (unsigned long long)channel.commands.software_updates, channel.commands.software_valid,
                    channel.commands.dxt1_noise, channel.commands.zcull_debug5, channel.commands.rop_control);
            if (result != H2_PUSH_COMPLETE && result != H2_PUSH_NEED_DATA) { consuming = 0; reject(c, ip, address, put); }
            if (pending) put = pending_put;
        } while (pending);
        consuming = 0;
    } else if (address == BAR + 0x800040 || address == BAR + 0x3240) *value = channel.put;
    else if (address == BAR + 0x800044 || address == BAR + 0x3244) *value = h2_host_channel_get(&channel);
    else *value = channel.stream.remaining != 0 || channel.put != h2_host_channel_get(&channel);
    return 1;
}
