/*
 * xv_scene.h - Xita scene viewer: replays a scene pack exported from a Halo map
 * (halo_scene_export.py) through the D3D HLE, standing in for the recompiled game
 * until Stage 4 exists.  Everything it draws lives in guest RAM exactly as the game
 * would have it: the map's tag data at 0x803A6000 (pre-baked D3D vertex/index buffer
 * structs), textures with Xbox D3DTexture headers, raw u16 strips.
 */
#pragma once
#include <stdint.h>

/* Load app0:assets/<name> into guest RAM.  Returns number of draws, 0 if absent/invalid. */
int  xv_scene_load(const char *path);
/* Record one frame of D3D HLE calls (call from the game fiber, then xv_d3d_Swap). */
void xv_scene_frame(uint32_t frame, uint32_t vs_handle);
