#ifndef XV_VERTEX_UPLOAD_H
#define XV_VERTEX_UPLOAD_H
#include <stdint.h>

const void *xv_vertex_upload(unsigned slot, const void *source, unsigned bytes);
/* Serialized recording thread only; -1 restores the configured default. */
void xv_vertex_upload_override(int enabled);
void xv_vertex_compare_override(int enabled);
void xv_vertex_copy_override(int enabled);
/* Caller must own the retired slot before reset/shutdown. */
void xv_vertex_upload_reset(unsigned slot);
void xv_vertex_upload_shutdown(void);
void xv_vertex_upload_report(unsigned frames);
#endif
