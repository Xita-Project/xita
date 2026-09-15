/* Exercise the actual dispatch function with bounded consumer oracles. An
 * active consumer owns both success and rejection; rejection cannot fall
 * through to a different renderer or leave changed floating-point controls. */
#define H2_QUAD_RENDER 1
#define H2_SCREEN_RENDER 1
#define H2_BC1_RENDER 1
#define H2_COMPOSITION_RENDER 1
#define H2_THRESHOLD_RENDER 1
#define H2_BLUR_RENDER 1
#define H2_BLEND_RENDER 1
#include "host_channel_runtime.c"
#include <assert.h>
#include <stdio.h>
static unsigned calls[7], accepts, descriptor_reads;
static uint32_t fp;
uint32_t h2_platform_fpscr_read(void) { return fp; }
void h2_platform_fpscr_write(uint32_t value) { fp=value; }
void xv_logf(const char *format, ...) { (void)format; fp=0xBAD; }
#define CONSUMER(type, name, index) \
int name(type *q, h2_command_state *s, h2_kelvin_clear *c, uint8_t sub, uint16_t method, uint32_t value) \
{ \
    assert(s==&channel.commands&&c==&channel.clear&&sub==3); \
    ++calls[index];fp=0xBAD; \
    if(!(accepts&(1u<<index)))return 0; \
    if(method==0x17FC&&value==0){q->active=0;++q->completed;} \
    else if(method==0x17FC&&value==7)q->active=1; \
    return 1; \
}
CONSUMER(h2_quad_draw,h2_quad_method,0)
CONSUMER(h2_screen_draw,h2_screen_method,1)
CONSUMER(h2_bc1_draw,h2_bc1_method,2)
CONSUMER(h2_composition_draw,h2_composition_method,3)
CONSUMER(h2_threshold_draw,h2_threshold_method,4)
CONSUMER(h2_blur_draw,h2_blur_method,5)
CONSUMER(h2_blend_draw,h2_blend_method,6)
unsigned h2_composition_probe(const h2_composition_draw *q,const h2_command_state *s,const h2_kelvin_clear *c)
{ assert(q==&composition_quad&&s==&channel.commands&&c==&channel.clear);fp=0xBAD;return 3; }
static int read_descriptor(void *opaque,uint32_t address,uint32_t *value)
{
    assert(opaque==&descriptor_reads);assert(address>=0x10000&&address<0x10030&&!(address&3));
    ++descriptor_reads;fp=0xBAD;*value=address^0x13579BDF;
    return !(address&4); /* partial failures still preserve caller state */
}
static void init(void)
{
    memset(&movie_quad,0,sizeof movie_quad);memset(&screen_quad,0,sizeof screen_quad);
    memset(&bc1_quad,0,sizeof bc1_quad);memset(&composition_quad,0,sizeof composition_quad);
    memset(&threshold_quad,0,sizeof threshold_quad);
    memset(&blur_quad,0,sizeof blur_quad);
    memset(&blend_quad,0,sizeof blend_quad);
    memset(calls,0,sizeof calls);fp=0xFEDCBA98;
}
static int dispatch(uint16_t method,uint32_t value)
{
    int result=geometry_method(NULL,3,method,value,0x87654321);
    assert(fp==0xFEDCBA98);return result;
}
int main(void)
{
    for(unsigned owner=0;owner<7;++owner)for(unsigned success=0;success<2;++success){
        init();uint8_t *active[]={&movie_quad.active,&screen_quad.active,&bc1_quad.active,&composition_quad.active,&threshold_quad.active,&blur_quad.active,&blend_quad.active};
        *active[owner]=1;accepts=success?127:127^(1u<<owner);
        assert(dispatch(0x1518,0x12345678)==(int)success);
        for(unsigned i=0;i<7;++i)assert(calls[i]==(i==owner));
        assert(*active[owner]);memset(calls,0,sizeof calls);
        assert(dispatch(0x17FC,0)==(int)success);assert(*active[owner]==!success);
        for(unsigned i=0;i<7;++i)assert(calls[i]==(i==owner));
    }
    for(unsigned selected=0;selected<7;++selected){
        init();accepts=1u<<selected;assert(dispatch(0x17FC,7)==1);
        for(unsigned i=0;i<7;++i)assert(calls[i]==(i<=selected));
    }
    init();accepts=0;assert(dispatch(0x17FC,7)==0);
    for(unsigned i=0;i<7;++i)assert(calls[i]==1);
    init();accepts=0;descriptor_reads=0;
    channel.commands.dma[1]=0x10000;channel.clear.dma_color=0x10010;channel.clear.dma_zeta=0x10020;
    channel.clear.read_instance=read_descriptor;channel.clear.opaque=&descriptor_reads;
    h2_command_state before_commands=channel.commands;h2_kelvin_clear before_clear=channel.clear;
    assert(dispatch(0x17FC,7)==0&&descriptor_reads==12);
    assert(!memcmp(&channel.commands,&before_commands,sizeof before_commands));
    assert(!memcmp(&channel.clear,&before_clear,sizeof before_clear));
    init();accepts=127;assert(dispatch(0x1518,0)==-1);
    for(unsigned i=0;i<7;++i)assert(!calls[i]);
    puts("geometry dispatch: exclusive active ownership, strict rejection and FPSCR restoration passed");
}
