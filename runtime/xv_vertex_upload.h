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
#include "xv_record_prefix.h"
#if XV_RECORD_PREFIX
/* Recorder only. Copy a concrete token; consumers never reread pool metadata
 * while suffix uploads append or update the slot's current token. */
void xv_vertex_upload_seal_token(unsigned slot,uint32_t *ticket,unsigned *pending);
void xv_vertex_upload_wait_token(uint32_t ticket,unsigned pending);
#endif
const void *xv_vertex_upload_readback(unsigned slot, const void *ptr);
/* Caller must own the retired slot before reset/shutdown. */
void xv_vertex_upload_reset(unsigned slot);
void xv_vertex_upload_shutdown(void);
void xv_vertex_upload_report(unsigned frames);
#endif
