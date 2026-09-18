#include "sprite_draw.h"
#include <string.h>
extern void xv_mark_written(const void *host, uint32_t bytes) __attribute__((weak));
#define BYTES (640u * 480u * 4u)
static int overlaps(uintptr_t a, uint32_t an, uintptr_t b, uint32_t bn)
{ return a < b + bn && b < a + an; }

static int pipeline(const h2_sprite_draw *q, const h2_command_state *s,
                      const h2_kelvin_clear *c)
{
    const h2_sprite_contract *r = q->contract;
    if (!r || !q->render || s->program_start || s->execution_mode != 6 || s->context_write ||
        s->software_valid != 7 || s->dxt1_noise || s->zcull_debug5 || s->rop_control ||
        s->provoking_vertex != 1 || s->edge_flag != 1 || s->compress_depth != 1 ||
        s->shader_inputs || s->shadow_slope != 0x7F800000 ||
        c->format != 0x128 || c->clip_horizontal != (640u << 16) ||
        c->clip_vertical != (480u << 16) || c->pitch != 0x0A000A00 ||
        memcmp(s->program, r->program, sizeof r->program) ||
        memcmp(&s->constants[10], r->constants, sizeof r->constants) ||
        memcmp(s->setup_valid, r->setup_valid, sizeof r->setup_valid)) return 0;
    /* The trusted pinned reference must also remain inside the backend's
     * explicit framebuffer/sampler operations; changing a reference file
     * cannot grant depth writes, alpha tests, other blending or formats. */
    static const uint32_t required[][2] = {
        {0x300,0},{0x304,1},{0x308,1},{0x30C,0},{0x314,0},{0x32C,0},
        {0x344,0x302},{0x348,0x303},{0x350,0x8006},{0x358,0x00010101},
        {0x35C,0},{0x398,0x4B7FFFFF},{0x39C,0x405},{0x3A0,0x900},{0x147C,0},
        {0x1B08,0x00030303},{0x1B0C,0x4003FFC0},{0x1B14,0x02062000},
        {0x1E70,1},{0x1E74,0}
    };
    for (unsigned i = 0; i < sizeof required / sizeof *required; ++i)
        if (s->setup[required[i][0] / 4] != required[i][1]) return 0;
    for (unsigned i = 0; i < 16; ++i)
        if (s->setup[(0x1760 + i*4)/4] != (i < 2 ? 0x22u : i == 2 ? 0x40u : 2u)) return 0;
    uint32_t format=s->setup[0x1B04/4];
    if ((format & ~0x0FF00003u) != 0x10E28u ||
        (format & 3) < 1 || (format & 3) > 2 ||
        ((format >> 20) & 15) > 10 || ((format >> 24) & 15) > 10) return 0;
    for (unsigned i = 0; i < 2048; ++i) {
        unsigned method = i * 4;
        if (method >= 0x1B20 && method <= 0x1BE0 && (method & 63) == 32) {
            if (s->setup[i] & 0x32) return 0; /* non-indexed palette: no fetch */
        } else if (method != 0x1B00 && method != 0x1B04 && s->setup[i] != r->setup[i]) return 0;
    }
    return 1;
}


/* Only the live stage0 fetch and RGB destination are mapped. Disabled texture
 * stages and depth/stencil never grant a memory access. */
static int resources(const h2_command_state *s, const h2_kelvin_clear *c,
                     h2_sprite_request *r, uint8_t **destination)
{
    if (!c->has_color_dma || !c->read_instance || !c->map_physical ||
        !h2_dxt23_texture_rect_read(s,c,0,&r->texture0) ||
        r->texture0.bytes > 8192 || (r->texture0.physical & 15) ||
        (uintptr_t)r->texture0.blocks > UINTPTR_MAX-r->texture0.bytes) return 0;
    h2_dma_object dma; uint32_t color, readable;
    if (!h2_dma_load(c->read_instance,c->opaque,c->dma_color,&dma) ||
        !h2_dma_resolve(&dma,c->color_offset,BYTES,0,c->physical_bytes,&readable) ||
        !h2_dma_resolve(&dma,c->color_offset,BYTES,1,c->physical_bytes,&color) ||
        readable != color || (color & 3) ||
        overlaps(color,BYTES,r->texture0.physical,r->texture0.bytes)) return 0;
    if (c->check_attachment && !c->check_attachment(c->opaque,color,BYTES,2560,0,c->format)) return 0;
    uint8_t *target=c->map_physical(c->opaque,color,BYTES);
    if (!target || (uintptr_t)target > UINTPTR_MAX-BYTES ||
        overlaps((uintptr_t)target,BYTES,(uintptr_t)r->texture0.blocks,r->texture0.bytes)) return 0;
    r->destination=target; *destination=target; return 1;
}
static float as_float(uint32_t value)
{ float f; memcpy(&f,&value,4); return f; }
static int rectangle(const h2_sprite_draw *q, h2_sprite_vertex vertices[4])
{
    float xy[4][2]; int corners=1, zero_uv=1;
    for (unsigned v=0;v<4;++v) {
        const uint32_t *w=q->words+v*5;
        for (unsigned k=0;k<2;++k) {
            xy[v][k]=as_float(w[k]);
            if (!(xy[v][k] >= -2048 && xy[v][k] <= 2048)) return 0;
        }
        /* Support the original full-texture corners or all-positive-zero UVs.
         * Do not combine these patterns per vertex or substitute a solid fill:
         * the original shader still samples texture0 and multiplies its color. */
        if (w[2] != (v==1||v==2 ? 0x3F800000u : 0) ||
            w[3] != (v>=2 ? 0x3F800000u : 0)) corners=0;
        if (w[2] || w[3]) zero_uv=0;
        if (w[4] != q->words[4]) return 0;
        for (unsigned a=0;a<2;++a) {
            memcpy(vertices[v].attribute[a],w+a*2,8);
            vertices[v].attribute[a][2]=0; vertices[v].attribute[a][3]=1;
        }
        static const unsigned shifts[]={16,8,0,24};
        for (unsigned k=0;k<4;++k) vertices[v].attribute[2][k]=((w[4]>>shifts[k])&255)/255.0f;
    }
    return (corners || zero_uv) && xy[0][0] < xy[1][0] && xy[0][1] < xy[3][1] &&
        xy[0][0]==xy[3][0] && xy[1][0]==xy[2][0] &&
        xy[0][1]==xy[1][1] && xy[2][1]==xy[3][1];
}
static int finish(h2_sprite_draw *q,h2_command_state *s,h2_kelvin_clear *c)
{
    h2_sprite_request r; uint8_t *destination;
    if(q->count != 20 || q->completed == UINT64_MAX || !pipeline(q,s,c) ||
        !rectangle(q,r.vertices) || !resources(s,c,&r,&destination)) return 0;
    memcpy(r.constants,&s->constants[10],sizeof r.constants);
    for(unsigned i=0;i<18;++i) {
        unsigned method=i<16?0xA60+i*4:0x1E20+(i-16)*4;
        uint32_t color=s->setup[method/4]; static const unsigned shifts[]={16,8,0,24};
        for(unsigned k=0;k<4;++k)r.factors[i][k]=((color>>shifts[k])&255)/255.0f;
    }
    const uint32_t *rgba=q->render(q->opaque,&r);
    if(!rgba || ((uintptr_t)rgba&3) || (uintptr_t)rgba > UINTPTR_MAX-BYTES ||
        overlaps((uintptr_t)rgba,BYTES,(uintptr_t)destination,BYTES) ||
        overlaps((uintptr_t)rgba,BYTES,(uintptr_t)r.texture0.blocks,r.texture0.bytes)) return 0;
    /* NV097_COLOR_MASK selects RGB only. Preserve guest A independently of
     * staging contents, including pixels outside the original rectangle. */
    for(unsigned i=0;i<BYTES;i+=4) memcpy(destination+i,(const uint8_t *)rgba+i,3);
    if (xv_mark_written) xv_mark_written(destination, BYTES);
    q->active=0; ++q->completed; return 1;
}
int h2_sprite_method(h2_sprite_draw *q,h2_command_state *s,h2_kelvin_clear *c,
                     uint8_t sub,uint16_t method,uint32_t value)
{
    if(!q||!s||!c||!q->contract||sub>=8||s->bound[sub]!=1)return 0;
    if(!q->active) {
        h2_sprite_request r;uint8_t *destination;
        if(method!=0x17FC||value!=8||q->completed==UINT64_MAX||
            !pipeline(q,s,c)||!resources(s,c,&r,&destination))return 0;
        memset(q->words,0,sizeof q->words);q->count=0;q->active=1;q->subchannel=sub;return 1;
    }
    if(sub!=q->subchannel)return 0;
    if(method==0x17FC)return value==0&&finish(q,s,c);
    if(method!=0x1818||q->count>=20)return 0;
    if(q->count%5<4) {
        float f=as_float(value);
        if(!(f>=-2048&&f<=2048))return 0;
    }
    q->words[q->count++]=value;return 1;
}
