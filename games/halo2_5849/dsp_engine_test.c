/* Synthetic DSP transfers, fault boundaries and ISA arithmetic; no game bytes. */
#include "dsp_engine.c"
#include "dsp/interp/dsp_cpu.c"
#include <assert.h>

static h2_dsp_engine *fixture(void)
{
    h2_dsp_engine*s=calloc(1,sizeof(*s));assert(s);
    s->scratch_size=0x10000;s->scratch=calloc(1,s->scratch_size);assert(s->scratch);
    s->core.is_gp=1;s->core.opaque=s;s->core.read_peripheral=read_peripheral;s->core.write_peripheral=write_peripheral;
    dsp56k_reset_cpu(&s->core);s->dma_control=RUNNING;s->dma_next=16;
    return s;
}
static void descriptor(h2_dsp_engine*s,uint32_t control,uint32_t count,uint32_t addr,uint32_t offset,uint32_t base,uint32_t size)
{
    uint32_t d[7]={0x4000,control,count,addr,offset,base,size-1};
    memcpy(s->core.xram+16,d,sizeof(d));
}
static int transfer(h2_dsp_engine*s)
{
    active=s;if(setjmp(s->escape)){active=NULL;return 0;}dma_run(s);active=NULL;return 1;
}
static void dma_tests(void)
{
    for(unsigned space=0;space<3;space++){
        h2_dsp_engine*s=fixture();uint32_t addr=space==0?0x90:space==1?0x1890:0x2890;
        for(unsigned i=0;i<32;i++)put32(s->scratch+0x400+i*4,0xff000000u|i*0x070103u);
        descriptor(s,0x59e0,32,addr,0x400,0,4);assert(transfer(s));
        for(unsigned i=0;i<32;i++)assert(memread(s,space,0x90+i)==i*0x070103u);
        assert(s->eol&&s->status.transfers==1);
        s->dma_next=16;s->eol=0;descriptor(s,0x59e2,32,addr,0x500,0,4);assert(transfer(s));
        for(unsigned i=0;i<32;i++)assert(le32(s->scratch+0x500+i*4)==i*0x070103u);
        h2_dsp_destroy(s);
    }
    h2_dsp_engine*s=fixture();for(unsigned i=0;i<8;i++)s->core.xram[0x100+i]=i+1;
    descriptor(s,0x59d2,8,0x100,24,0x800,32);assert(transfer(s));
    for(unsigned i=0;i<8;i++)assert(le32(s->scratch+0x800+((24+i*4)%32))==i+1);
    assert(s->core.xram[20]==24); /* one complete wrap updates original offset */
    s->dma_next=16;descriptor(s,0x4bd0,8,0x120,24,0x800,32);assert(transfer(s));
    for(unsigned i=0;i<8;i++)assert(s->core.xram[0x120+i]==i+1);
    h2_dsp_destroy(s);
    const uint32_t bad[][7]={
        {0x4000,0x59e1,4,0x100,0x400,0,3}, /* interleaving */
        {0x4000,0x58e2,4,0x100,0x400,0,3}, /* FIFO */
        {0x4000,0x59e0,4,0x1000,0x400,0,3}, /* X hole */
        {0x4000,0x59e0,2,0x1fff,0x400,0,3}, /* Y end */
        {0x4000,0x59e0,2,0x37ff,0x400,0,3}, /* P end */
        {0x4000,0x59e0,0xffffffff,0x100,0x400,0,3},
        {0x4000,0x59e0,4,0x100,0xffff,0,3},
        {0x4000,0x59e0,4,0x100,4,0xfffffffc,3},
        {0x4000,0x59d2,4,0x100,32,0x800,31}, /* invalid circular offset */
        {0x4000,0x59d2,4,0x100,0,0x800,0xffffffff},
        {0x8000,0x59e0,4,0x100,0x400,0,3}, /* unknown next flags */
    };
    for(unsigned n=0;n<sizeof(bad)/sizeof(*bad);n++){
        s=fixture();memcpy(s->core.xram+16,bad[n],sizeof(bad[n]));
        memset(s->scratch,0xa5,s->scratch_size);uint32_t before[4096];memcpy(before,s->core.xram,sizeof(before));
        assert(!transfer(s));assert(s->status.fault&&!s->eol&&!s->status.transfers);
        assert(!memcmp(before,s->core.xram,sizeof(before)));
        for(unsigned i=0;i<s->scratch_size;i++)assert(s->scratch[i]==0xa5);
        assert(!h2_dsp_zero_frame(s));h2_dsp_destroy(s);
    }
    s=fixture();descriptor(s,0x59e0,1,0x100,0,0,4);s->core.xram[16]=16;
    assert(!transfer(s));assert(!strcmp(s->status.fault,"DMA chain budget"));assert(s->status.transfers==DMA_NODE_LIMIT);h2_dsp_destroy(s);
}
static int peripheral(h2_dsp_engine*s,uint32_t addr,uint32_t value,int write)
{
    active=s;if(setjmp(s->escape)){active=NULL;return 0;}
    if(write)write_peripheral(&s->core,addr,value);else(void)read_peripheral(&s->core,addr);
    active=NULL;return 1;
}
static void fault_tests(void)
{
    h2_dsp_engine*s=fixture();s->core.pc=0x2d;s->status.cycles=0x100000a;
    assert(read_peripheral(&s->core,0xffffb3)==10);
    s->core.pc=0x400;assert(!peripheral(s,0xffffb3,0,0));h2_dsp_destroy(s);
    s=fixture();assert(!peripheral(s,0xffffa0,0,0));h2_dsp_destroy(s);
    s=fixture();assert(!peripheral(s,0xffffa0,0,1));h2_dsp_destroy(s);
    s=fixture();assert(!peripheral(s,0xffffb2,1,1));h2_dsp_destroy(s);
    s=fixture();assert(!peripheral(s,0xffffd7,1,1));h2_dsp_destroy(s);
    s=fixture();s->core.pc=0x1000;assert(!frame(s));assert(s->status.fault);h2_dsp_destroy(s);
    s=fixture();s->core.pram[0]=0; /* NOPs eventually reach checked P limit */
    assert(!frame(s));assert(s->status.instructions==4096);h2_dsp_destroy(s);
}
static void image_tests(void)
{
    uint8_t image[0x850]={0},monitor[0x5cc]={0};
    put32(image+0x804,1);put32(image+0x808,0x81c);put32(image+0x80c,1);put32(image+0x810,3);
    put32(image+0x820,1);put32(image+0x828,0x818);put32(image+0x82c,4);
    put32(image+0x830,0x81c);put32(image+0x834,4);put32(image+0x840,0xc000);
    h2_dsp_engine*s=calloc(1,sizeof(*s));assert(s);assert(validate(s,image,sizeof(image)));free(s);
    h2_dsp_status st;assert(!h2_dsp_create(monitor,sizeof(monitor),image,sizeof(image),&st));assert(st.fault); /* no fabricated acknowledge */
    for(unsigned size=0;size<sizeof(image);size++){
        s=calloc(1,sizeof(*s));assert(s);assert(!validate(s,image,size));free(s);
    }
    const unsigned offsets[]={0x800,0x804,0x808,0x80c,0x810,0x814,0x820,0x824,0x828,0x82c,0x830,0x834,0x838,0x83c,0x840,0x844};
    for(unsigned n=0;n<sizeof(offsets)/sizeof(*offsets);n++){
        uint32_t old=le32(image+offsets[n]);put32(image+offsets[n],0xffffffff);s=calloc(1,sizeof(*s));assert(s);assert(!validate(s,image,sizeof(image)));free(s);put32(image+offsets[n],old);
    }
    s=fixture();s->effect_count=1;s->state_offset=0x818;s->effects[0]=(h2_dsp_effect){.state_offset=0x818,.state_bytes=8};
    s->core.xram[0x80]=0x123456;s->core.xram[0x81]=0xabcdef;uint8_t out[8];assert(h2_dsp_read_effect(s,0,1,out,5));assert(out[0]==0x34&&out[1]==0x12&&out[2]==0&&out[3]==0xef&&out[4]==0xcd);
    memset(out,0x55,sizeof(out));assert(!h2_dsp_read_effect(s,0,7,out,2));assert(!h2_dsp_read_effect(s,1,0,out,4));assert(!h2_dsp_read_effect(s,0,0xffffffff,out,1));for(unsigned i=0;i<8;i++)assert(out[i]==0x55);
    h2_dsp_destroy(s);
}
int main(void)
{
    uint32_t value=0x12345678;
    for(unsigned bits=1;bits<32;bits++)for(unsigned i=0;i<1024;i++){
        value=value*1664525u+1013904223u;uint32_t mask=(1u<<bits)-1,low=value&mask;
        uint32_t expected=low&(1u<<(bits-1))?low|~mask:low;
        assert(dsp_signextend((int)bits,value)==expected);
    }
    dma_tests();fault_tests();image_tests();puts("DSP engine: synthetic transfers, bounds, faults, no false ack, effect reads and 31,744 sign-extension cases pass");return 0;
}
