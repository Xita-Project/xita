#pragma once
#include <stddef.h>
enum { XV_UPDATE_LIMIT=64*1024*1024, XV_UPDATE_CHUNK=65536 };
/* Network thread alone owns transfer/storage operations. Other threads use
 * atomic status/request fields, never the transfer FILE or metadata buffers. */
void xv_update_init(void);
int xv_update_begin(unsigned size,const char *sha,const char *contract);
int xv_update_chunk(unsigned offset,const void *data,unsigned size);
int xv_update_finish(void);
void xv_update_close(void);
int xv_update_request(int rollback);
unsigned xv_update_requested(void);
void xv_update_status(char *out,size_t size);
void xv_update_json(char *out,size_t size);
/* Stable boot helper only: apply staged transaction, choose/mark an attempt.
 * Returns 0/1 slot, or -1; file paths are fixed, never supplied over HTTP. */
int xv_update_boot(void);
int xv_update_confirm(unsigned slot);
