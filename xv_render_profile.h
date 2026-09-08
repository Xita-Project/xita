#ifndef XV_RENDER_PROFILE_H
#define XV_RENDER_PROFILE_H

#include <stdint.h>

/* Pump-thread elapsed time, split into mutually exclusive stages. Submission
 * includes API-internal stalls and preemption; none of these are GPU timers.
 * Call only from the GXM owner. Stages outside a measured frame are ignored. */
enum xv_render_stage {
    XV_RENDER_SUBMIT,
    XV_RENDER_PREVIOUS_FINISH,
    XV_RENDER_TARGET_FINISH,
    XV_RENDER_FRAME_FINISH,
    XV_RENDER_DISPLAY_QUEUE,
    XV_RENDER_RETIRE,
    XV_RENDER_STAGE_COUNT
};

void xv_render_profile_begin(uint32_t mesh_frame);
void xv_render_profile_stage(enum xv_render_stage stage);
void xv_render_profile_end(void);

/* Nested attribution within SUBMIT. These spans are subsets of submit time,
 * not additional frame costs and not GPU execution timers. */
enum xv_render_call {
    XV_RENDER_SCENE_BEGIN, XV_RENDER_SCENE_END,
    XV_RENDER_VERTEX_UNIFORM, XV_RENDER_FRAGMENT_UNIFORM,
    XV_RENDER_DRAW, XV_RENDER_SHADER_LOOKUP, XV_RENDER_CALL_COUNT
};
uint64_t xv_render_profile_call_begin(void);
void xv_render_profile_call_end(enum xv_render_call call, uint64_t token);
/* Target 0 is the game backbuffer, 1..8 are offscreen slots, 9 is upscale.
 * Target timings are subsets of the existing EndScene timer. */
void xv_render_profile_scene_end(unsigned target, uint64_t token);
void xv_render_profile_work(uint32_t shader, unsigned indices, int no_alpha);
void xv_render_profile_depth_only(unsigned indices);
void xv_render_profile_cutout(unsigned indices);
#define XV_RENDER_CALL(kind, expression) __extension__ ({ \
    uint64_t xv_call_token_ = xv_render_profile_call_begin(); \
    __auto_type xv_call_result_ = (expression); \
    xv_render_profile_call_end((kind), xv_call_token_); \
    xv_call_result_; \
})
#define XV_RENDER_END(target, expression) __extension__ ({ \
    uint64_t xv_end_token_ = xv_render_profile_call_begin(); \
    __auto_type xv_end_result_ = (expression); \
    xv_render_profile_scene_end((target), xv_end_token_); \
    xv_end_result_; \
})

#endif
