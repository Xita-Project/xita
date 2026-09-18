/* One synchronous virtual DMA channel. Completion means accepted methods have
 * executed against guest RAM, including implemented semaphore writes. It never
 * implies display, vblank or unimplemented asynchronous GPU work. */
#pragma once
#include "push_stream.h"
#include "command_state.h"

typedef struct h2_host_channel {
    h2_push_stream stream;
    h2_kelvin_clear clear;
    h2_command_state commands;
    h2_dma_object dma;
    h2_push_read read_physical;
    /* Optional XDK software flip boundary. Rejection must preserve guest/state. */
    int (*software_flip)(void *opaque, uint32_t value, uint32_t source);
    /* Optional geometry consumer: -1 means not handled, 0 rejects, 1 executes.
     * Rejection/not-handled must preserve guest and command state. */
    int (*geometry_method)(void *opaque, uint8_t subchannel, uint16_t method,
                            uint32_t value, uint32_t source);
    void *opaque;
    uint32_t put;
    uint8_t bootstrap;
} h2_host_channel;
int h2_host_channel_init(h2_host_channel *channel, const h2_dma_object *dma,
                         uint32_t ring, uint32_t bytes, h2_push_read read_physical,
                         h2_instance_read read_instance, h2_physical_map map_physical,
                         void *opaque, uint32_t physical_bytes);
uint32_t h2_host_channel_get(const h2_host_channel *channel);
enum h2_push_result h2_host_channel_submit(h2_host_channel *channel, uint32_t put,
                                          uint32_t word_budget, h2_push_fault *fault);
