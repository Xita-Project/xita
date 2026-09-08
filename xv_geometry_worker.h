#ifndef XV_GEOMETRY_WORKER_H
#define XV_GEOMETRY_WORKER_H
#include <stdint.h>
/* Single caller; input is private CPU scratch, and all work joins at return. */
void xv_geometry_sort_parallel(int32_t *values, unsigned count);
void xv_geometry_worker_report(void);
void xv_geometry_worker_shutdown(void);
#endif
