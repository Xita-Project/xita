#pragma once
#include "xv_x86rt.h"
void h2_host_miniport_init(xctx *c);
void h2_host_channel_configure(xctx *c);
void h2_host_memory_barrier(xctx *c);
void h2_host_tile_remove(xctx *c);
void h2_host_tile_configure(xctx *c);
/* Desired encoder settings only. A future presenter must implement them or
 * reject presentation; recording a request never applies a display mode. */
typedef struct h2_host_av_config {
    uint32_t flicker_filter, luma_filter;
    uint8_t has_flicker, has_luma;
} h2_host_av_config;
h2_host_av_config h2_host_av_configuration(void);
void __wrap_xk_AvSendTVEncoderOption(xctx *c);
void __wrap_xk_AvSetDisplayMode(xctx *c);
/* Return zero only for an address outside the virtual channel contract. */
int h2_host_channel_bus(xctx *c, uint32_t ip, uint32_t address, unsigned width,
                         uint32_t *value, int write);
