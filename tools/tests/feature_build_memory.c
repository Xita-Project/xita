#include "kernel/xk_feature_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){
    uint8_t *ram=calloc(1,65536);assert(ram);
    uint32_t pages[16];for(unsigned i=0;i<16;i++)pages[i]=4096*i;
    xk_feature_span output,stack,read,alias;
    assert(xk_feature_map_span(ram,65536,pages,16,4094,8,&output));
    assert(output.host==ram+4094 && output.physical==4094 && output.bytes==8);
    assert(xk_feature_map_span(ram,65536,pages,16,8192,4096,&stack));
    assert(xk_feature_map_span(ram,65536,pages,16,16384,16,&read));
    assert(xk_feature_layout_disjoint(&output,&stack,&read,1));
    /* Different guest pages, same physical output bytes. */
    pages[6]=0;pages[7]=4096;
    assert(xk_feature_map_span(ram,65536,pages,16,6*4096+4094,8,&alias));
    assert(xk_feature_spans_overlap(&output,&alias));
    assert(!xk_feature_layout_disjoint(&output,&stack,&alias,1));
    assert(!xk_feature_layout_disjoint(&output,&alias,&read,1));
    assert(!xk_feature_layout_disjoint(&output,&stack,&stack,1));
    xk_feature_span saved=alias;
    pages[7]=12288; /* split mapping: no cached host pointer is safe */
    assert(!xk_feature_map_span(ram,65536,pages,16,6*4096+4094,8,&alias));
    assert(saved.host==alias.host && saved.physical==alias.physical && saved.bytes==alias.bytes);
    assert(!xk_feature_map_span(ram,65536,pages,16,UINT32_MAX-1,8,&alias));
    assert(!xk_feature_map_span(ram,65536,pages,16,65535,2,&alias));
    assert(!xk_feature_map_span(ram,65536,pages,16,0,0,&alias));
    pages[1]=65536;
    assert(!xk_feature_map_span(ram,65536,pages,16,4096,1,&alias));
    pages[1]=1;
    assert(!xk_feature_map_span(ram,65536,pages,16,4096,1,&alias));
    /* Check the interval predicate against a byte-wise oracle, including
     * adjacent and empty ranges. This catches endpoint inclusivity mistakes. */
    for(unsigned a=0;a<24;a++)for(unsigned b=0;b<24;b++)
    for(unsigned na=0;na<12;na++)for(unsigned nb=0;nb<12;nb++){
        xk_feature_span x={NULL,a,na},y={NULL,b,nb};int overlap=0;
        for(unsigned i=0;i<na;i++)for(unsigned j=0;j<nb;j++)overlap|=a+i==b+j;
        assert(xk_feature_spans_overlap(&x,&y)==overlap);
    }
    free(ram);puts("PASS feature memory: contiguous/split mappings, physical aliases, bounds and 82944 interval cases");
}
