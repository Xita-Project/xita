/* Independent implementation of the NV4/NV10 packet forms documented by
 * envytools hw/fifo/dma-pusher, restricted to one checked allocation. */
#include "push_stream.h"
#include <string.h>

static int valid_range(uint32_t base, uint32_t bytes)
{
    return bytes && !(base & 3) && !(bytes & 3) && bytes <= UINT32_MAX - base;
}

int h2_push_init(h2_push_stream *stream, uint32_t base, uint32_t bytes)
{
    if (!stream || !valid_range(base, bytes)) return 0;
    memset(stream, 0, sizeof *stream);
    stream->base = stream->get = base;
    stream->bytes = bytes;
    return 1;
}

static int in_range(const h2_push_stream *stream, uint32_t address, int allow_end)
{
    if ((address & 3) || address < stream->base) return 0;
    uint32_t offset = address - stream->base;
    return allow_end ? offset <= stream->bytes : offset < stream->bytes;
}

enum h2_push_result h2_push_run(h2_push_stream *stream, uint32_t put,
                                uint32_t word_budget, h2_push_read read_word,
                                h2_push_emit emit, void *opaque, h2_push_fault *fault)
{
    if (fault) memset(fault, 0, sizeof *fault);
    if (!stream || !read_word || !emit || !valid_range(stream->base, stream->bytes) ||
        !in_range(stream, stream->get, 1) || !in_range(stream, put, 1) ||
        stream->remaining > 0x7FF || stream->method > 0x1FFC ||
        (stream->method & 3) || stream->subchannel > 7 || stream->non_increasing > 1)
        return H2_PUSH_BAD_STATE;
    for (;;) {
        if (stream->get == put)
            return stream->remaining ? H2_PUSH_NEED_DATA : H2_PUSH_COMPLETE;
        if (!word_budget) return H2_PUSH_BUDGET_EXHAUSTED;
        uint32_t address = stream->get, word = 0;
        if (fault) {
            fault->address = address; fault->word = 0;
            fault->method = stream->method; fault->subchannel = stream->subchannel;
        }
        if (!in_range(stream, address, 0) || !read_word(opaque, address, &word))
            return H2_PUSH_MEMORY_FAULT;
        if (fault) fault->word = word;
        if (stream->remaining) {
            if (!emit(opaque, stream->subchannel, stream->method, word, address))
                return H2_PUSH_METHOD_REJECTED;
            --stream->remaining;
            if (!stream->non_increasing) stream->method = (stream->method + 4) & 0x1FFC;
            stream->get += 4;
        } else if ((word & 3) == 1 || (word & 0xE0000003u) == 0x20000000u) {
            uint32_t target = (word & 3) == 1 ? word & 0xFFFFFFFCu : word & 0x1FFFFFFCu;
            /* A jump to PUT may stop at the exclusive end. Reads never may. */
            if (!in_range(stream, target, 1)) return H2_PUSH_MEMORY_FAULT;
            stream->get = target;
        } else if ((word & 0xE0030003u) == 0 || (word & 0xE0030003u) == 0x40000000u) {
            stream->header_address = address;
            stream->method = word & 0x1FFC;
            stream->subchannel = (word >> 13) & 7;
            stream->remaining = (word >> 18) & 0x7FF;
            stream->non_increasing = (word >> 30) & 1;
            stream->get += 4;
        } else {
            /* CALL/RETURN and later-generation forms need an explicit contract. */
            return H2_PUSH_UNSUPPORTED_COMMAND;
        }
        --word_budget;
    }
}
