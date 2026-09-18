#pragma once
#include "xv_x86rt.h"
int h2_instance_memory_init(void);
uint32_t h2_instance_bytes(void);
uint32_t h2_instance_claim(uint32_t bytes, uint32_t *padding);
void __wrap_xk_MmClaimGpuInstanceMemory(xctx *context);
