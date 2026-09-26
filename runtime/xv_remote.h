#pragma once
#include <stdint.h>

/* Compiled only with XV_DEVELOPER_BUILD=1, then opt-in at startup.
 * Tester binaries provide inert hooks and contain no remote server. */
void xv_remote_start(void);
void xv_remote_stop(void);
/* Called only with a finished, owned ABGR framebuffer, before slot release. */
void xv_remote_frame(const void *pixels, unsigned width, unsigned height, unsigned pitch);
/* Short-lived remote input; physical input takes priority. No network I/O here. */
void xv_remote_pad(uint32_t *buttons, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry);
/* Recording owner only: consume one authenticated draw-trace request at a
 * frame boundary. The network thread never reads or changes guest state. */
int xv_remote_take_draw_trace(void);

int xv_remote_ready(void);

const char *xv_distribution(void);
