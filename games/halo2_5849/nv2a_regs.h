/* Narrow, stateful NV2A bootstrap model. No command processor or renderer yet. */
#pragma once
#include <stdint.h>

enum h2_nv2a_result {
    H2_NV2A_OK, H2_NV2A_UNKNOWN_REGISTER, H2_NV2A_INVALID_ACCESS,
    H2_NV2A_UNSUPPORTED_OPERATION
};
typedef struct h2_nv2a {
    uint32_t memory_bytes;
    uint32_t pci_command, pci_latency, pci_rom;
    uint32_t crtc_interrupt_enable, timer_interrupt_enable;
} h2_nv2a;

void h2_nv2a_reset(h2_nv2a *device, uint32_t memory_bytes);
enum h2_nv2a_result h2_nv2a_read32(const h2_nv2a *device, uint32_t offset, uint32_t *value);
enum h2_nv2a_result h2_nv2a_write32(h2_nv2a *device, uint32_t offset, uint32_t value);
