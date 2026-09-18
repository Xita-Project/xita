/* Private owned-DSP/synthetic-input sink probe. Its package contains owned
 * executable DSP code and must never be distributed. No game/menu claim. */
#include "audio_host.h"
#include "dsp_asset.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void xv_logf(const char *format, ...)
{ va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }
void xk_os_log(const char *format, ...)
{ va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }
uint64_t xk_os_monotonic_us(void) { return sceKernelGetProcessTimeWide(); }
int main(void)
{
    sceIoMkdir("ux0:data", 0777); sceIoMkdir("ux0:data/xita-halo2", 0777);
    FILE *f = fopen("ux0:data/xita-halo2/fxin2-probe.txt", "w"); if (!f) return 2;
    h2_dsp_engine *engine = NULL; h2_dsp_status dsp = {0}; h2_audio_backend_status sink = {0};
    int ok = 0; g_xram = calloc(1, 4096); g_img_base = g_xram; g_xpt = calloc(1u << 20, 4);
    if (!g_xram || !g_xpt || h2_audio_backend_open() < 0) goto done;
    engine = h2_dsp_asset_open("app0:halo2-dsp.bin", &dsp);
    if (!engine || !h2_dsp_zero_frame(engine)) goto done;
    int32_t bins[32][32] = {{0}};
    for (unsigned i = 0; i < 32; ++i) {
        bins[13][i] = 0x100000;
#if H2_FX_PROBE_SECOND
        bins[23][i] = 0x80000;
#endif
    }
    if (!h2_dsp_mix_frame(engine, bins)) goto done;
    for (unsigned i = 0; i < 6; ++i) if (h2_audio_backend_set_headroom(i, 0) < 0) goto done;
    if (h2_audio_backend_fx_bind(engine, 13) < 0 || h2_audio_backend_fx_route(13, 6) < 0 ||
        h2_audio_backend_fx_play(13) < 0) goto done;
#if H2_FX_PROBE_SECOND
    if (h2_audio_backend_fx_bind(engine, 23) < 0 || h2_audio_backend_fx_play(23) < 0) goto done;
#endif
    uint64_t begin = sceKernelGetProcessTimeWide();
    while (sceKernelGetProcessTimeWide() - begin < 5000000) {
        h2_audio_backend_snapshot(&sink);
        if (sink.error) goto done;
        if (sink.fx_submitted_frames >= 4096 && sink.fx_consumed_frames >= 3072) { ok = 1; break; }
        sceKernelDelayThread(1000);
    }
done:
    /* Joining and draining are required before reading/freeing the DSP. */
    int retired = h2_audio_backend_close();
    h2_audio_backend_snapshot(&sink);
    if (!retired && engine) h2_dsp_snapshot(engine, &dsp);
    ok = ok && !retired && !sink.error && sink.peak && sink.nonzero_grains &&
         sink.fx_submitted_frames == sink.fx_consumed_frames;
#if H2_FX_PROBE_SECOND
    ok = ok && sink.fx_playing_mask == 3 && sink.fx_source_submitted[1] &&
         sink.fx_source_submitted[1] == sink.fx_source_consumed[1];
#endif
    fprintf(f, "owned_DSP_synthetic_signal_real_sink=%s\ncomputed_frames=%llu submitted_frames=%llu consumed_frames=%llu\n"
            "compute_us=%llu max_grain_us=%llu deadline_misses=%u empty_after_compute=%u\n"
            "sink_grains=%u nonzero_grains=%u peak=%u error=%08X close=%d\n"
            "dsp_frames=%llu instructions=%llu canonical=%016llX\n"
            "sources_bound=%X sources_playing=%X source13_submitted=%llu consumed=%llu source23_submitted=%llu consumed=%llu\n"
            "Source: synthetic bin13 one-frame pulse through original GP; optional second probe adds bin23 pulse and default FL/FR source.\n"
            "Output: real GP FL/FR monitor to Vita stereo sink; no EP/AC3, title/menu or real-time guarantee.\n",
            ok ? "PASS" : "FAIL", (unsigned long long)sink.fx_computed_frames,
            (unsigned long long)sink.fx_submitted_frames, (unsigned long long)sink.fx_consumed_frames,
            (unsigned long long)sink.fx_compute_us, (unsigned long long)sink.fx_max_compute_us,
            sink.fx_deadline_misses, sink.fx_empty_after_compute, sink.grains, sink.nonzero_grains,
            sink.peak, sink.error, retired, (unsigned long long)dsp.frames,
            (unsigned long long)dsp.instructions, (unsigned long long)dsp.state_fingerprint,
            sink.fx_bound_mask, sink.fx_playing_mask,
            (unsigned long long)sink.fx_source_submitted[0], (unsigned long long)sink.fx_source_consumed[0],
            (unsigned long long)sink.fx_source_submitted[1], (unsigned long long)sink.fx_source_consumed[1]);
    fclose(f);
    if (!retired) { h2_dsp_destroy(engine); free(g_xram); free(g_xpt); }
    sceKernelExitProcess(ok ? 0 : 1); return ok ? 0 : 1;
}
