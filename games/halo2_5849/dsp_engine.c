#include "dsp_engine.h"
#include "dsp/interp/dsp_cpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include <limits.h>

#define SCRATCH_LIMIT (2u * 1024u * 1024u)
#define EFFECT_LIMIT 64u
#define FRAME_LIMIT 2000000u
#define DMA_NODE_LIMIT 256u
#define START 1u
#define STOP 2u
#define FREEZE 3u
#define UNFREEZE 4u
#define FROZEN 8u
#define RUNNING 16u
#define STOPPED 32u

struct h2_dsp_engine {
    dsp_core_t core;
    uint8_t *scratch;
    uint32_t scratch_size, image_size, code_words, state_offset, state_bytes;
    uint32_t effect_count;
    h2_dsp_effect effects[EFFECT_LIMIT];
    uint32_t dma_control, dma_next, dma_start, dma_configuration, dma_reads;
    uint32_t interrupts, eol, counter_config[3];
    h2_dsp_status status;
    jmp_buf escape;
};
/* The upstream opcode cache is also process-global. All engine calls must
 * therefore be serialized; the H2 adapter uses its existing audio mutex. */
static h2_dsp_engine *active;
static uint32_t le32(const void *v)
{ const uint8_t *p = v; return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(void *v, uint32_t n)
{ uint8_t *p=v; for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }
static _Noreturn void fault(h2_dsp_engine *s, const char *why, uint32_t a, uint32_t v)
{
    s->status.fault=why; s->status.fault_address=a; s->status.fault_value=v;
    s->status.pc=s->core.pc;
    longjmp(s->escape,1);
}
_Noreturn void h2_dsp_assert_failure(const char *condition, const char *file, int line)
{
    (void)file;
    if(active) fault(active,condition,active->core.pc,(uint32_t)line);
    fprintf(stderr,"DSP assertion outside protected execution: %s\n",condition); abort();
}
static int decode_address(uint32_t encoded, uint32_t count, int *space, uint32_t *address)
{
    uint32_t limit;
    if(encoded<0x1800) { *space=DSP_SPACE_X;*address=encoded;limit=0x1800; }
    else if(encoded<0x2000) { *space=DSP_SPACE_Y;*address=encoded-0x1800;limit=0x800; }
    else if(encoded>=0x2800 && encoded<0x3800) { *space=DSP_SPACE_P;*address=encoded-0x2800;limit=0x1000; }
    else return 0;
    if(count>limit-*address) return 0;
    /* X has two mapped mix-buffer windows and an unmapped hole. */
    if(*space==DSP_SPACE_X && count && *address<0x1400 && *address+count>0x1000) return 0;
    return 1;
}
static uint32_t memread(h2_dsp_engine*s,int space,uint32_t address)
{ return dsp56k_read_memory(&s->core,space,address); }
static void memwrite(h2_dsp_engine*s,int space,uint32_t address,uint32_t value)
{ dsp56k_write_memory(&s->core,space,address,value); }
static void dma_run(h2_dsp_engine*s)
{
    if(!(s->dma_control&RUNNING)||(s->dma_control&FROZEN))return;
    unsigned nodes=0;
    while(!(s->dma_next&0x4000)){
        if(++nodes>DMA_NODE_LIMIT)fault(s,"DMA chain budget",s->dma_next,nodes);
        if(s->dma_next&~0x7fffu)fault(s,"DMA node flags",s->dma_next,0);
        int ns;uint32_t na;
        if(!decode_address(s->dma_next&0x3fff,7,&ns,&na))fault(s,"DMA node range",s->dma_next,7);
        uint32_t d[7];for(unsigned i=0;i<7;i++)d[i]=memread(s,ns,na+i);
        /* Exact non-interleaved, unit-step encodings observed in the owned GP
         * monitor/effects. Format-2 bit9 variants follow pinned xemu's 24-bit
         * scratch representation; no FIFO or arbitrary format is accepted. */
        switch(d[1]){
        case 0x59e0:case 0x59e2:case 0x59d2:case 0x49e2:case 0x4bd2:case 0x4bd0:break;
        default:fault(s,"unsupported DMA control",s->dma_next,d[1]);
        }
        if(d[0]&~0x7fffu)fault(s,"DMA next flags",s->dma_next,d[0]);
        int ms;uint32_t ma;
        if(!d[2]||!decode_address(d[3],d[2],&ms,&ma))fault(s,"DMA data range",d[3],d[2]);
        uint32_t offset=d[4],base=d[5],size=d[6]+1u;
        int circular=((d[1]>>5)&15)==14,write=(d[1]&2)!=0;
        uint64_t bytes=(uint64_t)d[2]*4;
        if(circular){
            if(!size||base>s->scratch_size||size>s->scratch_size-base||offset>=size||((size|offset|base)&3))
                fault(s,"DMA circular scratch range",base,size);
        }else{
            /* Linear scratch uses base+offset as pinned reference does. */
            if((uint64_t)base+offset+bytes>s->scratch_size||((base|offset)&3))
                fault(s,"DMA linear scratch range",offset,(uint32_t)bytes);
        }
        /* Validate every range/control before any node mutation or transfer. */
        for(uint32_t i=0;i<d[2];i++){
            uint32_t addr=base+offset;
            if(write)put32(s->scratch+addr,memread(s,ms,ma+i));
            else memwrite(s,ms,ma+i,le32(s->scratch+addr)&0xffffffu);
            offset+=4;if(circular&&offset==size)offset=0;
        }
        if(d[1]&16)memwrite(s,ns,na+4,offset);
        s->dma_next=d[0];if(s->dma_next&0x4000)s->eol=1;
        ++s->status.transfers;
    }
}
static uint32_t read_peripheral(dsp_core_t*c,uint32_t address)
{
    h2_dsp_engine*s=c->opaque;
    switch(address){
    case 0xffffb3:
        /* Logical interpreter-cycle profiling counter, not Xbox wall time.
         * This exact monitor uses it only for X:7C..7F diagnostics; its branch
         * alternatives rejoin before any effect work. */
        if(c->pc!=0x2d&&c->pc!=0x33&&c->pc!=0x3a&&c->pc!=0x4d)
            fault(s,"unreviewed profiling-counter reader",c->pc,address);
        return (uint32_t)s->status.cycles&0xffffff;
    case 0xffffc5:return s->interrupts|(s->eol?128:0);
    case 0xffffd4:return s->dma_next;
    case 0xffffd5:return s->dma_start;
    case 0xffffd6:
        /* Synchronous transfers complete before START returns. Retain the
         * pinned reference's three-poll RUNNING observation required by the
         * original monitor, never clear EOL before a completed transfer. */
        if((s->dma_control&RUNNING)&&++s->dma_reads>2){s->dma_control=(s->dma_control&~RUNNING)|STOPPED;s->dma_reads=0;}
        return s->dma_control;
    case 0xffffd7:return s->dma_configuration;
    default:fault(s,"unsupported DSP peripheral read",address,0);
    }
}
static void write_peripheral(dsp_core_t*c,uint32_t address,uint32_t value)
{
    h2_dsp_engine*s=c->opaque;
    switch(address){
    case 0xffffb0:case 0xffffb1:case 0xffffb2:
        if((address==0xffffb2&&(c->pc!=0x23||value!=0xffffff))||
           (address==0xffffb0&&(c->pc!=0x25||value!=1))||
           (address==0xffffb1&&(c->pc!=0x27||value!=1)))
            fault(s,"unreviewed profiling-counter setup",address,value);
        s->counter_config[address-0xffffb0]=value;return;
    case 0xffffc4:
        if(value!=1)fault(s,"unsupported DSP halt control",address,value);
        c->is_idle=true;return;
    case 0xffffc5:
        if(value&~0xfffu)fault(s,"DSP interrupt clear flags",address,value);
        s->interrupts&=~value;if(value&128)s->eol=0;return;
    case 0xffffd4:s->dma_next=value;return;
    case 0xffffd5:s->dma_start=value;return;
    case 0xffffd7:
        if(value)fault(s,"unsupported DSP DMA configuration",address,value);
        s->dma_configuration=value;return;
    case 0xffffd6:
        switch(value){
        case START:s->dma_control=(s->dma_control|RUNNING)&~STOPPED;s->dma_reads=0;break;
        case STOP:s->dma_control=(s->dma_control|STOPPED)&~RUNNING;break;
        case FREEZE:s->dma_control|=FROZEN;break;
        case UNFREEZE:s->dma_control&=~FROZEN;break;
        default:fault(s,"unsupported DSP DMA action",address,value);
        }
        dma_run(s);return;
    default:fault(s,"unsupported DSP peripheral write",address,value);
    }
}
static int frame(h2_dsp_engine*s)
{
    if(!s||s->status.fault||active)return 0;
    active=s;
    if(setjmp(s->escape)){active=NULL;return 0;}
    s->interrupts|=2;s->core.is_idle=false;
    for(unsigned i=0;i<FRAME_LIMIT;i++){
        dsp56k_execute_instruction(&s->core);
        s->status.instructions++;s->status.cycles+=s->core.instr_cycle;
        if(s->core.is_idle){s->status.frames++;active=NULL;return 1;}
    }
    fault(s,"DSP frame instruction budget",s->core.pc,FRAME_LIMIT);
}
static uint64_t hash_bytes(uint64_t v,const uint8_t*p,size_t n)
{for(size_t i=0;i<n;i++)v=(v^p[i])*UINT64_C(0x100000001b3);return v;}
static uint64_t hash_words(uint64_t v,const uint32_t*p,size_t n)
{for(size_t i=0;i<n;i++){uint8_t b[4];put32(b,p[i]);v=hash_bytes(v,b,4);}return v;}
static uint64_t state_hash(const h2_dsp_engine*s)
{
    uint64_t v=UINT64_C(0xcbf29ce484222325);
    v=hash_words(v,s->core.registers,DSP_REG_MAX);
    v=hash_words(v,s->core.stack[0],16);v=hash_words(v,s->core.stack[1],16);
    v=hash_words(v,s->core.xram,DSP_XRAM_SIZE);v=hash_words(v,s->core.yram,DSP_YRAM_SIZE);
    v=hash_words(v,s->core.pram,DSP_PRAM_SIZE);v=hash_words(v,s->core.mixbuffer,DSP_MIXBUFFER_SIZE);
    return hash_bytes(v,s->scratch,s->scratch_size);
}
void h2_dsp_snapshot(const h2_dsp_engine*s,h2_dsp_status*out)
{
    if(!out)return;
    if(!s){memset(out,0,sizeof(*out));return;}
    *out=s->status;out->pc=s->core.pc;out->command=le32(s->scratch+0x810);
    out->effect_count=s->effect_count;out->scratch_bytes=s->scratch_size;out->state_fingerprint=state_hash(s);
}
void h2_dsp_destroy(h2_dsp_engine*s)
{if(s){free(s->scratch);free(s);}}
static int validate(h2_dsp_engine*s,const uint8_t*image,size_t bytes)
{
    if(bytes<0x820||bytes>0xc000)return 0;
    if(le32(image+0x800)||le32(image+0x810)!=3||le32(image+0x814))return 0;
    uint32_t code=le32(image+0x804),state=le32(image+0x80c),offset=le32(image+0x808);
    if(!code||code>0x1000-0x171||!state||state>0x1000-0x80||offset!=0x818+code*4)return 0;
    uint64_t descriptor=(uint64_t)offset+state*4;
    if(descriptor+8>bytes)return 0;
    uint32_t count=le32(image+descriptor),scratch=le32(image+descriptor+4);
    if(!count||count>EFFECT_LIMIT||descriptor+8+(uint64_t)count*40!=bytes||scratch>SCRATCH_LIMIT-0xc000)return 0;
    for(uint32_t i=0;i<code;i++)if(le32(image+0x818+i*4)>0xffffff)return 0;
    for(uint32_t i=0;i<count;i++){
        const uint8_t*p=image+descriptor+8+i*32;
        uint32_t d[8];for(unsigned j=0;j<8;j++)d[j]=le32(p+j*4);
        if((d[0]|d[1]|d[2]|d[3]|d[4]|d[5]|d[6]|d[7])&3)return 0;
        if(!d[1]||d[0]<0x818||(uint64_t)d[0]+d[1]>offset||!d[3]||d[2]<offset||
           (uint64_t)d[2]+d[3]>descriptor||(uint64_t)d[4]+d[5]>0x800*4||
           d[6]<0xc000||(uint64_t)d[6]+d[7]>0xc000+scratch)return 0;
        s->effects[i]=(h2_dsp_effect){d[0],d[1],d[2],d[3],d[4],d[5],d[6]-0xc000,d[7]};
    }
    s->effect_count=count;s->scratch_size=0xc000+scratch;s->image_size=(uint32_t)bytes;
    s->code_words=code;s->state_offset=offset;s->state_bytes=state*4;
    return 1;
}
h2_dsp_engine*h2_dsp_create(const void*monitor,size_t monitor_bytes,const void*image,size_t image_bytes,h2_dsp_status*status)
{
    if(status)memset(status,0,sizeof(*status));
    if(active||!monitor||!image||monitor_bytes!=0x5cc)return NULL;
    h2_dsp_engine*s=calloc(1,sizeof(*s));if(!s)return NULL;
    if(!validate(s,image,image_bytes)){s->status.fault="DSP image layout";goto failed;}
    const uint8_t*m=monitor;
    for(unsigned i=0;i<monitor_bytes/4;i++)if(le32(m+i*4)>0xffffff){s->status.fault="DSP monitor word";goto failed;}
    s->scratch=calloc(1,s->scratch_size);if(!s->scratch){s->status.fault="DSP allocation";goto failed;}
    memcpy(s->scratch,m,monitor_bytes);
    s->core.is_gp=true;s->core.opaque=s;s->core.read_peripheral=read_peripheral;s->core.write_peripheral=write_peripheral;
    dsp56k_reset_cpu(&s->core);
    for(unsigned i=0;i<0x800;i++)s->core.pram[i]=le32(s->scratch+i*4)&0xffffff;
    if(!frame(s))goto failed;
    memcpy(s->scratch+0x800,(const uint8_t*)image+0x800,image_bytes-0x800);
    if(!frame(s))goto failed;
    if(le32(s->scratch+0x810)){s->status.fault="DSP download not acknowledged";goto failed;}
    h2_dsp_snapshot(s,status);return s;
failed:
    if(status){*status=s->status;status->pc=s->core.pc;}
    h2_dsp_destroy(s);return NULL;
}
int h2_dsp_effect_map(const h2_dsp_engine*s,uint32_t index,h2_dsp_effect*out)
{if(!s||s->status.fault||!out||index>=s->effect_count)return 0;*out=s->effects[index];return 1;}
int h2_dsp_read_effect(const h2_dsp_engine*s,uint32_t index,uint32_t offset,void*out,uint32_t bytes)
{
    if(!s||s->status.fault||!out||index>=s->effect_count)return 0;
    const h2_dsp_effect*e=&s->effects[index];
    if(offset>e->state_bytes||bytes>e->state_bytes-offset)return 0;
    uint32_t address=0x200+e->state_offset-s->state_offset+offset;
    if((uint64_t)address+bytes>sizeof(s->core.xram))return 0;
    uint8_t*dest=out;
    for(uint32_t i=0;i<bytes;i++){uint32_t a=address+i, word=a/4;
        uint32_t value=word>=0xc00&&word<0x1000?s->core.mixbuffer[word-0xc00]:s->core.xram[word];
        dest[i]=(uint8_t)(value>>((a&3)*8));}
    return 1;
}
int h2_dsp_write_effect_pair(h2_dsp_engine*s,uint32_t index,uint32_t offset,uint32_t first,uint32_t second)
{
    if(!s||s->status.fault||active||index>=s->effect_count||(offset&3)||
       ((first|second)&0xff000000u))return 0;
    const h2_dsp_effect*e=&s->effects[index];
    if(offset>e->state_bytes||8>e->state_bytes-offset||e->state_offset<s->state_offset)return 0;
    uint64_t shadow=(uint64_t)e->state_offset+offset;
    uint64_t address=0x200ull+e->state_offset-s->state_offset+offset;
    if(!s->scratch||(shadow&3)||(address&3)||shadow+8>s->image_size||
       shadow+8>s->scratch_size||address+8>0xc00u*4)return 0;
    /* Original 383D79 shadow copy followed by 37E5C6 immediate GP writes.
     * All checks precede both writes; no monitor, histories or counters reset. */
    put32(s->scratch+shadow,first);put32(s->scratch+shadow+4,second);
    s->core.xram[address/4]=first;s->core.xram[address/4+1]=second;
    return 1;
}
int h2_dsp_queue_reverb9(h2_dsp_engine*s,uint32_t flags,const uint32_t parameters[66])
{
    if(!s||!parameters||s->status.fault||active||!s->status.frames||
       !s->core.is_idle||s->effect_count<=9||!s->scratch||
       s->scratch_size<0x818||s->image_size<0x818||
       (flags&0xff000000u)||!(flags&4))return 0;
    const h2_dsp_effect*e=&s->effects[9];
    uint64_t expected=0x818ull+(uint64_t)s->code_words*4;
    uint64_t shadow=e->state_offset,live=0x200ull+shadow-s->state_offset;
    if(expected!=s->state_offset||le32(s->scratch+0x804)!=s->code_words||
       le32(s->scratch+0x810)||le32(s->scratch+0x814)||
       shadow<s->state_offset||(shadow&3)||e->state_bytes<544||
       shadow+544>(uint64_t)s->state_offset+s->state_bytes||
       shadow+544>s->image_size||shadow+544>s->scratch_size||
       live+544>0xc00u*4)return 0;
    uint32_t copy[66];memcpy(copy,parameters,sizeof copy);
    /* Original deferred coalescing starts at offset16, retains the saved
     * image gap at20..279, and ends after the 264-byte parameter payload. */
    put32(s->scratch+shadow+16,flags);
    for(unsigned i=0;i<66;++i)put32(s->scratch+shadow+280+i*4,copy[i]);
    put32(s->scratch+0x800,(uint32_t)((shadow+16-s->state_offset)/4));
    put32(s->scratch+0x808,(uint32_t)shadow+16);
    put32(s->scratch+0x80c,132);
    /* Publish last. The caller's mixer lock prevents a partial observation. */
    put32(s->scratch+0x810,2);
    return 1;
}
int h2_dsp_zero_frame(h2_dsp_engine*s)
{
    if(!s||s->status.fault||active)return 0;
    memset(s->core.mixbuffer,0,sizeof(s->core.mixbuffer));
    return frame(s);
}
int h2_dsp_mix_frame(h2_dsp_engine*s,const int32_t bins[32][32])
{
    if(!s||s->status.fault||active||!bins)return 0;
    for(unsigned b=0;b<32;b++)for(unsigned i=0;i<32;i++)
        if(bins[b][i]<-8388608||bins[b][i]>8388607)return 0;
    for(unsigned b=0;b<32;b++)for(unsigned i=0;i<32;i++)
        s->core.mixbuffer[b*32+i]=(uint32_t)bins[b][i]&0xffffffu;
    return frame(s);
}
int h2_dsp_read_fx_frame(const h2_dsp_engine*s,unsigned bin,int32_t out[32])
{
    if(!s||s->status.fault||active||!s->status.frames||!out||bin<11||bin>30||s->scratch_size<0xba00)return 0;
    for(unsigned i=0;i<32;i++){
        int32_t value=(int32_t)(le32(s->scratch+0xb000+(bin-11)*128+i*4)&0xffffffu);
        out[i]=value&0x800000?value-0x1000000:value;
    }
    return 1;
}

int h2_dsp_copy_space(const h2_dsp_engine*s,unsigned space,uint32_t offset,void*out,uint32_t bytes)
{
    if(!s||s->status.fault||!out||space>3)return 0;
    uint32_t limit=space==0?DSP_XRAM_SIZE*4:space==1?DSP_YRAM_SIZE*4:space==2?DSP_PRAM_SIZE*4:s->scratch_size;
    if(offset>limit||bytes>limit-offset)return 0;
    if(space==3){memcpy(out,s->scratch+offset,bytes);return 1;}
    uint8_t*dest=out;
    for(uint32_t i=0;i<bytes;i++){
        uint32_t a=offset+i,word=a/4,value;
        if(space==0)value=word>=0xc00?s->core.mixbuffer[word-0xc00]:s->core.xram[word];
        else value=space==1?s->core.yram[word]:s->core.pram[word];
        dest[i]=(uint8_t)(value>>((a&3)*8));
    }
    return 1;
}
