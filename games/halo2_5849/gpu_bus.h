#pragma once
#include "xv_x86rt.h"
void h2_gpu_bus_reset(uint32_t physical_memory_bytes);
uint32_t h2_bus_read32(xctx *context, uint32_t instruction, uint32_t address);
void h2_bus_write32(xctx *context, uint32_t instruction, uint32_t address, uint32_t value);
void h2_graphics_stop(xctx *context, uint32_t instruction, uint32_t address,
                      uint32_t value, int write, int reason) __attribute__((noreturn));
