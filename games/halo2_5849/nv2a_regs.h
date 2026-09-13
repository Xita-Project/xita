/* Narrow, stateful NV2A bootstrap model. No command processor or renderer yet. */
#pragma once
#include <stdint.h>

enum h2_nv2a_result {
    H2_NV2A_OK, H2_NV2A_UNKNOWN_REGISTER, H2_NV2A_INVALID_ACCESS,
    H2_NV2A_UNSUPPORTED_OPERATION
};
typedef struct h2_nv2a {
    uint32_t memory_bytes;
    uint32_t master_enable, master_interrupt_enable;
    uint32_t core_pll, memory_pll, video_pll;
    uint32_t pci_command, pci_latency, pci_rom;
    uint32_t crtc_interrupt_enable, timer_interrupt_enable;
} h2_nv2a;

enum { H2_ENGINE_TIMER = 1u << 16, H2_ENGINE_FB = 1u << 20,
       H2_ENGINE_CRTC = 1u << 24 };
/* Side-effect-free register subreads, little endian; no cross-register reads. */
enum h2_nv2a_result h2_nv2a_read(const h2_nv2a *device, uint32_t offset,
                               unsigned width, uint32_t *value);
uint32_t h2_nv2a_pll_hz(uint32_t coefficient);
void h2_nv2a_reset(h2_nv2a *device, uint32_t memory_bytes);
enum h2_nv2a_result h2_nv2a_read32(const h2_nv2a *device, uint32_t offset, uint32_t *value);
enum h2_nv2a_result h2_nv2a_write32(h2_nv2a *device, uint32_t offset, uint32_t value);
