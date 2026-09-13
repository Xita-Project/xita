#include "xv_constant_window.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t rng=1234;
static uint32_t next(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }

int main(void)
{
    float source[192][4], saved[192][4];
    for (unsigned i=0;i<192;i++) for (unsigned j=0;j<4;j++) {
        uint32_t value=next();memcpy(&source[i][j],&value,4);
    }
    memcpy(saved,source,sizeof saved);
    unsigned comparisons=0;
    const int16_t bases[]={INT16_MIN,-300,-289,-288,-193,-192,-97,-96,-95,-1,0,94,95,96,191,INT16_MAX};
    const uint16_t counts[]={0,1,2,4,16,96,191,192,193,256,512,UINT16_MAX};
    for (unsigned b=0;b<sizeof bases/sizeof *bases;b++)
        for (unsigned n=0;n<sizeof counts/sizeof *counts;n++) {
            unsigned count=counts[n];size_t bytes=(size_t)count*16+128;
            uint8_t *actual=malloc(bytes),*expected=malloc(bytes);
            assert(actual && expected);memset(actual,0xa5,bytes);memset(expected,0xa5,bytes);
            /* Deliberately vary the destination offset within its allocation;
             * the comparison includes every byte before and after the window. */
            unsigned offset=4*(1+comparisons%15);
            xv_constant_window_copy((float *)(actual+offset),source,bases[b],count);
            for (unsigned i=0;i<count;i++) {
                int row=(int)bases[b]+(int)i+96;
                if (row>=0 && row<192) memcpy(expected+offset+i*16,source[row],16);
                else memset(expected+offset+i*16,0,16);
            }
            assert(!memcmp(actual,expected,bytes));assert(!memcmp(saved,source,sizeof saved));
            free(actual);free(expected);comparisons++;
        }
    /* Every valid start/end combination, including the complete 192-row window. */
    float actual[192][4],expected[192][4];
    for (int first=0;first<=192;first++) for (unsigned count=0;count<=192u-first;count++) {
        memset(actual,0xa5,sizeof actual);memset(expected,0xa5,sizeof expected);
        xv_constant_window_copy(&actual[0][0],source,(int16_t)(first-96),(uint16_t)count);
        if (count) memcpy(expected,source[first],count*16);
        assert(!memcmp(actual,expected,sizeof actual));comparisons++;
    }
    printf("PASS constant windows: %u byte-exact comparisons, padding, bounds and unchanged source\n",comparisons);
    return 0;
}
