#ifndef XV_VERTEX_CAPTURE_H
#define XV_VERTEX_CAPTURE_H
#include "xv_vertex_prepare.h"

/* Single recording owner. Accepted jobs own copies of all consumed bytes and
 * reference masks before returning. Targets/context are frame-owned command
 * storage; they must survive until drain. Completion callbacks run only on the
 * recording owner, before command publication. Failed jobs publish no streams.
 * Only this worker may touch the upload pools while jobs remain outstanding. */
/* Optional capture reuse compares current source bytes exactly before sharing
 * immutable staging/results. CPU snapshots may survive joined drains; ordinary
 * GPU results do not. Optional persistent uploads instead retain immutable GPU
 * versions until all referencing frame slots retire. Sparse masks retain
 * independent preparation; no guest source is assumed immutable. An optional
 * completed-result shortcut invokes success inline only after collecting all
 * preceding jobs and matching every input; seal/wait still owns GPU copies. */
int xv_vertex_capture_submit(const xv_vertex_prepare_batch *batch,
    const void ***targets,void (*complete)(void *,int),void *context);
/* Mandatory before any synchronous upload, diagnostics/readback, override,
 * frame publication/reset, report, or shutdown. Joins CPU preparation only;
 * existing upload-copy tickets and GPU slot retirement remain separate. */
void xv_vertex_capture_drain(void);
/* Recording owner after acquiring a GPU-retired slot. Drains CPU preparation
 * and releases only this slot's references to persistent immutable uploads. */
void xv_vertex_capture_begin_slot(unsigned slot);
void xv_vertex_capture_shutdown(void);
void xv_vertex_capture_report(unsigned frames);
#endif
