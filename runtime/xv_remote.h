#pragma once
#include <stdint.h>

/* Opt-in development service. No sockets/buffers are allocated when disabled. */
void xv_remote_start(void);
void xv_remote_stop(void);
/* Called only with a finished, owned ABGR framebuffer, before slot release. */
void xv_remote_frame(const void *pixels, unsigned width, unsigned height, unsigned pitch);
/* Short-lived remote input; physical input takes priority. No network I/O here. */
void xv_remote_pad(uint32_t *buttons, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry);
