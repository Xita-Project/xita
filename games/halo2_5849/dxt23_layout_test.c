#include "dxt23_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void check(unsigned size, int (*layout)(const uint8_t *, size_t, uint8_t *, size_t))
{
    uint8_t storage[256], expected[256], before[256];
    unsigned cases = 0;
    for (int offset = 1-(int)size; offset < (int)size; ++offset) {
        for (unsigned i = 0; i < sizeof storage; ++i) storage[i] = (i * 71u) ^ (i >> 2);
        memcpy(before, storage, sizeof before); memcpy(expected, storage, sizeof expected);
        unsigned destination = 96 + offset;
        for (unsigned i = 0; i < size; ++i) {
            unsigned block = i / (size/4), source_block = ((block & 1) << 1) | (block >> 1);
            expected[destination + i] = before[96 + source_block * (size/4) + i % (size/4)];
        }
        assert(layout(storage + 96, size, storage + destination, size));
        assert(!memcmp(storage, expected, sizeof storage)); ++cases;
    }
    memcpy(before, storage, sizeof before);
    for (unsigned n = 0; n < 128; ++n) if (n != size) {
        assert(!layout(storage, n, storage + 128, size));
        assert(!layout(storage, size, storage + 128, n));
    }
    assert(!layout(NULL, size, storage, size));
    assert(!layout(storage, size, NULL, size));
    assert(!layout((void *)(UINTPTR_MAX - (size/2)), size, storage, size));
    assert(!layout(storage, size, (void *)(UINTPTR_MAX - (size/2)), size));
    assert(!memcmp(storage, before, sizeof storage));
    printf("Block layout %u bytes: %u overlapping spans, rejected sizes/pointers unchanged\n", size, cases);
}

/* Independent recursive spatial ordering, not the production bit encoder. */
static void walk(uint8_t *out, unsigned *cursor, const uint8_t *in, unsigned stride,
                 unsigned x, unsigned y, unsigned width, unsigned height)
{
    if (width == 1 && height == 1) {
        memcpy(out + (*cursor)++ * 16, in + (y * stride + x) * 16, 16);
    } else if (width > height) {
        walk(out,cursor,in,stride,x,y,width/2,height);
        walk(out,cursor,in,stride,x+width/2,y,width/2,height);
    } else if (height > width) {
        walk(out,cursor,in,stride,x,y,width,height/2);
        walk(out,cursor,in,stride,x,y+height/2,width,height/2);
    } else {
        walk(out,cursor,in,stride,x,y,width/2,height/2);
        walk(out,cursor,in,stride,x,y+height/2,width/2,height/2);
        walk(out,cursor,in,stride,x+width/2,y,width/2,height/2);
        walk(out,cursor,in,stride,x+width/2,y+height/2,width/2,height/2);
    }
}
static void rectangles(void)
{
    const unsigned max = 1024 * 1024;
    uint8_t *source = malloc(max+32), *dest = malloc(max+32), *expected = malloc(max+32);
    assert(source && dest && expected);
    for (unsigned i=0;i<max+32;++i) source[i]=(i*71u)^(i>>5)^(i>>13);
    for (unsigned x=0;x<=10;++x) for (unsigned y=0;y<=10;++y) {
        unsigned width=1u<<x,height=1u<<y,columns=(width+3)/4,rows=(height+3)/4;
        size_t bytes=(size_t)columns*rows*16;
        memset(dest,0xA5,max+32);memset(expected,0xA5,max+32);
        unsigned cursor=0;walk(expected+16,&cursor,source+16,columns,0,0,columns,rows);
        assert(cursor==columns*rows);
        assert(h2_dxt23_gxm_rect(source+16,bytes,dest+16,bytes,width,height));
        assert(!memcmp(dest,expected,max+32));
        assert(!h2_dxt23_gxm_rect(source+16,bytes-1,dest+16,bytes,width,height));
        assert(!h2_dxt23_gxm_rect(source+16,bytes,dest+16,bytes+1,width,height));
        assert(!memcmp(dest,expected,max+32));
    }
    memcpy(dest,source,max+32);
    for (int delta=-63;delta<64;++delta)
        assert(!h2_dxt23_gxm_rect(source+128,64,source+128+delta,64,8,8));
    assert(!memcmp(dest,source,max+32));
    const unsigned bad[]={0,3,7,1025,2048,UINT32_MAX};
    for (unsigned i=0;i<sizeof bad/sizeof *bad;++i) {
        assert(!h2_dxt23_gxm_rect(source,64,dest,64,bad[i],8));
        assert(!h2_dxt23_gxm_rect(source,64,dest,64,8,bad[i]));
    }
    assert(!h2_dxt23_gxm_rect(NULL,64,dest,64,8,8));
    assert(!h2_dxt23_gxm_rect(source,64,NULL,64,8,8));
    assert(!h2_dxt23_gxm_rect((void *)(UINTPTR_MAX-31),64,dest,64,8,8));
    assert(!h2_dxt23_gxm_rect(source,64,(void *)(UINTPTR_MAX-31),64,8,8));
    assert(!memcmp(dest,source,max+32));
    free(source);free(dest);free(expected);
    puts("BC2 rectangular layout: all121 logical shapes, recursive ordering, guard bytes, overlap and invalid spans passed");
}
int main(void) { check(64,h2_dxt23_gxm_8x8); check(32,h2_dxt1_gxm_8x8); rectangles(); }
