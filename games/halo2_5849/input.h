#pragma once
#include "xv_x86rt.h"
typedef struct h2_pad_sample {
    uint16_t buttons;
    uint8_t analog[8];
    int16_t axes[4];
} h2_pad_sample;
/* Real Vita platform sample; negative means host failure, zero means success. */
int h2_platform_pad(h2_pad_sample *sample, int initialize);
void h2_input_init(xctx *c);
void h2_input_open(xctx *c);
void h2_input_close(xctx *c);
void h2_input_state(xctx *c);
void h2_input_capabilities(xctx *c);
void h2_input_feedback(xctx *c);
