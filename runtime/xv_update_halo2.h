#pragma once
#include "xv_update.h"
enum { XV_HALO2_UPDATE_LIMIT=128*1024*1024 };
/* Independent fixed-path transaction store for the second game. */
void xv_halo2_update_init(void);
int xv_halo2_update_begin(unsigned size,const char *sha,const char *contract);
int xv_halo2_update_chunk(unsigned offset,const void *data,unsigned size);
int xv_halo2_update_finish(void);
void xv_halo2_update_close(void);
int xv_halo2_update_request(int rollback);
unsigned xv_halo2_update_requested(void);
void xv_halo2_update_status(char *out,size_t size);
void xv_halo2_update_json(char *out,size_t size);
int xv_halo2_update_boot(void);
int xv_halo2_update_confirm(unsigned slot);
static inline unsigned xv_updates_requested(void)
{ return xv_update_requested() || xv_halo2_update_requested(); }
