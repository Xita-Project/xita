#ifndef XV_SETTINGS_H
#define XV_SETTINGS_H
#include "dashboard/xv_dash.h"
enum { XV_SETTINGS_TOGGLE = 1u << 6 };
/* Recording-thread entry points. Input returns nonzero while Halo must receive
 * neutral input. Timestamps are microseconds; directions repeat by time. */
int xv_settings_input(uint32_t buttons, uint64_t now);
void xv_settings_frame(void);
void xv_settings_snapshot(xv_dash_graphics_view *view);
#endif
