/* runtime_stubs.c - host harness stand-ins for the few runtime/ (Vita GXM/UI) symbols the kernel references.
 * Weak, so a fuller host runtime can override them. Diagnostic only: no timing meaning. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
__attribute__((weak)) void xv_logf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
__attribute__((weak)) int xv_benchmark_active(void) { return 0; }
/* GPU visibility queries never complete on the host: report no result, no wait. */
__attribute__((weak)) uint32_t xd3d_r_visibility_generation(uint32_t id) { (void)id; return 0; }
__attribute__((weak)) uint32_t xd3d_r_visibility_result_generation(uint32_t id, uint32_t serial, uint32_t *pixels) { (void)id; (void)serial; if (pixels) *pixels = 0; return 0; }
__attribute__((weak)) int xd3d_r_visibility_wait_generation(uint32_t id, uint32_t serial, uint32_t timeout_us) { (void)id; (void)serial; (void)timeout_us; return 0; }
/* XV_HOST_VISIBILITY_PIXELS=N (host testing only): answer the lens-flare visibility queries with a pseudo pixel count
 * in [0, N) hashed from the query id and the frame number (every 8 frames), instead of 0, so flares get nonzero
 * brightness and the flare render path past its brightness test (f_000606B0's reflections and draws) runs on the host;
 * two runs of the same save and pad script see the same pattern. Default 0: no pixels, as before. Overrides xd3d.c's
 * weak 0-pixel default. */
#include <stdlib.h>
#ifndef XV_HOST_RUNTIME   /* --runtime builds link runtime/xv_d3d.c's real query results instead */
extern unsigned xd3d_frame(void);
uint32_t xd3d_r_visibility_result(uint32_t id, uint32_t *pixels)
{
    static int n = -1;
    if (n < 0) { const char *e = getenv("XV_HOST_VISIBILITY_PIXELS"); n = e ? atoi(e) : 0; if (n < 0) n = 0; }
    *pixels = n ? (((id * 2654435761u) >> 12) ^ ((xd3d_frame() >> 3) * 40503u)) % (uint32_t)n : 0;
    return 0;
}
#endif
/* Grouped 60-frame reports: the Vita log batches them; on the host print them directly. */
__attribute__((weak)) int xv_log_report_begin_async_frame(unsigned frame) { (void)frame; return 1; }
__attribute__((weak)) int xv_log_report_begin(void) { return 1; }
__attribute__((weak)) void xv_log_report_end(void) {}
