#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "xv_flare_clip.h"
#include "xv_shader.h"
#include "shaders/xv_layouts.h"
#define X_D3DPT_QUADLIST 8
typedef struct { float a[16][4]; } xd3d_im_vtx;
static struct { float vsc[192][4]; } xd3d_state;
static int g_mesh_path = 1;
static unsigned normal, registered, calls, total, synced;
static uint8_t recorded[72 * 40];
static uint32_t xv_d3d_handle_for_hash(uint32_t hash)
{ assert(hash == 0x405809D3u); normal++; return 2; }
static uint32_t xv_d3d_RegisterVertexShader(const xv_vs_desc_t *d)
{
    assert(normal && d->func_hash == 0x405809D3u);
    assert(d->nstreams == 1 && d->stride[0] == 40 && d->nattrs == 4);
    assert(d->attrs[3].stream == 0 && d->attrs[3].offset == 24);
    assert(d->attrs[3].vreg == 10 && d->attrs[3].components == 4);
    assert(d->attrs[3].format == SCE_GXM_ATTRIBUTE_FORMAT_F32);
    /* The streamed program's original layout must remain intact. */
    assert(xv_vs_halo_vs_56.stride[0] == 24);
    assert(xv_vs_halo_vs_56.attrs[3].stream == XV_CONST_STREAM);
    registered++; return 3;
}
static void xv_d3d_SetVertexShader(uint32_t h) { assert(h == 3); }
static void xv_d3d_SetAllConstants(const float c[192][4])
{ assert(c == xd3d_state.vsc); }
static void sync_draw_state(void) { synced++; }
static void xv_d3d_DrawImmediateStrided(uint32_t prim, const void *v, uint32_t n, uint32_t stride)
{
    assert(prim == X_D3DPT_QUADLIST && stride == 40 && n <= 64 && n % 4 == 0);
    assert(total + n <= 72);
    memcpy(recorded + total * stride, v, n * stride); total += n; calls++;
}
#include "flare_under_test.inc"
int main(void)
{
    xd3d_im_vtx v[68] = {0};
    for (unsigned i=0;i<68;i++) {
        v[i].a[0][0]=393+i; v[i].a[0][1]=338-i; v[i].a[0][2]=.710f;
        v[i].a[4][0]=i % 2; v[i].a[4][1]=1;
        v[i].a[9][0]=.4f; v[i].a[9][1]=.8f; v[i].a[9][2]=.4f; v[i].a[9][3]=1;
        for(unsigned j=0;j<4;j++) v[i].a[10][j]=(i+j)*.125f;
    }
    draw_immediate_flare(v, 68);
    assert(calls == 2 && total == 68 && registered == 1 && normal == 1 && synced == 1);
    for (unsigned i=0;i<68;i++) {
        float fields[10]; memcpy(fields,recorded+i*40,40);
        assert(fields[0]==v[i].a[0][0] && fields[1]==v[i].a[0][1] && fields[2]==.710f);
        assert(fields[3]==v[i].a[4][0] && fields[4]==1);
        uint32_t color; memcpy(&color,recorded+i*40+20,4); assert(color==0xff66cc66u);
        for(unsigned j=0;j<4;j++) assert(fields[6+j]==v[i].a[10][j]);
    }
    memset(v,0,sizeof v); /* submitted copies own every vertex and v10 value */
    float last; memcpy(&last,recorded+67*40+36,4); assert(last==70*.125f);
    draw_immediate_flare(v,4); assert(calls==3 && total==72 && registered==1);
    g_mesh_path=0; draw_immediate_flare(v,4); assert(calls==3);
    g_mesh_path=1; calls=total=synced=0; g_flare_seen=g_flare_culled=0;
    float (*rows)[4]=&xd3d_state.vsc[28];
    for(unsigned i=0;i<4;i++) rows[i][i]=1;
    for(unsigned i=0;i<4;i++) v[i].a[0][0]=2.0f+(i&1)*.2f;
    draw_immediate_flare(v,4);
    if (getenv("XITA_TEST_FLARE_CULL_DISABLED")) {
        assert(calls==1 && total==4 && synced==1 && !g_flare_culled);
        puts("PASS: disabled culling preserves the original draw path");
        return 0;
    }
    assert(!calls && !synced && g_flare_culled==1); /* rejected before draw setup */
    draw_immediate_flare(v,8); /* outside first quad, visible second quad */
    assert(calls==1 && total==4 && synced==1 && g_flare_culled==2 && g_flare_seen==3);
    float first_position; memcpy(&first_position,recorded,sizeof first_position); assert(first_position==0);
    /* A quad surrounding the viewport has no shared outside plane. */
    const float corners[4][2]={{-2,-2},{2,-2},{2,2},{-2,2}};
    for(unsigned i=0;i<4;i++) memcpy(v[i].a[0],corners[i],sizeof corners[i]);
    assert(!xv_flare_quad_outside(v,sizeof *v,rows));
    for(unsigned i=0;i<4;i++) v[i].a[0][0]=1.0001f; /* uncertain edge: retain */
    assert(!xv_flare_quad_outside(v,sizeof *v,rows));
    for(unsigned i=0;i<4;i++) v[i].a[0][0]=2;
    v[0].a[0][0]=NAN; assert(!xv_flare_quad_outside(v,sizeof *v,rows));
    v[0].a[0][0]=INFINITY; assert(!xv_flare_quad_outside(v,sizeof *v,rows));
    v[0].a[0][0]=2; rows[3][3]=-1; assert(!xv_flare_quad_outside(v,sizeof *v,rows));
    rows[3][3]=0; rows[3][2]=1; v[0].a[0][2]=-1;
    assert(!xv_flare_quad_outside(v,sizeof *v,rows));
    /* Check rejection against double-precision homogeneous planes across
     * varied affine transforms; retained cases may conservatively overdraw. */
    uint32_t rng=17; unsigned rejected=0;
    for(unsigned sample=0;sample<10000;sample++) {
        memset(rows,0,4*4*sizeof(float)); rows[3][3]=1;
        for(unsigned r=0;r<3;r++) for(unsigned j=0;j<4;j++) {
            rng=rng*1664525u+1013904223u; rows[r][j]=((int)(rng>>16)-32768)*.0001f;
        }
        float center[3];
        for(unsigned j=0;j<3;j++) { rng=rng*1664525u+1013904223u; center[j]=((int)(rng>>16)-32768)*.0001f; }
        for(unsigned i=0;i<4;i++) for(unsigned j=0;j<3;j++) v[i].a[0][j]=center[j]+(float)((i+j)%3)*.1f;
        if(!xv_flare_quad_outside(v,sizeof *v,rows)) continue;
        rejected++; unsigned common=15;
        for(unsigned i=0;i<4;i++) {
            double xy[2]={rows[0][3],rows[1][3]};
            for(unsigned r=0;r<2;r++) for(unsigned j=0;j<3;j++) xy[r]+=(double)v[i].a[0][j]*rows[r][j];
            common&=(xy[0]>1 ? 1u:0u)|(xy[0]<-1 ? 2u:0u)|(xy[1]>1 ? 4u:0u)|(xy[1]<-1 ? 8u:0u);
        }
        assert(common);
    }
    assert(rejected>1000);
    puts("PASS: flare packing/ownership, conservative clip planes, mixed batches, and skipped draw setup");
}
