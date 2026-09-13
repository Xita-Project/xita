#pragma once
#include "xv_x86rt.h"
void h2_host_miniport_init(xctx *c);
void h2_host_channel_configure(xctx *c);
void h2_host_memory_barrier(xctx *c);
void h2_host_tile_remove(xctx *c);
void h2_host_tile_configure(xctx *c);
/* Return zero only for an address outside the virtual channel contract. */
int h2_host_channel_bus(xctx *c, uint32_t ip, uint32_t address, unsigned width,
                         uint32_t *value, int write);
