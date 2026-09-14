#ifndef XV_UPLOAD_WORKER_H
#define XV_UPLOAD_WORKER_H
#include <stdint.h>

/* One recording producer, one core-0 consumer. Source and destination ranges
 * must remain owned and immutable until the returned ticket completes. This
 * copies native snapshots only; it never calls GXM. */
int xv_upload_worker_submit(void *dst, const void *src, unsigned bytes, uint32_t *ticket);
/* Synchronous, non-overlapping cached snapshot: lend the source only for this
 * call, copy disjoint halves on the owner and idle worker, and join before
 * returning. On 0, no bytes were changed and the caller must copy everything.
 * The source may be guest memory, but cannot change until this call returns.
 * No guest pointer survives this function. Requires the recording producer. */
int xv_upload_worker_snapshot(void *dst, const void *src, unsigned bytes);
void xv_upload_worker_wait(uint32_t ticket);
void xv_upload_worker_report(unsigned frames);
/* Stop producers/consumers before shutdown; all queued copies are joined. */
void xv_upload_worker_shutdown(void);
#endif
