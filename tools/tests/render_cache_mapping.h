/* ARM fixture: active snapshot deliberately differs from the global roots. */
#pragma once
#include "xv_x86rt.h"
extern uint32_t *xv_test_active_pt;
extern uint8_t *xv_test_active_image;
#undef X_PT
#define X_PT xv_test_active_pt
#undef X_IMG_BASE
#define X_IMG_BASE xv_test_active_image
