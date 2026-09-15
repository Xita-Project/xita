#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef int SceGxmContext;
typedef struct { float psc[18][4], texscale[4][4]; uint32_t fog_color, atest; } cmd_t;
typedef struct { const int *p_psc, *p_fogcolor, *p_atest, *p_texscale; } xv_fshader_t;
static int parameters[4];
static unsigned reserve_calls, write_calls, fail_write, null_buffer;
static int reserve_error;
static float buffer[96], written[4][72];
static unsigned sizes[4];
static void test_log(const char *fmt, ...) { (void)fmt; }
#define XV_LOG test_log
#define XV_RENDER_CALL(kind, expression) (expression)
static int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext *ctx, void **p)
{
    (void)ctx; reserve_calls++;
    *p = null_buffer ? NULL : buffer;
    return reserve_error;
}
static int sceGxmSetUniformDataF(void *p, const int *parameter, unsigned first,
                               unsigned count, const float *data)
{
    assert(p == buffer && !first && count <= 72);
    unsigned index = (unsigned)(parameter - parameters);
    assert(index < 4);
    write_calls++;
    if (write_calls == fail_write) return -17;
    sizes[index] = count;
    memcpy(written[index], data, count * sizeof(float));
    return 0;
}
#include "fragment_constants.inc"
static void reset(void)
{
    reserve_calls = write_calls = fail_write = null_buffer = 0;
    reserve_error = 0;
    memset(sizes, 0, sizeof sizes);
    memset(written, 0xA5, sizeof written);
}
int main(void)
{
    cmd_t c = {.fog_color = 0x7F123456, .atest = 0x104A5};
    for (unsigned i=0;i<18;i++) for (unsigned j=0;j<4;j++) c.psc[i][j]=(float)(i*4+j)-30.5f;
    for (unsigned i=0;i<4;i++) for (unsigned j=0;j<4;j++) c.texscale[i][j]=(float)(i*4+j)+.25f;
    int noat = atoi(getenv("XV_NO_ATEST"));
    unsigned cases = 0;
    for (unsigned mask=0;mask<16;mask++) {
        xv_fshader_t fs = {
            mask&1 ? parameters : NULL, mask&2 ? parameters+1 : NULL,
            mask&8 ? parameters+3 : NULL, mask&4 ? parameters+2 : NULL};
        unsigned expected=!!(mask&1)+!!(mask&2)+!!(mask&4)+!!(mask&8);
        reset();
        assert(bind_fragment_constants(NULL,&c,&fs,123,4)); cases++;
        assert(reserve_calls==!!mask && write_calls==expected);
        if(mask&1) assert(sizes[0]==72 && !memcmp(written[0],c.psc,sizeof c.psc));
        if(mask&2) {
            const float fog[]={0x12/255.0f,0x34/255.0f,0x56/255.0f,0x7F/255.0f};
            assert(sizes[1]==4 && !memcmp(written[1],fog,sizeof fog));
        }
        if(mask&4) assert(sizes[2]==16 && !memcmp(written[2],c.texscale,sizeof c.texscale));
        if(mask&8) {
            const float alpha[]={0xA5/255.0f,4.0f,noat ? 0.0f : 1.0f,0.0f};
            assert(sizes[3]==4 && !memcmp(written[3],alpha,sizeof alpha));
        }
        if (!mask) continue;
        reset(); reserve_error=-27;
        assert(!bind_fragment_constants(NULL,&c,&fs,123,5) && !write_calls); cases++;
        reset(); null_buffer=1;
        assert(!bind_fragment_constants(NULL,&c,&fs,123,6) && !write_calls); cases++;
        for (unsigned failure=1;failure<=expected;failure++) {
            reset(); fail_write=failure;
            assert(!bind_fragment_constants(NULL,&c,&fs,123,7)); cases++;
            assert(write_calls==failure); /* No writes after the first error. */
            reset();
            assert(bind_fragment_constants(NULL,&c,&fs,123,8)); cases++;
            assert(reserve_calls==1 && write_calls==expected); /* Fresh retry. */
        }
    }
    xv_fshader_t alpha_only={.p_atest=parameters+3};
    c.atest &= ~(1u<<16); reset();
    assert(bind_fragment_constants(NULL,&c,&alpha_only,124,0)); cases++;
    assert(written[3][2]==0.0f);
    printf("fragment constants: %u cases passed, alpha override %d\n",cases,noat);
    return 0;
}
