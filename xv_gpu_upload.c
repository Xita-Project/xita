#include "xv_gpu_upload.h"
#include <stdatomic.h>

/* Only the serialized recording thread touches these queues. No pump or
 * texture worker ever appends to them. Uncached memory has no dirty CPU cache
 * lines to clean; a DSB completes its stores before GXM can consume them. */
static struct { uint64_t bytes; unsigned ranges; } scene, ui;
void xv_gpu_write_barrier(void)
{
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    atomic_thread_fence(memory_order_seq_cst);
#endif
}
void xv_gpu_flush(const void *ptr, uint32_t len)
{ if (ptr && len) { scene.bytes += len; scene.ranges++; } }
void xv_gpu_flush_ui(const void *ptr, uint32_t len)
{ if (ptr && len) { ui.bytes += len; ui.ranges++; } }
void xv_gpu_flush_pending(void)
{
    /* Also orders index/immediate buffers which need no range registration. */
    xv_gpu_write_barrier();
    scene.bytes = ui.bytes = 0;
    scene.ranges = ui.ranges = 0;
}
void xv_gpu_flush_pump(const void *ptr, uint32_t len)
{ if (ptr && len) xv_gpu_write_barrier(); }
