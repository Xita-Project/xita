#include "dsp_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t fingerprint(const uint8_t*p,size_t n)
{uint64_t v=UINT64_C(0xcbf29ce484222325);for(size_t i=0;i<n;i++)v=(v^p[i])*UINT64_C(0x100000001b3);return v;}
static uint32_t word(const uint8_t*p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
h2_dsp_engine*h2_dsp_asset_open(const char*path,h2_dsp_status*status)
{
    if(status)*status=(h2_dsp_status){.fault="private DSP asset unavailable or mismatched"};
    FILE*f=fopen(path,"rb");if(!f)return NULL;
    const size_t bytes=16+1484+28768;
    uint8_t*p=malloc(bytes);if(!p){fclose(f);return NULL;}
    int valid=fread(p,1,bytes,f)==bytes&&!ferror(f)&&fgetc(f)==EOF;
    if(fclose(f))valid=0;
    h2_dsp_engine*s=NULL;
    if(valid&&!memcmp(p,"H2DSP001",8)&&word(p+8)==1484&&word(p+12)==28768&&
       fingerprint(p+16,1484)==UINT64_C(0x432765cccdec4764)&&
       fingerprint(p+16+1484,28768)==UINT64_C(0x237b4363ea268059))
        s=h2_dsp_create(p+16,1484,p+16+1484,28768,status);
    free(p);return s;
}
