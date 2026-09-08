#ifndef XV_GPU_UPLOAD_H
#define XV_GPU_UPLOAD_H
#include <stdint.h>

/* These publication functions accept only CPU-written, GPU-mapped UNCACHED
 * upload storage. They drain stores; they do not clean cached guest memory.
 * Cached guest vertices are copied into owned uploads by xv_vertex_upload. */
void xv_gpu_flush(const void *ptr, uint32_t len);       /* guest 3D/texture queue */
void xv_gpu_flush_ui(const void *ptr, uint32_t len);    /* guest UI queue */
void xv_gpu_flush_pending(void);                      /* guest publication */
void xv_gpu_flush_pump(const void *ptr, uint32_t len);  /* pump writes, immediate */
void xv_gpu_write_barrier(void);
#endif
