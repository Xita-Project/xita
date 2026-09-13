#include "command_snapshot.h"
#include <inttypes.h>

static void words(FILE *out, const char *name, const uint32_t *data, unsigned count)
{
    fprintf(out, ",\n\"%s\":[", name);
    for (unsigned i = 0; i < count; ++i)
        fprintf(out, "%s%" PRIu32, i ? "," : "", data[i]);
    fputc(']', out);
}
static void vectors(FILE *out, const char *name, const uint32_t (*data)[4], unsigned count)
{
    fprintf(out, ",\n\"%s\":[", name);
    for (unsigned i = 0; i < count; ++i)
        fprintf(out, "%s[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]",
                i ? "," : "", data[i][0], data[i][1], data[i][2], data[i][3]);
    fputc(']', out);
}
int h2_command_snapshot(FILE *out, const h2_host_channel *channel)
{
    if (!out || !channel) return 0;
    const h2_command_state *s = &channel->commands;
    const h2_kelvin_clear *c = &channel->clear;
    const h2_push_stream *p = &channel->stream;
    fputs("{\n\"schema_version\":1,\"scope\":\"tracked host channel state; not a complete NV2A draw state\"", out);
#define U32(name, value) fprintf(out, ",\n\"%s\":%" PRIu32, name, (uint32_t)(value))
#define U64(name, value) fprintf(out, ",\n\"%s\":%" PRIu64, name, (uint64_t)(value))
    U32("ring_base", p->base); U32("ring_bytes", p->bytes);
    U32("get", channel->bootstrap ? 0 : p->get); U32("put", channel->put);
    U32("header_address", p->header_address); U32("method", p->method);
    U32("remaining", p->remaining); U32("subchannel", p->subchannel);
    U32("non_increasing", p->non_increasing); U32("bootstrap", channel->bootstrap);
    U32("dma_address", channel->dma.address); U32("dma_limit", channel->dma.limit);
    U32("dma_class", channel->dma.object_class); U32("dma_target", channel->dma.target);
    U32("physical_bytes", c->physical_bytes); U32("object_instance", c->object_instance);
    U32("bound_subchannels", c->bound_subchannels); U32("has_object", c->has_object);
    U32("dma_color", c->dma_color); U32("dma_zeta", c->dma_zeta);
    U32("has_color_dma", c->has_color_dma); U32("has_zeta_dma", c->has_zeta_dma);
    U32("clip_horizontal", c->clip_horizontal); U32("clip_vertical", c->clip_vertical);
    U32("surface_format", c->format); U32("surface_pitch", c->pitch);
    U32("color_offset", c->color_offset); U32("zeta_offset", c->zeta_offset);
    U32("clear_color", c->clear_color); U32("clear_zstencil", c->clear_zstencil);
    U32("clear_horizontal", c->clear_horizontal); U32("clear_vertical", c->clear_vertical);
    U64("completed_clears", c->completed_clears); U64("written_pixels", c->written_pixels);
    U32("dma_valid", s->dma_valid); U32("constant_load", s->constant_load);
    U32("program_load", s->program_load); U32("program_start", s->program_start);
    U32("execution_mode", s->execution_mode); U32("context_write", s->context_write);
    U32("flip_read", s->flip_read); U32("flip_write", s->flip_write);
    U32("flip_modulo", s->flip_modulo); U32("semaphore_offset", s->semaphore_offset);
    U64("semaphore_releases", s->semaphore_releases);
    U32("last_semaphore_address", s->last_semaphore_address);
    U32("last_semaphore_value", s->last_semaphore_value);
    U32("software_valid", s->software_valid); U64("software_updates", s->software_updates);
    U32("dxt1_noise", s->dxt1_noise); U32("zcull_debug5", s->zcull_debug5);
    U32("rop_control", s->rop_control); U32("provoking_vertex", s->provoking_vertex);
    U32("edge_flag", s->edge_flag); U32("compress_depth", s->compress_depth);
    U32("shader_inputs", s->shader_inputs); U32("shadow_slope", s->shadow_slope);
    U32("m2mf_notifier", s->m2mf_notifier); U32("blit_operation", s->blit_operation);
    U32("pattern_color", s->pattern_color);
#undef U32
#undef U64
    /* Field-by-field serialization deliberately excludes host pointers and C
     * padding. Float constants remain exact uint32 bit patterns, including NaNs.
     * Unwritten program/constant slots remain zero-initialized; the format does
     * not claim those slots are valid or that every written slot is active. */
    fputs(",\n\"objects\":[", out);
    for (unsigned i = 0; i < 5; ++i) {
        const h2_command_object *o = &s->objects[i];
        fprintf(out, "%s{\"present\":%u,\"instance\":%" PRIu32, i ? "," : "", o->present, o->instance);
        words(out, "context", o->context, 4); fputc('}', out);
    }
    fputs("],\n\"bound\":[", out);
    for (unsigned i = 0; i < 8; ++i) fprintf(out, "%s%u", i ? "," : "", s->bound[i]);
    fputc(']', out);
    words(out, "dma", s->dma, 11);
    words(out, "surfaces_dma", s->surfaces_dma, 2);
    words(out, "blit_context", s->blit_context, 7);
    words(out, "vertex4ub", s->vertex4ub, 16);
    words(out, "setup", s->setup, 0x2000 / 4);
    words(out, "setup_valid", s->setup_valid, 0x2000 / 4 / 32);
    vectors(out, "constants", s->constants, 192);
    vectors(out, "program", s->program, 136);
    fputs("\n}\n", out);
    return !ferror(out);
}
