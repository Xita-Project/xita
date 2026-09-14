/* Standalone synthetic PCM/output utility. Contains no game image or code. */
#include "audio_host.h"
#include "recomp/kernel/xk_audio.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void xv_logf(const char *format, ...)
{ va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }
void xk_os_log(const char *format, ...)
{ va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }
uint64_t xk_os_monotonic_us(void) { return sceKernelGetProcessTimeWide(); }
static int wait_for_grains(uint32_t target, int nonzero)
{
    uint64_t deadline = sceKernelGetProcessTimeWide() + 3000000;
    do {
        h2_audio_backend_status status; h2_audio_backend_snapshot(&status);
        if (status.error) return -1;
        if ((nonzero ? status.nonzero_grains : status.grains) >= target) return 0;
        sceKernelDelayThread(1000);
    } while (sceKernelGetProcessTimeWide() < deadline);
    return -1;
}
int main(void)
{
    sceIoMkdir("ux0:data", 0777); sceIoMkdir("ux0:data/xita-halo2", 0777);
    FILE *report = fopen("ux0:data/xita-halo2/audio-probe.txt", "w");
    if (!report) return 2;
    int success = 0, voice = -1;
    h2_audio_backend_status before = {0}, playing = {0}, stopped = {0};
    g_xram = calloc(1, 0x6000); g_img_base = g_xram; g_xpt = calloc(1u << 20, 4);
    if (!g_xram || !g_xpt) goto finish;
    g_xpt[1] = 0; g_xpt[2] = 0x1000; g_xpt[3] = 0x3000;
    X_M16(0x1100) = 1; X_M16(0x1102) = 2; X_M32(0x1104) = 48000;
    X_M32(0x1108) = 192000; X_M16(0x110C) = 4; X_M16(0x110E) = 16;
    /* A 1 kHz stereo triangle, opposite polarity per channel, 16 complete
     * cycles in a looping PCM region crossing independently mapped pages. */
    int16_t pcm[768 * 2];
    for (unsigned i = 0; i < 768; ++i) {
        unsigned phase = i % 48;
        int16_t value = ((int)(phase < 24 ? phase : 48 - phase) - 12) * 600;
        pcm[i * 2] = value; pcm[i * 2 + 1] = -value;
    }
    x_guest_write(0x2FE0, pcm, sizeof pcm);
    if (h2_audio_backend_open() < 0) goto finish;
    h2_audio_backend_snapshot(&before);
    if (before.nonzero_grains || before.error || h2_audio_backend_free_voices() != XA_MAX_VOICES) goto finish;
    voice = xk_audio_voice_new(1, 0x1100);
    if (voice < 0 || h2_audio_backend_free_voices() != XA_MAX_VOICES - 1) goto finish;
    xk_audio_voice_set_data(voice, 0x2FE0, sizeof pcm); xk_audio_voice_play(voice, 1);
    if (wait_for_grains(12, 1) < 0) goto finish;
    h2_audio_backend_snapshot(&playing);
    if (!playing.peak || playing.peak > 7200 || playing.error) goto finish;
    xk_audio_voice_stop(voice); xk_audio_voice_free(voice); voice = -1;
    if (h2_audio_backend_free_voices() != XA_MAX_VOICES || wait_for_grains(playing.grains + 3, 0) < 0) goto finish;
    h2_audio_backend_snapshot(&stopped);
    /* At most the worker's already mixed grain can remain nonzero at stop. */
    if (stopped.nonzero_grains > playing.nonzero_grains + 1) goto finish;
    if (h2_audio_backend_close() < 0) goto finish;
    if (h2_audio_backend_open() < 0) goto finish;
    if (h2_audio_backend_close() < 0) goto finish;
    success = 1;
finish:
    if (voice >= 0) xk_audio_voice_free(voice);
    /* Failed joins retain every resource the worker may still access. Even a
     * failing probe must not free guest sample storage beneath that worker. */
    int retired = h2_audio_backend_close();
    if (retired < 0) success = 0;
    fprintf(report, "synthetic_pcm_probe=%s\nrate=48000 channels=2 format=PCM16 frequency=1000\n"
            "before_grains=%u before_nonzero=%u\nplaying_grains=%u playing_nonzero=%u peak=%u error=%08X\n"
            "stopped_grains=%u stopped_nonzero=%u\nreopen_close=%d\n",
            success ? "PASS" : "FAIL", before.grains, before.nonzero_grains,
            playing.grains, playing.nonzero_grains, playing.peak, playing.error,
            stopped.grains, stopped.nonzero_grains, success);
    fclose(report);
    if (!retired) { free(g_xpt); free(g_xram); }
    printf("[h2/audio-probe] synthetic PCM output, stop, free and reopen: %s\n", success ? "PASS" : "FAIL");
    sceKernelExitProcess(success ? 0 : 3); return 0;
}
