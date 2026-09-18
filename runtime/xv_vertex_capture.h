#ifndef XV_VERTEX_CAPTURE_H
#define XV_VERTEX_CAPTURE_H
#include "xv_vertex_prepare.h"

/* Single recording owner. Accepted jobs own copies of all consumed bytes and
 * reference masks before returning. Targets/context are frame-owned command
 * storage; they must survive until drain. Completion callbacks run only on the
 * recording owner, before command publication. Failed jobs publish no streams.
 * Only this worker may touch the upload pools while jobs remain outstanding. */
int xv_vertex_capture_submit(const xv_vertex_prepare_batch *batch,
    const void ***targets,void (*complete)(void *,int),void *context);
/* Mandatory before any synchronous upload, diagnostics/readback, override,
 * frame publication/reset, report, or shutdown. Joins CPU preparation only;
 * existing upload-copy tickets and GPU slot retirement remain separate. */
void xv_vertex_capture_drain(void);
void xv_vertex_capture_shutdown(void);
void xv_vertex_capture_report(unsigned frames);
#endif
