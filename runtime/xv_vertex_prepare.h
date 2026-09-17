#ifndef XV_VERTEX_PREPARE_H
#define XV_VERTEX_PREPARE_H
#include "xv_vertex_refs.h"
#include "xv_packed_vertex.h"

/* One recording owner. Inputs and references are borrowed until finish; the
 * owner may prepare independent material state but must not resume the guest,
 * access upload pools, publish/reset a slot, or submit another batch meanwhile.
 * The worker writes only this batch's results and the existing upload pools. */
#define XV_VERTEX_PREPARE_STREAMS 16u
typedef struct {
    const void *source, *result;
    unsigned bytes, stride;
    const xv_vertex_refs *refs;
#if XV_PACKED_VERTEX_LAYOUT
    unsigned packed;
#endif
} xv_vertex_prepare_stream;
typedef struct {
    unsigned slot, count;
    int ok;
    xv_vertex_prepare_stream streams[XV_VERTEX_PREPARE_STREAMS];
} xv_vertex_prepare_batch;

/* Disabled/unavailable/small batches execute synchronously. Always finish. */
void xv_vertex_prepare_begin(xv_vertex_prepare_batch *batch);
int xv_vertex_prepare_finish(xv_vertex_prepare_batch *batch);
/* Recording owner only at a drained boundary. Negative value restores both
 * configured enable state and cutoff; comparison cutoff is explicit. */
int xv_vertex_prepare_available(void);
void xv_vertex_prepare_override(int value, unsigned minimum_bytes);
void xv_vertex_prepare_shutdown(void);
void xv_vertex_prepare_report(unsigned frames);
#endif
