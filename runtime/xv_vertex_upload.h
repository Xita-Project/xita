#ifndef XV_VERTEX_UPLOAD_H
#define XV_VERTEX_UPLOAD_H
#include <stdint.h>
#include "xv_vertex_refs.h"
#include "xv_packed_vertex.h"
#ifndef XV_VERTEX_CAPTURE_PACKED
#define XV_VERTEX_CAPTURE_PACKED 0
#endif
#if XV_VERTEX_CAPTURE_PACKED != 0 && XV_VERTEX_CAPTURE_PACKED != 1
#error XV_VERTEX_CAPTURE_PACKED must be 0 or 1
#endif
#if XV_VERTEX_CAPTURE_PACKED && !XV_PACKED_VERTEX_LAYOUT
#error Compact capture requires the qualified packed vertex layout
#endif
#if XV_VERTEX_CAPTURE_PACKED
/* Already compacted private bytes, vertices*16 long. Cache identity remains
 * the original source; the GPU representation matches upload_packed exactly. */
const void *xv_vertex_upload_compact_snapshot(unsigned slot,const void *identity,
    const void *snapshot,unsigned bytes);
#endif
#if XV_PACKED_VERTEX_LAYOUT
/* Source is vertices*32 bytes; result owns vertices*16 bytes. Full-prefix
 * equality deliberately declines sparse reuse in this bounded prototype. */
const void *xv_vertex_upload_packed(unsigned slot,const void *source,unsigned vertices);
#endif

const void *xv_vertex_upload(unsigned slot, const void *source, unsigned bytes);
/* Separate comparison input from cache identity for an owned capture. Identity
 * is never dereferenced; every reuse still compares this draw's captured bytes.
 * The snapshot must cover bytes (including 32-byte input for packed records),
 * and remain immutable until this synchronous call returns. */
const void *xv_vertex_upload_snapshot(unsigned slot,const void *identity,
    const void *snapshot,unsigned bytes,unsigned stride,const xv_vertex_refs *refs,unsigned packed);
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
