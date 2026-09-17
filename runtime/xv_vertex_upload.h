#ifndef XV_VERTEX_UPLOAD_H
#define XV_VERTEX_UPLOAD_H
#include <stdint.h>
#include "xv_vertex_refs.h"
#include "xv_packed_vertex.h"
#if XV_PACKED_VERTEX_LAYOUT
/* Source is vertices*32 bytes; result owns vertices*16 bytes. Full-prefix
 * equality deliberately declines sparse reuse in this bounded prototype. */
const void *xv_vertex_upload_packed(unsigned slot,const void *source,unsigned vertices);
#endif

const void *xv_vertex_upload(unsigned slot, const void *source, unsigned bytes);
/* refs must be built from the exact retained index list, with a validated
 * fetch-within-stride layout. Metadata bounds cannot verify an invented mask
 * against indices that this API does not receive. Unfetched records may retain
 * old bytes; every subsequent draw must validate its own references/full span. */
const void *xv_vertex_upload_referenced(unsigned slot, const void *source, unsigned bytes,
                                       unsigned stride, const xv_vertex_refs *refs);
int xv_vertex_references_enabled(void);
void xv_vertex_references_override(int enabled);
/* Serialized recording thread only; -1 restores the configured default. */
void xv_vertex_upload_override(int enabled);
void xv_vertex_compare_override(int enabled);
/* Exact multi-vector load experiment; controls require a drained recorder. */
int xv_vertex_blocks_available(void);
int xv_vertex_blocks_enabled(void);
void xv_vertex_blocks_override(int enabled);
void xv_snapshot_worker_override(int enabled);
void xv_vertex_copy_override(int enabled);
int xv_vertex_worker_enabled(void);
void xv_vertex_worker_override(int enabled);
/* Seal on the recorder before publishing; wait on the pump before any draw.
 * Diagnostic CPU reads use the immutable mirror, not a pending GPU copy. */
void xv_vertex_upload_seal(unsigned slot);
void xv_vertex_upload_wait(unsigned slot);
const void *xv_vertex_upload_readback(unsigned slot, const void *ptr);
/* Caller must own the retired slot before reset/shutdown. */
void xv_vertex_upload_reset(unsigned slot);
void xv_vertex_upload_shutdown(void);
void xv_vertex_upload_report(unsigned frames);
#endif
