/*
 * xv_scene.c - scene-pack loader + per-frame replay (see xv_scene.h, halo_scene_export.py)
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>

#include "xv_d3d.h"
#include "xv_scene.h"

#define XV_LOG(...)  sceClibPrintf("[xv/scene] " __VA_ARGS__)
#define MAX_DRAWS    256

typedef struct { uint32_t vb_struct, ib_raw, index_count, prim, tex_struct, flags; float world[12]; float uv_scale[2]; } draw_t;

static draw_t   g_draws[MAX_DRAWS];
static unsigned g_ndraws;
static float    g_cam[9];              /* eye xyz, target xyz, fov, znear, zfar */

/* --- minimal 4x4 math (row-major, D3D conventions, clip z in [0, w]) ------------- */
typedef struct { float m[4][4]; } mat4;

static mat4 mat_identity(void)
{
    mat4 r; memset(&r, 0, sizeof(r));
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

static mat4 mat_mul(const mat4 *a, const mat4 *b)      /* r = a * b (row vectors: v*a*b) */
{
    mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    return r;
}

static void v3_norm(float *v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static mat4 mat_lookat(const float *eye, const float *at, const float *up_in)
{
    float f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    v3_norm(f);
    float up[3] = { up_in[0], up_in[1], up_in[2] };
    float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
    v3_norm(s);
    float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    mat4 r = mat_identity();
    r.m[0][0] = s[0]; r.m[1][0] = s[1]; r.m[2][0] = s[2];
    r.m[0][1] = u[0]; r.m[1][1] = u[1]; r.m[2][1] = u[2];
    r.m[0][2] = f[0]; r.m[1][2] = f[1]; r.m[2][2] = f[2];          /* left-handed: +z forward */
    r.m[3][0] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r.m[3][1] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r.m[3][2] = -(f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    return r;
}

static mat4 mat_perspective(float fov_deg, float aspect, float zn, float zf)   /* D3D LH, z -> [0,1] */
{
    float yscale = 1.0f / tanf(fov_deg * 0.5f * 3.14159265f / 180.0f);
    mat4 r; memset(&r, 0, sizeof(r));
    r.m[0][0] = yscale / aspect;
    r.m[1][1] = yscale;
    r.m[2][2] = zf / (zf - zn);
    r.m[2][3] = 1.0f;
    r.m[3][2] = -zn * zf / (zf - zn);
    return r;
}

/* --- pack loading ---------------------------------------------------------------- */
static int read_all(const char *path, void **out, uint32_t *size)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    void *buf = malloc((size_t)sz);
    if (!buf) { sceIoClose(fd); return -1; }
    int got = sceIoRead(fd, buf, (SceSize)sz);
    sceIoClose(fd);
    if (got != (int)sz) { free(buf); return -1; }
    *out = buf; *size = (uint32_t)sz;
    return 0;
}

int xv_scene_load(const char *path)
{
    void *buf; uint32_t size;
    if (read_all(path, &buf, &size) != 0) {
        XV_LOG("no scene pack at %s\n", path);
        return 0;
    }
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t magic, version, nblobs;
    memcpy(&magic, p, 4); memcpy(&version, p + 4, 4); memcpy(&nblobs, p + 8, 4);
    if (magic != 0x43535658u /* 'XVSC' */ || version != 3) {
        XV_LOG("%s: bad pack header\n", path);
        free(buf);
        return 0;
    }
    uint32_t off = 12;
    for (uint32_t i = 0; i < nblobs; ++i) {
        uint32_t addr, len, foff;
        memcpy(&addr, p + off, 4); memcpy(&len, p + off + 4, 4); memcpy(&foff, p + off + 8, 4);
        off += 12;
        if (foff + len > size || (addr & 0x03FFFFFF) + len > (64u << 20)) {
            XV_LOG("blob %u out of range\n", i);
            continue;
        }
        memcpy(xv_guest_ptr(addr), p + foff, len);
        xv_gpu_flush(xv_guest_ptr(addr), len);
        XV_LOG("blob %u: %u KB -> guest 0x%08X\n", i, len >> 10, addr);
    }
    memcpy(&g_ndraws, p + off, 4); off += 4;
    if (g_ndraws > MAX_DRAWS) g_ndraws = MAX_DRAWS;
    memcpy(g_draws, p + off, g_ndraws * sizeof(draw_t)); off += g_ndraws * sizeof(draw_t);
    memcpy(g_cam, p + off, sizeof(g_cam));
    free(buf);
    XV_LOG("scene: %u draws, camera eye (%.0f %.0f %.0f) -> (%.0f %.0f %.0f) fov %.0f\n", g_ndraws,
           g_cam[0], g_cam[1], g_cam[2], g_cam[3], g_cam[4], g_cam[5], g_cam[6]);
    return (int)g_ndraws;
}

/* --- per-frame replay -------------------------------------------------------------- */
void xv_scene_frame(uint32_t frame, uint32_t vs_handle)
{
    /* Camera: slow orbit around the target so motion proves the pipeline is live. */
    float t = (float)frame * 0.004f;
    float eye[3] = { g_cam[0], g_cam[1], g_cam[2] };
    float at[3]  = { g_cam[3], g_cam[4], g_cam[5] };
    float dx = at[0] - eye[0], dy = at[1] - eye[1];
    float r = sqrtf(dx * dx + dy * dy);
    if (r > 1.0f) {
        float a = atan2f(dy, dx) + t;
        at[0] = eye[0] + r * cosf(a);
        at[1] = eye[1] + r * sinf(a);
    }
    float up[3] = { 0, 0, 1 };                       /* Halo is z-up */
    mat4 view = mat_lookat(eye, at, up);
    mat4 proj = mat_perspective(g_cam[6], 960.0f / 544.0f, g_cam[7], g_cam[8]);
    mat4 wvp  = mat_mul(&view, &proj);

    mat4 viewproj = wvp;
    float light[4] = { 0.3f, -0.5f, 0.8f, 0 };

    xv_d3d_Clear(X_D3DCLEAR_TARGET | X_D3DCLEAR_ZBUFFER, 0xFF050308u, 1.0f, 0);
    xv_d3d_SetRenderState_CullMode(X_D3DCULL_NONE);
    xv_d3d_SetVertexShader(vs_handle);
    xv_d3d_SetVertexShaderConstant(4, light, 1);
    xv_d3d_SetTextureStageState(0, X_D3DTSS_MINFILTER, X_D3DTEXF_LINEAR);
    xv_d3d_SetTextureStageState(0, X_D3DTSS_MAGFILTER, X_D3DTEXF_LINEAR);

    /* opaque first, then transparent */
    for (int pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < g_ndraws; ++i) {
            const draw_t *d = &g_draws[i];
            int transparent = (d->flags & 1) != 0;
            if (transparent != pass)
                continue;
            /* per-draw world (3x4 from the scenario placement) * view * proj ->
             * c[0..3] = rows of transpose(WVP) so that oPos.x = dot(v, c[0]) ... */
            mat4 world = mat_identity();
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    world.m[c][r] = d->world[r * 3 + c];      /* row vectors: v * world */
            world.m[3][0] = d->world[9]; world.m[3][1] = d->world[10]; world.m[3][2] = d->world[11];
            mat4 w = mat_mul(&world, &viewproj);
            float rows[16];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    rows[i * 4 + j] = w.m[j][i];
            xv_d3d_SetVertexShaderConstant(0, rows, 4);
            float uvs[4] = { d->uv_scale[0], d->uv_scale[1], 0, 0 };
            xv_d3d_SetVertexShaderConstant(5, uvs, 1);
            float lit[4] = { 0.3f, -0.5f, 0.8f, (d->flags & 4) ? 1.0f : 0.0f };   /* .w = unlit */
            xv_d3d_SetVertexShaderConstant(4, lit, 1);
            xv_d3d_SetRenderState_ZEnable(1);
            xv_d3d_SetRenderState_ZWriteEnable(!(d->flags & 2));
            xv_d3d_SetRenderState_AlphaBlendEnable(transparent);
            xv_d3d_SetRenderState_SrcBlend(X_D3DBLEND_SRCALPHA);
            xv_d3d_SetRenderState_DestBlend(X_D3DBLEND_INVSRCALPHA);
            xv_d3d_SetStreamSource(0, d->vb_struct, 32);
            xv_d3d_SetTexture(0, d->tex_struct);
            xv_d3d_DrawIndexedVertices(d->prim == 1 ? X_D3DPT_TRIANGLESTRIP : X_D3DPT_TRIANGLELIST,
                                       d->index_count, d->ib_raw);
        }
    }
}
