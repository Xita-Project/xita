#ifndef XV_UPLOAD_WORKER_H
#define XV_UPLOAD_WORKER_H
#include <stdint.h>

/* One recording producer, one core-0 consumer. Source and destination ranges
 * must remain owned and immutable until the returned ticket completes. This
 * copies native snapshots only; it never accesses guest memory or calls GXM. */
int xv_upload_worker_submit(void *dst, const void *src, unsigned bytes, uint32_t *ticket);
void xv_upload_worker_wait(uint32_t ticket);
void xv_upload_worker_report(unsigned frames);
/* Stop producers/consumers before shutdown; all queued copies are joined. */
void xv_upload_worker_shutdown(void);
#endif
