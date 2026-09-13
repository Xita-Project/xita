#include "host_channel.h"
#include <string.h>

static int read_command(void *opaque, uint32_t offset, uint32_t *word)
{
    h2_host_channel *c = opaque;
    uint32_t physical;
    return h2_dma_resolve(&c->dma, offset, 4, 0, c->clear.physical_bytes, &physical) &&
           c->read_physical(c->opaque, physical, word);
}
static int execute_method(void *opaque, uint8_t subchannel, uint16_t method,
                           uint32_t value, uint32_t source)
{
    h2_host_channel *c = opaque;
    if (subchannel < 8 && c->commands.bound[subchannel] == 1 && method == 0x100 &&
        (value & 31) == 1 && c->software_flip)
        return c->software_flip(c->opaque, value, source);
    return h2_command_method(&c->commands, &c->clear, subchannel, method, value, source);
}
int h2_host_channel_init(h2_host_channel *c, const h2_dma_object *dma,
                         uint32_t ring, uint32_t bytes, h2_push_read read_physical,
                         h2_instance_read read_instance, h2_physical_map map_physical,
                         void *opaque, uint32_t physical_bytes)
{
    h2_host_channel candidate;
    uint32_t physical;
    /* The pinned constructor starts DMA GET at zero, writes a single bootstrap
     * jump there, then submits the allocated ring. No arbitrary DMA base alias. */
    if (!c || !dma || dma->address || !read_physical ||
        !h2_dma_resolve(dma, 0, 4, 0, physical_bytes, &physical) ||
        !h2_dma_resolve(dma, ring, bytes, 0, physical_bytes, &physical)) return 0;
    memset(&candidate, 0, sizeof candidate);
    if (!h2_push_init(&candidate.stream, ring, bytes) ||
        !h2_kelvin_clear_init(&candidate.clear, read_instance, map_physical,
                              opaque, physical_bytes, 0)) return 0;
    candidate.dma = *dma;
    candidate.read_physical = read_physical;
    candidate.opaque = opaque;
    candidate.bootstrap = 1;
    *c = candidate;
    return 1;
}
uint32_t h2_host_channel_get(const h2_host_channel *c)
{ return c->bootstrap ? 0 : c->stream.get; }
enum h2_push_result h2_host_channel_submit(h2_host_channel *c, uint32_t put,
                                          uint32_t budget, h2_push_fault *fault)
{
    if (fault) memset(fault, 0, sizeof *fault);
    if (!c || (put & 3) || put < c->stream.base ||
        (uint64_t)put > (uint64_t)c->stream.base + c->stream.bytes)
        return H2_PUSH_BAD_STATE;
    c->put = put;
    if (c->bootstrap) {
        uint32_t jump;
        if (!budget) return H2_PUSH_BUDGET_EXHAUSTED;
        if (!read_command(c, 0, &jump)) return H2_PUSH_MEMORY_FAULT;
        if (jump != (c->stream.base | 1u)) {
            if (fault) fault->word = jump;
            return H2_PUSH_UNSUPPORTED_COMMAND;
        }
        c->bootstrap = 0;
        --budget;
    }
    return h2_push_run(&c->stream, put, budget, read_command, execute_method, c, fault);
}
