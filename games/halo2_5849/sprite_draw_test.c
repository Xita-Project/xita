#include "sprite_draw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PIXELS (640u * 480u)
#define BYTES (PIXELS * 4)
#define IMAGE_BYTES 8192u
#define IMAGE (BYTES + 8192u)
static uint8_t ram[3 * BYTES + 4096], prior[sizeof ram];
static uint32_t pixels[PIXELS];
static h2_command_state s;
static h2_kelvin_clear c;
static h2_sprite_draw q;
static h2_sprite_contract reference;
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
        (map_failure == 3 && address == 4096)) return NULL;
    if (overflow_map) return (void *)(UINTPTR_MAX - 8);
    /* Distinct physical ranges can still return overlapping host spans. */
    if (alias_map == 2) return ram + 4096;
    if (alias_map == 1 && maps%2 == 0) return ram + 4096 + 4;
    return ram + address;
}
static int attachment(void *opaque, uint32_t address, uint32_t bytes, uint32_t pitch, int depth, uint32_t format)
{
    assert(!depth);
    assert(!opaque && bytes == BYTES && pitch == 2560 && format == 0x128);
    assert(address == (depth ? c.zeta_offset : c.color_offset)); ++attachments;
    return !attachment_failure;
}
static const uint32_t *render(void *opaque, const h2_sprite_request *r)
{
    assert(!opaque); ++renders;
    assert(r->texture0.blocks == ram + 4096 && r->texture0.bytes == IMAGE_BYTES);
    assert(!memcmp(r->texture0.blocks, prior + 4096, IMAGE_BYTES));
    assert(r->destination == ram + c.color_offset);
    assert(!memcmp(r->destination, prior + c.color_offset, BYTES));
    for(unsigned v=0;v<4;++v) {
        const uint32_t *w=q.words+v*5;
        assert(!memcmp(r->vertices[v].attribute[0],w,8));
        assert(!memcmp(r->vertices[v].attribute[1],w+2,8));
        for(unsigned a=0;a<2;++a)assert(r->vertices[v].attribute[a][2]==0&&r->vertices[v].attribute[a][3]==1);
        const unsigned shifts[]={16,8,0,24};
        for(unsigned k=0;k<4;++k)assert(r->vertices[v].attribute[2][k]==((w[4]>>shifts[k])&255)/255.0f);
    }
    assert(!memcmp(r->constants, reference.constants, sizeof r->constants));
    for (unsigned i = 0; i < 18; ++i) {
        assert(r->factors[i][0] == (i + 1) / 255.0f);
        assert(r->factors[i][1] == (i + 11) / 255.0f);
        assert(r->factors[i][2] == (i + 21) / 255.0f);
        assert(r->factors[i][3] == (i + 31) / 255.0f);
    }
    if (render_failure) return NULL;
    if (render_alias == 1) return (uint32_t *)(ram + c.color_offset);
    if (render_alias == 2) return (uint32_t *)(ram + 4096);
    if (render_alias == 4) return (uint32_t *)(UINTPTR_MAX - 8);
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
    for (unsigned i = 0; i < 21; ++i) s.program[i][0] = 0x13579u + i; /* synthetic trusted reference */
    set(0x300,0);set(0x304,1);set(0x308,1);set(0x30C,0);set(0x314,0);set(0x32C,0);
    set(0x344,0x302);set(0x348,0x303);set(0x34C,0xFFFFFF);set(0x350,0x8006);
    set(0x358,0x00010101);set(0x35C,0);set(0x398,0x4B7FFFFF);
    set(0x1B00,4096);set(0x1B04,0x3A10E29);set(0x1B08,0x30303);set(0x1B0C,0x4003FFC0);
    set(0x1B10,0x0A000000);set(0x1B14,0x2062000);set(0x1B1C,0x28001E0);
    set(0x147C,0);set(0x39C,0x405);set(0x3A0,0x900);
    for(unsigned i=0;i<16;++i)set(0x1760+i*4,i<2?0x22:i==2?0x40:2);
    for(unsigned unit=1;unit<4;++unit)set(0x1B00+unit*64,0x3FFFFFF); /* inactive and unmapped */
    set(0x1E70,1);set(0x1E74,0);
    for (unsigned i = 0; i < 18; ++i)
        set(i < 16 ? 0xA60 + i * 4 : 0x1E20 + (i-16)*4,
            ((i+1)<<16)|((i+11)<<8)|(i+21)|((i+31)<<24));
    memcpy(reference.setup,s.setup,sizeof reference.setup);memcpy(reference.setup_valid,s.setup_valid,sizeof reference.setup_valid);
    memcpy(reference.program,s.program,sizeof reference.program);
    for(unsigned i=0;i<178;++i)for(unsigned k=0;k<4;++k)reference.constants[i][k]=s.constants[10+i][k]=0x3F000000+i*4+k;
    const float xy[][2]={{12,23},{1012,23},{1012,87},{12,87}};
    for(unsigned i=0;i<4;++i) {
        memcpy(reference.captured_inline+i*5,xy[i],8);
        reference.captured_inline[i*5+2]=i==1||i==2?0x3F800000:0;
        reference.captured_inline[i*5+3]=i>=2?0x3F800000:0;
        reference.captured_inline[i*5+4]=0x01EEDDCC;
    }
    c.read_instance=read_word;c.map_physical=map_ram;c.check_attachment=attachment;c.physical_bytes=sizeof ram;
    c.has_color_dma=1;c.has_zeta_dma=0;c.dma_color=0x13000;c.dma_zeta=0xFFFFFFF0;
    c.color_offset=IMAGE;c.zeta_offset=0xFFFFFFFF; /* unused and intentionally invalid */c.format=0x128;c.pitch=0x0A000A00;
    c.clip_horizontal=640u<<16;c.clip_vertical=480u<<16;
    q.contract=&reference;q.render=render;
    reads=maps=renders=attachments=0;
    read_failure=map_failure=alias_map=overflow_map=render_failure=render_alias=attachment_failure=readonly=0;
    memcpy(prior,ram,sizeof ram);
}
static void reject(unsigned sub, unsigned method, uint32_t value)
{
    h2_sprite_draw beforeq=q;h2_command_state befores=s;h2_kelvin_clear beforec=c;
    assert(!h2_sprite_method(&q,&s,&c,sub,method,value));
    assert(!memcmp(&q,&beforeq,sizeof q)&&!memcmp(&s,&befores,sizeof s)&&!memcmp(&c,&beforec,sizeof c));
    assert(!memcmp(prior,ram,sizeof ram));
}
static void begin(void){assert(h2_sprite_method(&q,&s,&c,0,0x17FC,8));}
static void vertices(int check_rejections)
{
    for(unsigned i=0;i<20;++i){
        uint32_t value=reference.captured_inline[i];
        if(check_rejections){reject(0,0x181C,value);reject(1,0x1818,value);reject(0,0x17FC,0);}
        assert(h2_sprite_method(&q,&s,&c,0,0x1818,value));
    }
}
int main(void)
{
    init();
    for(unsigned i=0;i<2048;++i)if(i!=0x1B00/4&&i!=0x1B04/4&&
        !(i*4>=0x1B20&&i*4<=0x1BE0&&((i*4)&63)==32)){
        s.setup[i]^=1;reject(0,0x17FC,8);s.setup[i]^=1;
    }
    for(unsigned i=0;i<64;++i){s.setup_valid[i]^=~0u;reject(0,0x17FC,8);s.setup_valid[i]^=~0u;}
    for(unsigned i=0;i<21;++i)for(unsigned k=0;k<4;++k){s.program[i][k]^=1;reject(0,0x17FC,8);s.program[i][k]^=1;}
    for(unsigned i=0;i<178;++i)for(unsigned k=0;k<4;++k){s.constants[10+i][k]^=1;reject(0,0x17FC,8);s.constants[10+i][k]^=1;}
    assert(!reads&&!maps&&!renders);
    init();begin();vertices(1);assert(!renders&&!memcmp(ram,prior,sizeof ram));
    assert(h2_sprite_method(&q,&s,&c,0,0x17FC,0));assert(!q.active&&q.completed==1&&renders==1);
    for(unsigned i=0;i<BYTES;i+=4)memcpy(prior+c.color_offset+i,(uint8_t *)pixels+i,3);
    assert(!memcmp(ram,prior,sizeof ram));
    init();begin();
    for(unsigned method=0;method<0x2000;method+=4)if(method!=0x1818)reject(0,method,0x5A5A5A5A);
    /* END reruns state/resource validation and never commits failed staging. */
    for(int failure=0;failure<13;++failure){
        init();begin();vertices(0);
        switch(failure){case 0:read_failure=1;break;case 1:map_failure=1;break;case 2:map_failure=2;break;
        case 3:map_failure=3;break;case 4:alias_map=1;break;case 5:overflow_map=1;break;
        case 6:render_failure=1;break;case 7:readonly=1;break;case 8:attachment_failure=1;break;
        case 9:c.has_color_dma=0;break;case 10:s.setup[0x1B00/4]=c.color_offset;break;
        case 11:s.setup[0x304/4]=0;break;case 12:q.words[0]=0x7FC00000;break;}
        reject(0,0x17FC,0);if(failure!=6)assert(!renders);
    }
    for(int alias=1;alias<=5;++alias){if(alias==3)continue;init();begin();vertices(0);render_alias=alias;reject(0,0x17FC,0);assert(renders==1);}
    init();s.setup[0x1B00/4]=sizeof ram-IMAGE_BYTES+4;reject(0,0x17FC,8);
    init();s.setup[0x1B00/4]+=1;reject(0,0x17FC,8);
    /* A modified private reference cannot authorize unsupported GPU state. */
    const unsigned methods[]={0x1B14,0x147C,0x35C,0x1768,0x304,0x1E70};
    for(unsigned i=0;i<sizeof methods/sizeof *methods;++i){init();reference.setup[methods[i]/4]=s.setup[methods[i]/4]^=1;reject(0,0x17FC,8);assert(!maps&&!reads);}
    init();alias_map=2;s.setup[0x1B00/4]+=BYTES;reject(0,0x17FC,8);assert(!renders);
    init();c.color_offset=4096+4;reject(0,0x17FC,8);assert(!renders);
    init();q.contract=NULL;reject(0,0x17FC,8);
    init();q.render=NULL;reject(0,0x17FC,8);
    /* All logical BC2 sizes permitted by the 8KiB resource budget; formats
     * are independent of the captured address and do not map disabled stages. */
    for(unsigned x=0;x<=10;++x)for(unsigned y=0;y<=10;++y){
        init();s.setup[0x1B04/4]=0x10E29|(x<<20)|(y<<24);
        unsigned bytes=(((1u<<x)+3)/4)*(((1u<<y)+3)/4)*16;
        if(bytes<=8192){begin();assert(maps==2&&attachments==1);}
        else reject(0,0x17FC,8);
    }
    /* Invalid finite geometry and nonfinite words must never reach the sink. */
    for(unsigned i=0;i<4;++i){init();begin();vertices(0);q.words[i*5+4]^=1;reject(0,0x17FC,0);assert(!renders);}
    const uint32_t bad[]={0x7F800000,0xFF800000,0x7FC00000,0x45000001,0xC5000001};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;++i){init();begin();reject(0,0x1818,bad[i]);}
    init();begin();vertices(0);q.words[0]=q.words[5];reject(0,0x17FC,0);assert(!renders);
    init();begin();vertices(0);q.words[2]=0x3F000000;reject(0,0x17FC,0);assert(!renders);
    init();q.completed=UINT64_MAX;reject(0,0x17FC,8);assert(!maps);
    init();begin();vertices(0);q.completed=UINT64_MAX;reject(0,0x17FC,0);assert(!renders);
    init();begin();vertices(0);reject(0,0x1818,0);
    puts("sprite: bounded inline rectangles, normalized color lanes, exact state, disjoint DMA/host spans, failure rollback and RGB-only completed commit passed");
}
