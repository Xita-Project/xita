#pragma once
#include "xv_shader.h"

/* Synthetic identity used by tools/ps_pipeline.py for pretransformed vertices. */
#define XV_PASSTHROUGH_HASH 0xFFFFFFFEu
static const xv_attr_desc_t xv_passthrough_attrs[] = {
    { "position", 0, 0 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 0 },
    { "color0", 0, 3 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 3 },
    { "color1", 0, 4 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 4 },
    { "texcoord0", 0, 9 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 9 },
    { "texcoord1", 0, 10 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 10 },
    { "texcoord2", 0, 11 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 11 },
    { "texcoord3", 0, 12 * 16, SCE_GXM_ATTRIBUTE_FORMAT_F32, 4, 12 },
};
static const xv_vs_desc_t xv_vs_passthrough = {
    "app0:shaders/xv_passthrough.gxp", 1, {256, 0, 0, 0},
    7, xv_passthrough_attrs, 0, 4, 0, 0, XV_PASSTHROUGH_HASH, 0
};
