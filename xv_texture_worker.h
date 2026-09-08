#ifndef XV_TEXTURE_WORKER_H
#define XV_TEXTURE_WORKER_H
#include "xv_texture_decode.h"

/* Single guest caller. Large conversions are split with a persistent core-0
 * worker; the call joins before returning or allowing guest state to change. */
int xv_texture_decode(const xv_texture_job *job);
void xv_texture_worker_shutdown(void);
void xv_texture_worker_report(void);
#endif
