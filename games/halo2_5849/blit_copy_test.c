#include "command_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { BYTES=640*480*4, RAM_BYTES=0x300000, SOURCE=0x1000, DEST=0x180040 };
static uint32_t instance[0x5000/4];
static uint8_t ram[RAM_BYTES], before[RAM_BYTES];
static h2_command_state state;
static h2_kelvin_clear clear;
static uint32_t failed_word, failed_address;
static int alias_map, wrap_map, bad_tile;
static int read_instance(void *opaque, uint32_t offset, uint32_t *word) {
    (void)opaque;
    if ((offset&3) || offset<0x10000 || offset>=0x15000 || offset==failed_word) return 0;
    *word=instance[(offset-0x10000)/4]; return 1;
}
static void *map_ram(void *opaque, uint32_t address, uint32_t bytes) {
    (void)opaque;
    if (address==failed_address || address>RAM_BYTES || bytes>RAM_BYTES-address) return NULL;
    if (wrap_map) return (void *)(UINTPTR_MAX-31);
    if (alias_map && address==DEST) return ram+SOURCE+32;
    return ram+address;
}
static int attachment(void *opaque, uint32_t address, uint32_t bytes, uint32_t pitch, int zeta, uint32_t format) {
    (void)opaque; (void)address;
    assert(bytes==BYTES && pitch==2560 && !zeta && format==0x128); return !bad_tile;
}
static void graph(unsigned handle, unsigned klass) {
    instance[handle*2]=handle;
    instance[handle*2+1]=0x80010000|((0x12000+handle*16)>>4);
    instance[(0x2000+handle*16)/4]=klass;
}
static void dma(unsigned handle, unsigned klass, uint32_t limit) {
    instance[handle*2]=handle;
    instance[handle*2+1]=0x80000000|((0x13000+handle*16)>>4);
    unsigned i=(0x3000+handle*16)/4;
    instance[i]=0xB000|klass; instance[i+1]=limit; instance[i+2]=instance[i+3]=3;
}
static int emit(unsigned sub, unsigned method, uint32_t value) {
    return h2_command_method(&state,&clear,sub,method,value,0x1234);
}
static void reject(unsigned sub, unsigned method, uint32_t value) {
    h2_command_state old=state; h2_kelvin_clear old_clear=clear;
    memcpy(before,ram,sizeof ram); assert(!emit(sub,method,value));
    assert(!memcmp(&old,&state,sizeof state) && !memcmp(&old_clear,&clear,sizeof clear));
    assert(!memcmp(before,ram,sizeof ram));
}
int main(void) {
    for (unsigned i=0;i<RAM_BYTES;++i) ram[i]=(i*17u+(i>>8)*29u)^(i>>16);
    memset(ram+DEST,0xA5,BYTES);
    assert(h2_kelvin_clear_init(&clear,read_instance,map_ram,NULL,RAM_BYTES,0));
    clear.check_attachment=attachment;
    graph(1,0x9F); graph(2,0x62); graph(3,0x30);
    dma(4,2,RAM_BYTES-1); dma(5,3,RAM_BYTES-1);
    assert(emit(2,0,1) && emit(3,0,2) && emit(2,0x2FC,3));
    for (unsigned method=0x184;method<=0x198;method+=4) assert(emit(2,method,3));
    assert(emit(2,0x19C,2) && emit(3,0x184,4) && emit(3,0x188,5));
    reject(2,0x308,0x01E00280); /* no resource/point setup */
    reject(3,0x300,7); reject(3,0x304,0x0A0009FC);
    reject(3,0x308,0x08001000); reject(3,0x30C,DEST+1);
    reject(2,0x300,1); reject(2,0x304,0x10000);
    assert(emit(3,0x308,SOURCE) && emit(3,0x30C,DEST));
    assert(emit(3,0x300,0xA) && emit(3,0x304,0x0A000A00));
    assert(emit(2,0x300,0) && emit(2,0x304,0));
    assert(state.surfaces_valid==15 && state.blit_point_valid==3);
    for (unsigned bit=1;bit<16;bit<<=1) {
        state.surfaces_valid^=bit; reject(2,0x308,0x01E00280); state.surfaces_valid^=bit;
    }
    for (unsigned bit=1;bit<4;bit<<=1) {
        state.blit_point_valid^=bit; reject(2,0x308,0x01E00280); state.blit_point_valid^=bit;
    }
    reject(2,0x308,0); reject(2,0x308,0x01DF0280); reject(2,0x308,0x01E00281);
    for (unsigned i=0;i<2;++i) {
        failed_address=i?DEST:SOURCE; reject(2,0x308,0x01E00280); failed_address=0;
        for (unsigned lane=0;lane<4;++lane) {
            failed_word=state.surfaces_dma[i]+lane*4; reject(2,0x308,0x01E00280); failed_word=0;
        }
    }
    for (unsigned object=1;object<=3;++object) for (unsigned lane=0;lane<4;++lane) {
        unsigned index=(0x2000+object*16)/4+lane;
        instance[index]^=0x80000000; reject(2,0x308,0x01E00280); instance[index]^=0x80000000;
    }
    state.blit_context[6]^=16; reject(2,0x308,0x01E00280); state.blit_context[6]^=16;
    dma(5,2,RAM_BYTES-1); reject(2,0x308,0x01E00280); /* read-only destination */
    dma(5,3,DEST+BYTES-2); reject(2,0x308,0x01E00280); /* missing final byte */
    dma(5,3,RAM_BYTES-1);
    dma(4,3,RAM_BYTES-1); reject(2,0x308,0x01E00280); /* write-only source */
    dma(4,2,SOURCE+BYTES-2); reject(2,0x308,0x01E00280); dma(4,2,RAM_BYTES-1);
    assert(emit(3,0x30C,SOURCE+4)); reject(2,0x308,0x01E00280); /* physical alias */
    assert(emit(3,0x30C,DEST)); alias_map=1; reject(2,0x308,0x01E00280); alias_map=0;
    wrap_map=1; reject(2,0x308,0x01E00280); wrap_map=0;
    bad_tile=1; reject(2,0x308,0x01E00280); bad_tile=0;
    state.completed_blits=UINT64_MAX; reject(2,0x308,0x01E00280); state.completed_blits=0;
    state.copied_bytes=UINT64_MAX-BYTES+1; reject(2,0x308,0x01E00280); state.copied_bytes=0;
    memcpy(before,ram,sizeof ram); assert(emit(2,0x308,0x01E00280));
    assert(!memcmp(ram+DEST,before+SOURCE,BYTES)); /* all original alpha bits too */
    assert(!memcmp(ram,before,DEST));
    assert(!memcmp(ram+DEST+BYTES,before+DEST+BYTES,RAM_BYTES-DEST-BYTES));
    assert(state.completed_blits==1 && state.copied_bytes==BYTES);
    assert(state.last_blit_source==SOURCE && state.last_blit_dest==DEST);
    assert(emit(2,0x308,0x01E00280) && state.completed_blits==2 && state.copied_bytes==2*BYTES);
    puts("Halo 2 display SRCCOPY: exact RGBA, spans, contexts, permissions/limits, physical/host alias and rejection preservation pass.");
    return 0;
}
