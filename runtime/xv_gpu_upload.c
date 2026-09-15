#include "xv_gpu_upload.h"
#include <stdatomic.h>

/* GPU uploads are uncached. Range registration requires no cache cleaning;
 * publication still issues a DSB. Vertex preparation also issues its own DSB
 * on the writing core before releasing its source loan. */
void xv_gpu_write_barrier(void)
{
#if defined(__arm__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    atomic_thread_fence(memory_order_seq_cst);
#endif
}
void xv_gpu_flush(const void *ptr, uint32_t len)
{ (void)ptr; (void)len; }
void xv_gpu_flush_ui(const void *ptr, uint32_t len)
{ (void)ptr; (void)len; }
void xv_gpu_flush_pending(void)
{
    /* Also orders index/immediate buffers which need no range registration. */
    xv_gpu_write_barrier();
}
void xv_gpu_flush_pump(const void *ptr, uint32_t len)
{ if (ptr && len) xv_gpu_write_barrier(); }
