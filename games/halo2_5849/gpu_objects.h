/* Logical host lookup of the guest's existing instance/DMA objects.
 * This does not model the hardware hash-search cache or allocate new objects. */
#pragma once
#include <stdint.h>

typedef int (*h2_instance_read)(void *opaque, uint32_t instance_offset, uint32_t *word);
typedef struct h2_gpu_object {
    uint32_t instance;
    uint8_t engine, channel;
} h2_gpu_object;
typedef struct h2_dma_object {
    uint32_t address, limit;
    uint16_t object_class;
    uint8_t target;
} h2_dma_object;
/* The currently supported RAMHT is the observed 4KiB table at offset 0x10000.
 * Match handle and channel uniquely. Invalid/ambiguous entries are errors. */
int h2_object_lookup(h2_instance_read read_word, void *opaque, uint32_t handle,
                     uint8_t channel, h2_gpu_object *object);
/* Supported DMA layout is the driver's linear two-identical-page-entry form.
 * NVM/PCI address spaces share Xbox DRAM. Tiled/AGP/paged forms are unsupported. */
int h2_dma_load(h2_instance_read read_word, void *opaque, uint32_t instance,
                h2_dma_object *object);
/* Check class permissions, inclusive object limit and actual physical extent.
 * No masking/wrapping into RAM; the returned address is a physical byte offset. */
int h2_dma_resolve(const h2_dma_object *object, uint32_t offset, uint32_t bytes,
                   int write, uint32_t physical_bytes, uint32_t *address);
