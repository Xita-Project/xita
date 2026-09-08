#ifndef XV_DRAW_PROFILE_H
#define XV_DRAW_PROFILE_H
#include <stdint.h>

enum xv_draw_stage {
    XV_DRAW_SETUP, XV_DRAW_STATE, XV_DRAW_INDICES, XV_DRAW_PROGRAM,
    XV_DRAW_STREAMS, XV_DRAW_CONSTANTS, XV_DRAW_TEXTURES, XV_DRAW_DIAGNOSTICS,
    XV_DRAW_STAGE_COUNT
};
uint64_t xv_draw_profile_begin(void);
void xv_draw_profile_step(enum xv_draw_stage stage, uint64_t *stamp);
void xv_draw_profile_report(unsigned frames);
#endif
