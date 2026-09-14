#ifndef XV_VERTEX_UPLOAD_H
#define XV_VERTEX_UPLOAD_H
#include <stdint.h>
#include "xv_vertex_refs.h"

const void *xv_vertex_upload(unsigned slot, const void *source, unsigned bytes);
const void *xv_vertex_upload_referenced(unsigned slot, const void *source, unsigned bytes,
                                       unsigned stride, const xv_vertex_refs *refs);
int xv_vertex_references_enabled(void);
void xv_vertex_references_override(int enabled);
/* Serialized recording thread only; -1 restores the configured default. */
void xv_vertex_upload_override(int enabled);
void xv_vertex_compare_override(int enabled);
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
