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
    s->core.xram[0xbff]=0x112233;s->core.xram[0xc00]=0x445566;s->core.mixbuffer[0]=0xaabbcc;
    assert(h2_dsp_copy_space(s,0,0x2fff,out,5));
    assert(out[0]==0&&out[1]==0xcc&&out[2]==0xbb&&out[3]==0xaa&&out[4]==0);
    s->core.yram[0]=0x778899;assert(h2_dsp_copy_space(s,1,0,out,4));assert(out[0]==0x99&&out[1]==0x88&&out[2]==0x77&&out[3]==0);
    memset(out,0x55,sizeof(out));assert(!h2_dsp_copy_space(s,1,0x1fff,out,2));assert(!h2_dsp_copy_space(s,4,0,out,1));
    assert(!h2_dsp_copy_space(s,3,0xffffffff,out,1));for(unsigned i=0;i<8;i++)assert(out[i]==0x55);
    active=s;uint32_t original_mix=s->core.mixbuffer[0];assert(!h2_dsp_zero_frame(s));assert(s->core.mixbuffer[0]==original_mix);active=NULL;
    h2_dsp_destroy(s);
}
static void signal_tests(void)
{
    h2_dsp_engine*s=fixture();int32_t input[32][32],output[32];
    for(unsigned b=0;b<32;b++)for(unsigned i=0;i<32;i++)
        input[b][i]=i==0?-8388608:i==31?8388607:(int32_t)(b*32+i)-512;
    memset(output,0x55,sizeof output);assert(!h2_dsp_read_fx_frame(s,13,output));
    uint32_t before[DSP_MIXBUFFER_SIZE];memcpy(before,s->core.mixbuffer,sizeof before);
    for(unsigned b=0;b<32;b++)for(unsigned edge=0;edge<2;edge++){
        unsigned i=edge?31:0;int32_t old=input[b][i];input[b][i]=edge?8388608:-8388609;
        assert(!h2_dsp_mix_frame(s,input));assert(!memcmp(before,s->core.mixbuffer,sizeof before));
        assert(!s->status.frames&&!s->status.instructions&&!s->status.fault);input[b][i]=old;
    }
    assert(!h2_dsp_mix_frame(s,NULL));active=s;assert(!h2_dsp_mix_frame(s,input));active=NULL;
    /* Construct MOVEP X0,X:FFFFC4 from the documented interpreter bitfields,
     * exercising the actual checked frame-halt peripheral. */
    s->core.registers[DSP_REG_X0]=1;
    s->core.pram[0]=(8u<<16)|(1u<<15)|(1u<<14)|(DSP_REG_X0<<8)|4;
    assert(h2_dsp_mix_frame(s,input));assert(s->status.frames==1&&s->status.instructions==1);
    for(unsigned b=0;b<32;b++)for(unsigned i=0;i<32;i++)assert(s->core.mixbuffer[b*32+i]==((uint32_t)input[b][i]&0xffffffu));
    /* Same range/control as original monitor export: 20 bins, 32 words each.
     * The synthetic payload includes negative extrema and each lane/bin. */
    descriptor(s,0x49e2,640,0x1560,0xb000,0,0xb001);assert(transfer(s));
    for(unsigned bin=11;bin<=30;bin++){
        assert(h2_dsp_read_fx_frame(s,bin,output));assert(!memcmp(input[bin],output,sizeof output));
    }
    const unsigned bad[]={0,10,31,32,UINT32_MAX};memset(output,0x55,sizeof output);
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!h2_dsp_read_fx_frame(s,bad[i],output));
    assert(!h2_dsp_read_fx_frame(s,13,NULL));active=s;assert(!h2_dsp_read_fx_frame(s,13,output));active=NULL;
    s->scratch_size=0xb9ff;assert(!h2_dsp_read_fx_frame(s,13,output));s->scratch_size=0x10000;
    s->core.pc=0x1000;assert(!h2_dsp_mix_frame(s,input));assert(s->status.fault);
    assert(!h2_dsp_read_fx_frame(s,13,output));assert(!h2_dsp_mix_frame(s,input));
    for(unsigned i=0;i<32;i++)assert(output[i]==0x55555555);
    h2_dsp_destroy(s);
}
static void effect_write_tests(void)
{
    h2_dsp_engine*s=fixture();s->image_size=0x1000;s->effect_count=1;s->state_offset=0x818;
    s->effects[0]=(h2_dsp_effect){.state_offset=0x818,.state_bytes=128};
    memset(s->scratch,0xa5,s->scratch_size);
    for(unsigned i=0;i<DSP_XRAM_SIZE;i++)s->core.xram[i]=0x13579b;
    uint64_t frames=s->status.frames,instructions=s->status.instructions;
    const uint32_t pairs[][2]={{0,30},{0xabcdef,0x123456},{0xffffff,0},{1,0x800000}};
    for(unsigned p=0;p<sizeof pairs/sizeof *pairs;p++){
        assert(h2_dsp_write_effect_pair(s,0,32,pairs[p][0],pairs[p][1]));
        for(unsigned i=0;i<s->scratch_size;i++)if(i<0x838 || i>=0x840)assert(s->scratch[i]==0xa5);
        for(unsigned i=0;i<DSP_XRAM_SIZE;i++)if(i!=0x88 && i!=0x89)assert(s->core.xram[i]==0x13579b);
        assert(le32(s->scratch+0x838)==pairs[p][0] && le32(s->scratch+0x83c)==pairs[p][1]);
        assert(s->status.frames==frames && s->status.instructions==instructions);
        /* Interpreter MOVEs consume each newly written word; a subsequent
         * checked halt completes the frame. These are synthetic opcodes. */
        s->core.registers[DSP_REG_R0]=0x88;s->core.registers[DSP_REG_Y1]=1;s->core.pc=0;
        s->core.pram[0]=(1u<<17)|(1u<<7)|(1u<<4)|DSP_REG_X0;
        s->core.pram[1]=(1u<<17)|(1u<<7)|(1u<<6)|(1u<<4)|DSP_REG_Y0;
        s->core.pram[2]=(8u<<16)|(1u<<15)|(1u<<14)|(DSP_REG_Y1<<8)|4;
        int completed=frame(s);
        if(!completed)fprintf(stderr,"pair frame fault=%s pc=%x address=%x value=%x\n",s->status.fault,s->core.pc,s->status.fault_address,s->status.fault_value);
        assert(completed);assert(s->core.registers[DSP_REG_X0]==pairs[p][0] && s->core.registers[DSP_REG_Y0]==pairs[p][1]);
        frames=s->status.frames;instructions=s->status.instructions;
    }
    h2_dsp_engine *before=malloc(sizeof *before);uint8_t*scratch=malloc(s->scratch_size);assert(before&&scratch);
    *before=*s;memcpy(scratch,s->scratch,s->scratch_size);
    const uint32_t bad[][4]={{1,32,0,30},{0,33,0,30},{0,124,0,30},{0,UINT32_MAX,0,30},
        {0,32,0x1000000,0},{0,32,0,0xff000000}};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){
        assert(!h2_dsp_write_effect_pair(s,bad[i][0],bad[i][1],bad[i][2],bad[i][3]));
        assert(!memcmp(s,before,sizeof *s)&&!memcmp(s->scratch,scratch,s->scratch_size));
    }
    assert(!h2_dsp_write_effect_pair(NULL,0,32,0,30));
    active=s;assert(!h2_dsp_write_effect_pair(s,0,32,0,30));active=NULL;
    s->status.fault="test";assert(!h2_dsp_write_effect_pair(s,0,32,0,30));s->status.fault=NULL;
    assert(!memcmp(s,before,sizeof *s)&&!memcmp(s->scratch,scratch,s->scratch_size));
    s->effects[0].state_offset=0x817;assert(!h2_dsp_write_effect_pair(s,0,32,0,30));
    s->effects[0].state_offset=0x2e10;assert(!h2_dsp_write_effect_pair(s,0,32,0,30));
    s->effects[0].state_offset=0x818;s->image_size=0x83f;assert(!h2_dsp_write_effect_pair(s,0,32,0,30));
    assert(!memcmp(s->scratch,scratch,s->scratch_size));free(before);free(scratch);h2_dsp_destroy(s);
}
static h2_dsp_engine *reverb_fixture(void)
{
    h2_dsp_engine*s=fixture();s->image_size=0x2000;s->effect_count=10;
    s->code_words=32;s->state_offset=0x898;s->state_bytes=0x1000;
    s->status.frames=2;s->core.is_idle=true;
    s->effects[9]=(h2_dsp_effect){.state_offset=0xa00,.state_bytes=0x240};
    s->effects[8]=(h2_dsp_effect){.state_offset=0xd00,.state_bytes=0x240};
    memset(s->scratch,0xa5,s->scratch_size);
    put32(s->scratch+0x804,32);put32(s->scratch+0x810,0);put32(s->scratch+0x814,0);
    return s;
}
static void reverb_queue_tests(void)
{
    uint32_t parameters[66];
    for(unsigned i=0;i<66;++i)parameters[i]=i%2?0xff800001u+i:0x123456u+i;
    for(unsigned index=8;index<=9;++index){
    unsigned shadow=index==8?0xd00:0xa00;
    int (*submit)(h2_dsp_engine*,uint32_t,const uint32_t*)=index==8?h2_dsp_queue_reverb8:h2_dsp_queue_reverb9;
    for(unsigned aliased=0;aliased<2;++aliased){
        h2_dsp_engine*s=reverb_fixture(),*before=malloc(sizeof *s);
        uint8_t*expected=malloc(s->scratch_size);assert(before&&expected);
        uint32_t copy[66];const uint32_t*source=parameters;
        if(aliased)source=(const uint32_t*)(s->scratch+shadow+16);
        memcpy(copy,source,sizeof copy);*before=*s;memcpy(expected,s->scratch,s->scratch_size);
        put32(expected+shadow+16,7);
        for(unsigned i=0;i<66;++i)put32(expected+shadow+280+i*4,copy[i]);
        put32(expected+0x800,(shadow+16-0x898)/4);put32(expected+0x808,shadow+16);
        put32(expected+0x80c,132);put32(expected+0x810,2);
        assert(submit(s,7,source));
        assert(!memcmp(s,before,sizeof *s)); /* no live GP/history/counter change */
        assert(!memcmp(s->scratch,expected,s->scratch_size)); /* includes saved gap */
        assert(!submit(s,7,parameters)); /* pending monitor command */
        assert(!h2_dsp_queue_reverb8(s,7,parameters)&&!h2_dsp_queue_reverb9(s,7,parameters));
        assert(!memcmp(s,before,sizeof *s)&&!memcmp(s->scratch,expected,s->scratch_size));
        free(before);free(expected);h2_dsp_destroy(s);
    }
    for(unsigned bad=0;bad<22;++bad){
        h2_dsp_engine*s=reverb_fixture();uint32_t flags=7;const uint32_t*source=parameters;
        switch(bad){
        case 0:source=NULL;break;
        case 1:s->status.fault="injected";break;
        case 2:active=s;break;
        case 3:s->status.frames=0;break;
        case 4:s->core.is_idle=false;break;
        case 5:s->effect_count=index;break;
        case 6:s->scratch_size=0x817;break;
        case 7:s->image_size=0x817;break;
        case 8:flags=0x1000007;break;
        case 9:flags=3;break;
        case 10:s->state_offset++;break;
        case 11:put32(s->scratch+0x804,33);break;
        case 12:put32(s->scratch+0x810,3);break;
        case 13:put32(s->scratch+0x814,1);break;
        case 14:s->effects[index].state_offset=0x800;break;
        case 15:s->effects[index].state_offset=0xa01;break;
        case 16:s->effects[index].state_bytes=543;break;
        case 17:s->state_bytes=shadow-s->state_offset+543;break;
        case 18:s->effects[index].state_offset=0xfffffffcu;break;
        case 19:s->image_size=shadow+543;break;
        case 20:s->scratch_size=shadow+543;break;
        case 21:s->effects[index].state_offset=0x3600;s->state_bytes=0x4000;s->image_size=0x4000;break;
        }
        h2_dsp_engine*before=malloc(sizeof *s);uint8_t*saved=malloc(0x10000);assert(before&&saved);
        *before=*s;memcpy(saved,s->scratch,0x10000);
        assert(!submit(s,flags,source));
        assert(!memcmp(s,before,sizeof *s)&&!memcmp(s->scratch,saved,0x10000));
        active=NULL;free(before);free(saved);h2_dsp_destroy(s);
    }
    }
    assert(!h2_dsp_queue_reverb9(NULL,7,parameters)&&!h2_dsp_queue_reverb8(NULL,7,parameters));
    h2_dsp_engine*s=reverb_fixture();h2_dsp_engine before=*s;uint8_t saved[0x10000];memcpy(saved,s->scratch,sizeof saved);
    assert(!queue_reverb(s,7,7,parameters)&&!queue_reverb(s,UINT32_MAX,7,parameters));
    assert(!memcmp(s,&before,sizeof before)&&!memcmp(saved,s->scratch,sizeof saved));h2_dsp_destroy(s);
}
int main(void)
{
    uint32_t value=0x12345678;
    for(unsigned bits=1;bits<32;bits++)for(unsigned i=0;i<1024;i++){
        value=value*1664525u+1013904223u;uint32_t mask=(1u<<bits)-1,low=value&mask;
        uint32_t expected=low&(1u<<(bits-1))?low|~mask:low;
        assert(dsp_signextend((int)bits,value)==expected);
    }
    dma_tests();fault_tests();image_tests();signal_tests();effect_write_tests();reverb_queue_tests();puts("DSP engine: synthetic transfers, checked signal frames/FX export, faults, no false ack, effect reads/writes/real consumption, deferred reverb queue and 31,744 sign-extension cases pass");return 0;
}
