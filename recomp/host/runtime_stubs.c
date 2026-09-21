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
