/* Pure texture conversion. A range owns source texels (swizzled formats),
 * output rows (linear formats), or block rows (DXT); ranges never overlap. */
#ifndef XV_TEXTURE_DECODE_H
#define XV_TEXTURE_DECODE_H
#include <stdint.h>
#include "xv_bc_layout.h"
typedef struct {
    const uint8_t *src;
    uint32_t *dst;
    const uint32_t *palette;
    unsigned fmt, w, h, pitch;
    int linear, transpose, flat_palette;
    int bc_reorder;
} xv_texture_job;
static inline unsigned xv_tex_units(const xv_texture_job *j)
{
    if (j->bc_reorder) return ((j->w+3)/4)*((j->h+3)/4);
    switch (j->fmt) {
    case 0x0C: case 0x0E: case 0x0F: return (j->h + 3) / 4;
    case 0x12: case 0x1E: case 0x3F: case 0x40: case 0x41:
    case 0x10: case 0x11: case 0x1C: case 0x1D:
    case 0x13: case 0x1F: case 0x1B: case 0x20: return j->h;
    default: return j->w * j->h;
    }
}
static uint32_t ui_morton1(uint32_t x){ x&=0x55555555; x=(x|(x>>1))&0x33333333; x=(x|(x>>2))&0x0F0F0F0F; x=(x|(x>>4))&0x00FF00FF; x=(x|(x>>8))&0x0000FFFF; return x; }
static void ui_unswz(unsigned w, unsigned h, unsigned idx, unsigned *ox, unsigned *oy, int transpose){
    unsigned bx=ui_morton1(idx), by=ui_morton1(idx>>1), lw=0, lh=0, t;
    for(t=w;t>1;t>>=1)lw++;
    for(t=h;t>1;t>>=1)lh++;
    unsigned c=lw<lh?lw:lh, m=(1u<<c)-1;
    if(lw>=lh){*ox=(bx&m)|((idx>>(2*c))<<c);*oy=by&m;} else {*oy=(by&m)|((idx>>(2*c))<<c);*ox=bx&m;}
    { if (transpose && w == h) { unsigned t2 = *ox; *ox = *oy; *oy = t2; } }   /* diagnostic: transposed Morton */
}
static uint32_t ui_c565(uint16_t v){ unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31; return 0xFF000000u|((b*255/31)<<16)|((g*255/63)<<8)|(r*255/31); }
static uint32_t ui_c4444(uint16_t v){ unsigned a=(v>>12)&15,r=(v>>8)&15,g=(v>>4)&15,b=v&15; return ((a*17u)<<24)|((b*17u)<<16)|((g*17u)<<8)|(r*17u); }
static uint32_t ui_c1555(uint16_t v){ unsigned a=v>>15?255:0,r=(v>>10)&31,g=(v>>5)&31,b=v&31; return (a<<24)|((b*255/31)<<16)|((g*255/31)<<8)|(r*255/31); }
static void ui_dxt(const uint8_t *s, int fmt, uint32_t out[16]){
    const uint8_t *cb = fmt==0x0C ? s : s+8;
    uint16_t c0=cb[0]|(cb[1]<<8), c1=cb[2]|(cb[3]<<8);
    uint32_t p[4]={ui_c565(c0),ui_c565(c1),0,0};
    if(c0>c1||fmt!=0x0C){ for(int i=0;i<3;i++){unsigned a=(p[0]>>(i*8))&0xFF,b=(p[1]>>(i*8))&0xFF; p[2]|=(((2*a+b)/3)<<(i*8)); p[3]|=(((a+2*b)/3)<<(i*8));} p[2]|=0xFF000000u; p[3]|=0xFF000000u; }
    else { for(int i=0;i<3;i++){unsigned a=(p[0]>>(i*8))&0xFF,b=(p[1]>>(i*8))&0xFF; p[2]|=(((a+b)/2)<<(i*8));} p[2]|=0xFF000000u; }
    uint32_t bits=cb[4]|(cb[5]<<8)|(cb[6]<<16)|((uint32_t)cb[7]<<24);
    for(int i=0;i<16;i++) out[i]=p[(bits>>(i*2))&3];
    if(fmt==0x0E) for(int i=0;i<16;i++){ unsigned a4=(s[i/2]>>((i&1)*4))&0xF; out[i]=(out[i]&0x00FFFFFFu)|((a4*17u)<<24); }
    else if(fmt==0x0F){ unsigned a0=s[0],a1=s[1]; uint64_t ab=0; for(int i=0;i<6;i++) ab|=(uint64_t)s[2+i]<<(i*8);
        for(int i=0;i<16;i++){ unsigned code=(ab>>(i*3))&7,a; if(code==0)a=a0; else if(code==1)a=a1; else if(a0>a1)a=((8-code)*a0+(code-1)*a1)/7; else if(code==6)a=0; else if(code==7)a=255; else a=((6-code)*a0+(code-1)*a1)/5; out[i]=(out[i]&0x00FFFFFFu)|(a<<24);} }
}
/* decode guest texture -> dst (w*h RGBA8, bytes R,G,B,A). Returns 0 on success. */
static inline int xv_tex_decode_range(const xv_texture_job *job, unsigned first, unsigned last)
{
    if (job->bc_reorder) {
        if (job->fmt!=0x0C && job->fmt!=0x0E && job->fmt!=0x0F) return -1;
        return xv_bc_reorder_range((uint8_t *)job->dst,job->src,job->w,job->h,
            job->fmt==0x0C ? 8 : 16,first,last);
    }
    const uint8_t *src = job->src;
    uint32_t *dst = job->dst;
    const uint32_t *palette = job->palette;
    unsigned fmt = job->fmt, w = job->w, h = job->h, pitch = job->pitch;
    int linear = job->linear, transpose = job->transpose, flat = job->flat_palette;
    if (fmt==0x0C || fmt==0x0E || fmt==0x0F) {                         /* DXT: linear 4x4 block order */
        unsigned bw=(w+3)/4, bs=(fmt==0x0C?8:16);
        for (unsigned by=first; by<last; ++by) for (unsigned bx=0; bx<bw; ++bx) {
            uint32_t blk[16]; ui_dxt(src + (by*bw+bx)*bs, fmt, blk);
            for (unsigned i=0;i<16;i++){ unsigned x=bx*4+(i&3), y=by*4+(i>>2); if(x<w&&y<h) dst[y*w+x]=blk[i]; }
        }
    } else if (fmt==0x12 || fmt==0x1E || fmt==0x3F || fmt==0x40 || fmt==0x41) {   /* linear 32-bit */
        for (unsigned y=first;y<last;y++) for (unsigned x=0;x<w;x++){ const uint8_t *p=src+y*pitch+x*4; uint32_t r,g,b,a;
            if(fmt==0x3F){a=p[3];b=p[2];g=p[1];r=p[0];} else if(fmt==0x41){r=p[3];g=p[2];b=p[1];a=p[0];}
            else if(fmt==0x40){b=p[3];g=p[2];r=p[1];a=p[0];} else {b=p[0];g=p[1];r=p[2];a=fmt==0x1E?255:p[3];}
            dst[y*w+x]=(a<<24)|(b<<16)|(g<<8)|r; }
    } else if (fmt==0x06 || fmt==0x07) {                              /* swizzled A8R8G8B8 */
        for (unsigned i=first;i<last;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y,transpose); const uint8_t *p=src+i*4;
            if(x<w&&y<h) dst[y*w+x]=((fmt==0x07?255u:p[3])<<24)|(p[0]<<16)|(p[1]<<8)|p[2]; }
    } else if (fmt==0x02 || fmt==0x03 || fmt==0x04 || fmt==0x05) {    /* swizzled 16-bit */
        for (unsigned i=first;i<last;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y,transpose); uint16_t v=src[i*2]|(src[i*2+1]<<8);
            if(x<w&&y<h) dst[y*w+x]= fmt==0x05?ui_c565(v): fmt==0x04?ui_c4444(v): ui_c1555(v) | (fmt==0x03 ? 0xFF000000u : 0); }
    } else if (fmt==0x10 || fmt==0x11 || fmt==0x1C || fmt==0x1D) {    /* linear 16-bit */
        for (unsigned y=first;y<last;y++) for (unsigned x=0;x<w;x++){ uint16_t v=src[y*pitch+x*2]|(src[y*pitch+x*2+1]<<8);
            dst[y*w+x]= fmt==0x11?ui_c565(v): fmt==0x1D?ui_c4444(v): ui_c1555(v) | (fmt==0x1C ? 0xFF000000u : 0); }
    } else if (fmt==0x0B && palette) {                              /* P8 swizzled through the bound palette (D3DCOLOR ARGB) */
        for (unsigned i=first;i<last;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y,transpose); uint32_t p=palette[src[i]];
            if(x<w&&y<h) dst[y*w+x]=flat ? 0xFFFF8080u : (p&0xFF000000u)|((p&0xFF)<<16)|(p&0xFF00)|((p>>16)&0xFF); }
    } else if (fmt==0x0B || fmt==0x00 || fmt==0x19 || fmt==0x01) {   /* 8-bit swizzled: L8/P8, A8, AL8 */
        for (unsigned i=first;i<last;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y,transpose); uint8_t l=src[i];
            if(x<w&&y<h) dst[y*w+x]= fmt==0x19 ? ((uint32_t)l<<24)|0x00FFFFFF : fmt==0x01 ? ((uint32_t)l<<24)|(l<<16)|(l<<8)|l : 0xFF000000u|(l<<16)|(l<<8)|l; }
    } else if (fmt==0x13 || fmt==0x1F || fmt==0x1B) {                  /* 8-bit linear: L8, A8, AL8 */
        for (unsigned y=first;y<last;y++) for (unsigned x=0;x<w;x++){ uint8_t l=src[y*pitch+x];
            dst[y*w+x]= fmt==0x1F ? ((uint32_t)l<<24)|0x00FFFFFF : fmt==0x1B ? ((uint32_t)l<<24)|(l<<16)|(l<<8)|l : 0xFF000000u|(l<<16)|(l<<8)|l; }
    } else if (fmt==0x1A) {                                             /* A8L8 swizzled */
        for (unsigned i=first;i<last;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y,transpose); uint8_t l=src[i*2], a=src[i*2+1];
            if(x<w&&y<h) dst[y*w+x]=((uint32_t)a<<24)|(l<<16)|(l<<8)|l; }
    } else if (fmt==0x20) {                                             /* LIN_A8L8 */
        for (unsigned y=first;y<last;y++) for (unsigned x=0;x<w;x++){ uint8_t l=src[y*pitch+x*2], a=src[y*pitch+x*2+1]; dst[y*w+x]=((uint32_t)a<<24)|(l<<16)|(l<<8)|l; }
    } else { (void)linear; return -1; }
    return 0;
}


#endif
