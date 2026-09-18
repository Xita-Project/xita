#include "composition_draw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PIXELS (640u * 480u)
#define BYTES (PIXELS * 4)
#define IMAGE_BYTES (320u * 240u * 4u)
#define IMAGE (2u * BYTES + 8192u)
static uint8_t ram[3 * BYTES + 4096], prior[sizeof ram];
static uint32_t pixels[PIXELS];
static h2_command_state s;
static h2_kelvin_clear c;
static h2_composition_draw q;
static h2_composition_contract reference;
static unsigned reads, maps, renders, attachments;
static int read_failure, map_failure, alias_map, overflow_map, render_failure, render_alias, attachment_failure, readonly;
static int read_word(void *opaque, uint32_t offset, uint32_t *word)
{
    assert(!opaque); ++reads;
    if (read_failure || offset < 0x13000 || offset >= 0x13010 || (offset & 3)) return 0;
    const uint32_t object[] = {readonly ? 0xB002u : 0xB03Du, sizeof ram - 1, 3, 3};
    *word = object[(offset - 0x13000) / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t address, uint32_t bytes)
{
    assert(!opaque); ++maps;
    if (address > sizeof ram || bytes > sizeof ram - address || map_failure == 1 ||
        (map_failure == 2 && address == c.color_offset) ||
        (map_failure == 3 && bytes == 64) ||
        (map_failure == 4 && address == IMAGE)) return NULL;
    if (overflow_map) return (void *)(UINTPTR_MAX - 8);
    /* Distinct physical ranges can still return overlapping host spans. */
    if (alias_map == 1) return ram + 4096;
    if (alias_map >= 2 && address == IMAGE) {
        const uint32_t targets[] = {4096, BYTES + 4096, 2 * BYTES + 4096};
        return ram + targets[alias_map - 2] + 4;
    }
    assert(bytes != 32); /* retained DOTPRODUCT unit1 must not be fetched */
    return ram + address;
}
static int attachment(void *opaque, uint32_t address, uint32_t bytes, uint32_t pitch, int depth, uint32_t format)
{
    assert(!depth);
    assert(!opaque && bytes == BYTES && pitch == 2560 && format == 0x128);
    assert(address == (depth ? c.zeta_offset : c.color_offset)); ++attachments;
    return !attachment_failure;
}
static const uint32_t *render(void *opaque, const h2_composition_request *r)
{
    assert(!opaque); ++renders;
    assert(r->texture0.pixels == ram + 4096 && r->texture0.bytes == BYTES);
    assert(r->texture2.blocks == ram + 2 * BYTES + 4096 && r->texture2.bytes == 64);
    assert(r->texture3.pixels == ram + IMAGE && r->texture3.bytes == IMAGE_BYTES);
    assert(!memcmp(r->texture3.pixels, prior + IMAGE, IMAGE_BYTES));
    assert(r->destination == ram + c.color_offset);
    assert(!memcmp(r->destination, prior + c.color_offset, BYTES));
    assert(!memcmp(r->vertices, reference.vertices, sizeof r->vertices));
    for (unsigned i = 0; i < 18; ++i) {
        assert(r->factors[i][0] == (i + 1) / 255.0f);
        assert(r->factors[i][1] == (i + 11) / 255.0f);
        assert(r->factors[i][2] == (i + 21) / 255.0f);
        assert(r->factors[i][3] == (i + 31) / 255.0f);
    }
    if (render_failure) return NULL;
    if (render_alias == 1) return (uint32_t *)(ram + c.color_offset);
    if (render_alias == 2) return (uint32_t *)(ram + 4096);
    if (render_alias == 3) return (uint32_t *)(ram + 2 * BYTES + 4096);
    if (render_alias == 4) return (uint32_t *)(UINTPTR_MAX - 8);
    if (render_alias == 6) return (uint32_t *)(ram + IMAGE);
    if (render_alias == 5) return (uint32_t *)((uint8_t *)pixels + 1);
    return pixels;
}
static void set(unsigned method, uint32_t value)
{
    s.setup[method / 4] = value;
    s.setup_valid[method / 128] |= 1u << ((method / 4) % 32);
}
static void init(void)
{
    memset(&s,0,sizeof s);memset(&c,0,sizeof c);memset(&q,0,sizeof q);memset(&reference,0,sizeof reference);
    for (unsigned i = 0; i < sizeof ram; ++i) ram[i] = (i * 71u) ^ (i >> 11);
    for (unsigned i = 0; i < PIXELS; ++i) pixels[i] = i * 0x5711u ^ 0xB0C08723u;
    s.bound[0] = 1; s.dma[1] = 0x13000; s.dma_valid = 2; s.execution_mode = 6; s.software_valid = 7;
    s.provoking_vertex = s.edge_flag = s.compress_depth = 1; s.shadow_slope = 0x7F800000;
    for (unsigned i = 0; i < 7; ++i) s.program[i][0] = 0x13579u + i; /* synthetic trusted reference */
    set(0x300,0);set(0x304,1);set(0x308,0);set(0x30C,0);set(0x314,0);set(0x32C,0);
    set(0x344,0x8001);set(0x348,0x303);set(0x34C,0xFFFFFF);set(0x350,0x8006);
    set(0x358,0x01010101);set(0x35C,0);set(0x398,0x4B7FFFFF);
    set(0x1B00,4096);set(0x1B04,0x11229);set(0x1B08,0x30303);set(0x1B0C,0x4003FFC0);
    set(0x1B10,0x0A000000);set(0x1B14,0x1012000);set(0x1B1C,0x28001E0);
    set(0x147C,0);
    set(0x1B40,0x3FFFFFF); /* intentionally unmapped, no fetch in DOTPRODUCT */
    set(0x1B44,0x3310C29);set(0x1B48,0x10101);set(0x1B4C,0x4003FFC0);set(0x1B54,0x2022000);
    set(0x1BC0,IMAGE);set(0x1BC4,0x11229);set(0x1BC8,0x30303);set(0x1BCC,0x4003FFC0);
    set(0x1BD0,0x5000000);set(0x1BD4,0x2022000);set(0x1BDC,0x14000F0);
    set(0x1B80,2*BYTES+4096);set(0x1B84,0x3310E29);set(0x1B88,0x30303);
    set(0x1B8C,0x4003FFC0);set(0x1B94,0x2022000);set(0x1E70,0xA621);set(0x1E74,0x44);
    for (unsigned i = 0; i < 18; ++i)
        set(i < 16 ? 0xA60 + i * 4 : 0x1E20 + (i-16)*4,
            ((i+1)<<16)|((i+11)<<8)|(i+21)|((i+31)<<24));
    memcpy(reference.setup,s.setup,sizeof reference.setup);memcpy(reference.setup_valid,s.setup_valid,sizeof reference.setup_valid);
    memcpy(reference.program,s.program,sizeof reference.program);
    const float xy[][2]={{0,0},{0,480},{640,480},{640,0}};
    for (unsigned i=0;i<4;++i) {
        for (unsigned a=0;a<=4;++a) {
            reference.vertices[i].attribute[a][0] = a==2?.5f:a==3?1.0f:xy[i][0];
            reference.vertices[i].attribute[a][1] = a==2||a==3?0:xy[i][1];
            reference.vertices[i].attribute[a][2] = a==0?16777215.0f:0;
            reference.vertices[i].attribute[a][3] = a==0?16777215.0f:a==2||a==3?0:1;
        }
    }
    c.read_instance=read_word;c.map_physical=map_ram;c.check_attachment=attachment;c.physical_bytes=sizeof ram;
    c.has_color_dma=1;c.has_zeta_dma=0;c.dma_color=0x13000;c.dma_zeta=0xFFFFFFF0;
    c.color_offset=BYTES+4096;c.zeta_offset=0xFFFFFFFF; /* unused and intentionally invalid */c.format=0x128;c.pitch=0x0A000A00;
    c.clip_horizontal=640u<<16;c.clip_vertical=480u<<16;
    q.contract=&reference;q.render=render;
    reads=maps=renders=attachments=0;
    read_failure=map_failure=alias_map=overflow_map=render_failure=render_alias=attachment_failure=readonly=0;
    memcpy(prior,ram,sizeof ram);
}
static void reject(unsigned sub, unsigned method, uint32_t value)
{
    h2_composition_draw beforeq=q;h2_command_state befores=s;h2_kelvin_clear beforec=c;
    assert(!h2_composition_method(&q,&s,&c,sub,method,value));
    assert(!memcmp(&q,&beforeq,sizeof q)&&!memcmp(&s,&befores,sizeof s)&&!memcmp(&c,&beforec,sizeof c));
    assert(!memcmp(prior,ram,sizeof ram));
}
static void begin(void){assert(h2_composition_method(&q,&s,&c,0,0x17FC,7));}
static void vertices(int check_rejections)
{
    static const unsigned methods[]={0x1A40,0x1A30,0x1A20,0x1A10,0x1518};
    for(unsigned v=0;v<4;++v)for(unsigned field=0;field<5;++field)for(unsigned component=0;component<4;++component){
        unsigned method=methods[field]+component*4;uint32_t value;
        memcpy(&value,&reference.vertices[v].attribute[4-field][component],4);
        if(check_rejections){reject(0,method,value^1);reject(0,method+1,value);reject(1,method,value);reject(0,0x17FC,0);}
        assert(h2_composition_method(&q,&s,&c,0,method,value));
    }
}
int main(void)
{
    init();assert(h2_composition_probe(&q,&s,&c)==0);assert(!memcmp(ram,prior,sizeof ram));
    s.setup[0x304/4]=0;assert(h2_composition_probe(&q,&s,&c)==2);s.setup[0x304/4]=1;
    map_failure=1;assert(h2_composition_probe(&q,&s,&c)==3);map_failure=0;
    q.contract=NULL;assert(h2_composition_probe(&q,&s,&c)==1);
    init();
    for(unsigned i=0;i<2048;++i)if(i!=0x1B00/4&&i!=0x1B80/4&&i!=0x1BC0/4&&
        !(i*4>=0x1B20&&i*4<=0x1BE0&&((i*4)&63)==32)){
        s.setup[i]^=1;reject(0,0x17FC,7);s.setup[i]^=1;
    }
    for(unsigned i=0;i<64;++i){s.setup_valid[i]^=~0u;reject(0,0x17FC,7);s.setup_valid[i]^=~0u;}
    for(unsigned i=0;i<7;++i)for(unsigned k=0;k<4;++k){s.program[i][k]^=1;reject(0,0x17FC,7);s.program[i][k]^=1;}
    assert(!reads&&!maps&&!renders);
    init();begin();vertices(1);assert(!renders&&!memcmp(ram,prior,sizeof ram));
    assert(h2_composition_method(&q,&s,&c,0,0x17FC,0));assert(!q.active&&q.completed==1&&renders==1);
    memcpy(prior+c.color_offset,pixels,BYTES);assert(!memcmp(ram,prior,sizeof ram));
    init();begin();
    for(unsigned method=0;method<0x2000;method+=4)if(method!=0x1A40)reject(0,method,0x5A5A5A5A);
    /* END reruns state/resource validation and never commits failed staging. */
    for(int failure=0;failure<16;++failure){
        init();begin();vertices(0);
        switch(failure){case 0:read_failure=1;break;case 1:map_failure=1;break;case 2:map_failure=2;break;
        case 3:map_failure=3;break;case 4:alias_map=1;break;case 5:overflow_map=1;break;
        case 6:render_failure=1;break;case 7:readonly=1;break;case 8:attachment_failure=1;break;
        case 9:c.has_color_dma=0;break;case 10:s.setup[0x1B80/4]=4096;break;case 11:s.setup[0x304/4]=0;break;
        case 12:map_failure=4;break;case 13:s.setup[0x1BC0/4]=4096;break;
        case 14:s.setup[0x1BC0/4]=2*BYTES+4096;break;case 15:s.setup[0x1BC0/4]=c.color_offset;break;}
        reject(0,0x17FC,0);
        if(failure!=6)assert(!renders);
    }
    for(int alias=1;alias<=6;++alias){init();begin();vertices(0);render_alias=alias;reject(0,0x17FC,0);assert(renders==1);}
    for(int alias=2;alias<=4;++alias){init();alias_map=alias;reject(0,0x17FC,7);assert(!renders);}
    init();s.setup[0x1BC0/4]=sizeof ram-IMAGE_BYTES+4;reject(0,0x17FC,7);
    init();s.setup[0x1BC0/4]+=1;reject(0,0x17FC,7);
    init();reference.setup[0x1BD4/4]=s.setup[0x1BD4/4]=0x1012000;reject(0,0x17FC,7);assert(!maps&&!reads);
    init();reference.setup[0x147C/4]=s.setup[0x147C/4]=1;reject(0,0x17FC,7);assert(!maps&&!reads);
    init();c.color_offset=4096;reject(0,0x17FC,7);assert(!renders);
    init();s.setup[0x1B80/4]+=1;reject(0,0x17FC,7);
    init();s.setup[0x1B80/4]=sizeof ram-63;reject(0,0x17FC,7);
    init();s.setup[0x1B00/4]=sizeof ram-BYTES+1;reject(0,0x17FC,7);
    init();reference.setup[0x35C/4]=s.setup[0x35C/4]=1;reject(0,0x17FC,7);assert(!maps&&!reads);
    init();reference.setup[0x1B04/4]=s.setup[0x1B04/4]=0x11E29;reject(0,0x17FC,7);assert(!maps&&!reads);
    init();q.contract=NULL;reject(0,0x17FC,7);
    init();q.render=NULL;reject(0,0x17FC,7);
    puts("composition draw: exact original ordering, RGBA commit, three sampled inputs, unused DOTPRODUCT resource, alias and failed-render isolation passed");
}
