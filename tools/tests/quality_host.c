#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp/kernel/xk.h"
#include "recomp/kernel/xk_quality.h"

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void xv_logf(const char *fmt, ...) { (void)fmt; }

int main(int argc,char **argv)
{
    assert(argc==3);
    g_xram=calloc(1,64u<<20); g_img_base=g_xram; g_xpt=calloc(1u<<20,4);
    assert(g_xram&&g_xpt);
    for (unsigned i=0;i<16384;++i) g_xpt[i]=g_xpt[0x80000+i]=i*4096u;
    if (!strcmp(argv[1],"budget")) {
        setenv("XV_DECAL_LIMIT","32",1);
        uint32_t pool=0x80001000,clock=0x80002000,data=0x80010000;
        X_M32(0x2fab34)=pool; X_M32(0x2f8ca0)=clock;
        X_M16(pool+0x20)=2048; X_M16(pool+0x22)=0x38;
        X_M8(pool+0x24)=1; X_M32(pool+0x34)=data; X_M32(clock+0xc)=100;
        for (unsigned i=0;i<48;++i) {
            uint32_t p=data+i*0x38;
            X_M16(p)=1; X_M32(p+0x14)=i; X_MF32(p+0x1c)=300; X_MF32(p+0x20)=20;
        }
        X_M16(data+48*0x38)=1; X_M16(data+48*0x38+2)=2;
        X_MF32(data+48*0x38+0x1c)=300;
        xk_quality_decal_budget();
        for (unsigned i=0;i<48;++i) {
            uint32_t p=data+i*0x38;
            assert(X_MF32(p+0x1c)==(i<16 ? .000001f : 300.0f));
            assert(X_M32(p+0x14)==(i<16 ? 99u : i));
        }
        assert(X_MF32(data+48*0x38+0x1c)==300);
        puts("decal budget: oldest impact records expired; permanent record retained");
    } else {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        assert(!fseek(f,0,SEEK_END)); long size=ftell(f); rewind(f);
        assert(size>0 && size<32u*1024*1024);
        assert(fread(X_G(0x803a6000),1,size,f)==(size_t)size); fclose(f);
        xk_quality_map_read(0x803a6000,(uint32_t)size);
        f=fopen(argv[2],"wb"); assert(f);
        assert(fwrite(X_G(0x803a6000),1,size,f)==(size_t)size); assert(!fclose(f));
    }
    free(g_xram);free(g_xpt);return 0;
}
