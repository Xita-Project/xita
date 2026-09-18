/* Bounded NV2A method stream for a future Halo 2 host channel.
 * Parsing is not execution: the consumer must implement every accepted method. */
#pragma once
#include <stdint.h>

enum h2_push_result {
    H2_PUSH_COMPLETE, H2_PUSH_NEED_DATA, H2_PUSH_BUDGET_EXHAUSTED,
    H2_PUSH_BAD_STATE, H2_PUSH_MEMORY_FAULT, H2_PUSH_UNSUPPORTED_COMMAND,
    H2_PUSH_METHOD_REJECTED
};
typedef struct h2_push_stream {
    uint32_t base, bytes, get, header_address;
    uint16_t method, remaining;
    uint8_t subchannel, non_increasing;
} h2_push_stream;
typedef struct h2_push_fault {
    uint32_t address, word;
    uint16_t method;
    uint8_t subchannel;
} h2_push_fault;
/* Read one little-endian guest word. Return zero without supplying a value if
 * unreadable. The consumer returns zero without side effects for an unsupported
 * method. Neither callback may alter the parser, its bounds, or submitted PUT. */
typedef int (*h2_push_read)(void *opaque, uint32_t address, uint32_t *word);
typedef int (*h2_push_emit)(void *opaque, uint8_t subchannel, uint16_t method,
                           uint32_t value, uint32_t source_address);
int h2_push_init(h2_push_stream *stream, uint32_t base, uint32_t bytes);
/* One bounded ring allocation, increasing/non-increasing packets and jumps.
 * No implicit wrap, CALL/RETURN, DMA translation, cache, IRQ, or GPU completion.
 * An accepted method advances GET; rejection leaves GET on its data word.
 * NEED_DATA preserves a packet split across PUT updates. */
enum h2_push_result h2_push_run(h2_push_stream *stream, uint32_t put,
                                uint32_t word_budget, h2_push_read read_word,
                                h2_push_emit emit, void *opaque, h2_push_fault *fault);
