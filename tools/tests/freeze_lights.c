/* cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g
 * -Iruntime tools/tests/freeze_lights.c -o /tmp/freeze-lights */
#include "xv_freeze_lights.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void put16(void *p,uint16_t v) { memcpy(p,&v,2); }
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
int main(void)
{
    uint8_t *arena=calloc(1,65536);
    uint32_t *pt=malloc((1u<<20)*4u);
    xv_freeze_light_sample *s=calloc(1,sizeof *s);
    assert(arena&&pt&&s);
    for(unsigned i=0;i<(1u<<20);++i) pt[i]=65536;
    pt[0x2fc]=0; pt[0x80001]=4096; pt[0x80002]=8192;
    pt[0x80003]=12288; pt[0x80004]=16384;
    put32(arena+0x670,0x80003000); put32(arena+0x674,0x80001000);
    put16(arena+4096+0x20,4); put16(arena+4096+0x22,12);
    put32(arena+4096+0x34,0x80002ff8); /* crosses a mapped page */
    put32(arena+8192+4092,0x1234);
    put32(arena+12288,0xbeef0001); /* next[0] */
    put32(arena+12288+12,UINT32_MAX); /* next[1] */
    xv_freeze_light_sample_read(s,arena,pt,65536);
    assert(s->valid==15 && s->refs[0][1]==0x1234);
    unsigned steps;
    assert(xv_freeze_light_chain(s,0xabcd0000,&steps)==0 && steps==2);
    s->refs[1][2]=0x98760000;
    assert(xv_freeze_light_chain(s,0xabcd0000,&steps)==1 && steps==2);
    s->refs[1][2]=0x98760004;
    assert(xv_freeze_light_chain(s,0,&steps)==2);
    assert(xv_freeze_light_chain(s,UINT32_MAX,&steps)==0 && steps==0);
    s->valid=3; assert(xv_freeze_light_chain(s,0,&steps)==3);
    uint8_t buf[8];
    assert(!xv_freeze_light_read(arena,pt,65536,UINT32_MAX,buf,2));
    assert(!xv_freeze_light_read(arena,pt,65536,0x50000,buf,1));
    pt[0x500]=65535;
    assert(!xv_freeze_light_read(arena,pt,65536,0x500000,buf,1));
    pt[0x80003]=65536;
    xv_freeze_light_sample_read(s,arena,pt,65536);
    assert(s->valid==3); /* partial table and heads must not be labeled valid */
    put16(arena+4096+0x20,2049);
    xv_freeze_light_sample_read(s,arena,pt,65536); assert(s->valid==3);
    put16(arena+4096+0x20,4); put16(arena+4096+0x22,16);
    xv_freeze_light_sample_read(s,arena,pt,65536); assert(s->valid==3);
    free(s);free(pt);free(arena); puts("freeze lights: bounded capture and chain tests passed");
}
