#include "gpu_objects.h"

int h2_object_lookup(h2_instance_read read_word, void *opaque, uint32_t handle,
                     uint8_t channel, h2_gpu_object *object)
{
    if (!read_word || !object || channel > 31) return 0;
    h2_gpu_object candidate = {0};
    unsigned matches = 0;
    for (uint32_t offset = 0x10000; offset < 0x11000; offset += 8) {
        uint32_t stored_handle, context;
        if (!read_word(opaque, offset, &stored_handle) ||
            !read_word(opaque, offset + 4, &context)) return 0;
        if (!(context & 0x80000000u) || stored_handle != handle ||
            ((context >> 24) & 31) != channel) continue;
        if ((context & ~0x9F03FFFFu) || ++matches > 1) return 0;
        candidate.instance = (context & 0xFFFF) << 4;
        candidate.engine = (context >> 16) & 3;
        candidate.channel = channel;
    }
    if (matches != 1) return 0;
    *object = candidate;
    return 1;
}

int h2_dma_load(h2_instance_read read_word, void *opaque, uint32_t instance,
                h2_dma_object *object)
{
    if (!read_word || !object || (instance & 15) || instance > 0xFFFF0) return 0;
    uint32_t flags, limit, frame, duplicate;
    if (!read_word(opaque, instance, &flags) || !read_word(opaque, instance + 4, &limit) ||
        !read_word(opaque, instance + 8, &frame) || !read_word(opaque, instance + 12, &duplicate)) return 0;
    uint32_t object_class = flags & 0xFFF, target = (flags >> 16) & 3;
    if (object_class != 2 && object_class != 3 && object_class != 0x3D) return 0;
    if ((flags & 0x000FC000u) != (0x8000u | (target << 16)) ||
        (flags & 0x3000) != 0x3000 || (frame & 3) != 3 || frame != duplicate ||
        (target != 0 && target != 2)) return 0;
    h2_dma_object result = {
        .address = (frame & 0xFFFFF000u) | (flags >> 20), .limit = limit,
        .object_class = object_class, .target = target
    };
    *object = result;
    return 1;
}

int h2_dma_resolve(const h2_dma_object *object, uint32_t offset, uint32_t bytes,
                   int write, uint32_t physical_bytes, uint32_t *address)
{
    if (!object || !address || !bytes || (write != 0 && write != 1) ||
        (object->target != 0 && object->target != 2) ||
        (object->object_class != 0x3D && object->object_class != (write ? 3 : 2))) return 0;
    uint64_t last = (uint64_t)offset + bytes - 1;
    uint64_t end = (uint64_t)object->address + last;
    if (last > object->limit || end >= physical_bytes) return 0;
    *address = object->address + offset;
    return 1;
}
