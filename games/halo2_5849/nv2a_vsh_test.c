/* Exercises the NV2A vertex-program interpreter with hand-assembled slots.
 * The assembler here packs the same fields dx8_shader_parse.py decodes, so a
 * round trip (assemble -> nv2a_vsh_run) checks the interpreter's field reads,
 * MAC/ILU semantics (including add's A,C operands), swizzle/negate, masked
 * writes, a0-relative constants, and a full 4x DP4 matrix transform. */
#include "nv2a_vsh.c"
#include <assert.h>
#include <stdio.h>

/* ---- tiny assembler ---- */
typedef struct { uint32_t w[4]; } slot;
static void put(slot *s, unsigned dw, unsigned lsb, unsigned width, uint32_t v)
{ s->w[dw] |= (v & ((1u << width) - 1u)) << lsb; }
/* swizzle code from a 4-char string of x/y/z/w */
static unsigned swz(const char *s)
{
    unsigned v = 0; const char *t = "xyzw";
    for (unsigned k = 0; k < 4; ++k) { unsigned c = 0; for (; t[c]; ++c) if (t[c]==s[k]) break; v = (v<<2)|c; }
    return v; /* packed X,Y,Z,W each 2 bits, X in high pair */
}
static void set_swz(slot *s, char slotc, const char *sw)
{
    unsigned p = swz(sw); /* bits: X=6..7,Y=4..5,Z=2..3,W=0..1 within the pair block */
    unsigned x=(p>>6)&3, y=(p>>4)&3, z=(p>>2)&3, w=p&3;
    if (slotc=='A'){put(s,1,6,2,x);put(s,1,4,2,y);put(s,1,2,2,z);put(s,1,0,2,w);}
    if (slotc=='B'){put(s,2,23,2,x);put(s,2,21,2,y);put(s,2,19,2,z);put(s,2,17,2,w);}
    if (slotc=='C'){put(s,2,8,2,x);put(s,2,6,2,y);put(s,2,4,2,z);put(s,2,2,2,w);}
}
/* operand: slotc in A/B/C; mux 1=R,2=V,3=C; index = temp/const index (V uses shared v) */
static void operand(slot *s, char slotc, unsigned mux, unsigned index, int neg, const char *sw)
{
    set_swz(s, slotc, sw ? sw : "xyzw");
    if (slotc=='A'){put(s,2,26,2,mux); put(s,1,8,1,neg?1:0); if(mux==1)put(s,2,28,4,index);}
    if (slotc=='B'){put(s,2,11,2,mux); put(s,2,25,1,neg?1:0); if(mux==1)put(s,2,13,4,index);}
    if (slotc=='C'){put(s,3,28,2,mux); put(s,2,10,1,neg?1:0);
                    if(mux==1){put(s,2,0,2,(index>>2)&3);put(s,3,30,2,index&3);}}
    if (mux==2) put(s,1,9,4,index);   /* shared V */
    if (mux==3) put(s,1,13,8,index);  /* shared CONST */
}
static void mac(slot *s, unsigned op){ put(s,1,21,4,op); }
static void ilu(slot *s, unsigned op){ put(s,1,25,3,op); }
static void out_reg(slot *s, unsigned o_addr, unsigned mask, unsigned mux /*0=MAC,1=ILU*/)
{ put(s,3,3,8,o_addr); put(s,3,11,1,1); put(s,3,12,4,mask); put(s,3,2,1,mux); }
static void a0relative(slot *s){ put(s,3,1,1,1); }
static void final(slot *s){ put(s,3,0,1,1); }

static float I[NV2A_VSH_INPUTS][4], C[NV2A_VSH_CONSTS][4], O[NV2A_VSH_OUTPUTS][4];
static void clear(void){ memset(I,0,sizeof I); memset(C,0,sizeof C); }
static int feq(float a, float b){ float d=a-b; return d<1e-4f && d>-1e-4f; }

static void test_mov_passthrough(void)
{
    clear(); I[0][0]=1; I[0][1]=2; I[0][2]=3; I[0][3]=4;
    slot s={{0}}; mac(&s,1); operand(&s,'A',2,0,0,"xyzw"); out_reg(&s,NV2A_O_POS,0xF,0); final(&s);
    nv2a_vsh_run((const uint32_t(*)[4])&s,0,1,I,C,O);
    assert(feq(O[NV2A_O_POS][0],1)&&feq(O[NV2A_O_POS][1],2)&&feq(O[NV2A_O_POS][2],3)&&feq(O[NV2A_O_POS][3],4));
}
static void test_add_reads_a_and_c(void)
{   /* add uses A and C, NOT B: oD0 = v0 + c0 */
    clear(); I[0][0]=1; I[0][1]=1; I[0][2]=1; I[0][3]=1; C[0][0]=10; C[0][1]=20; C[0][2]=30; C[0][3]=40;
    slot s={{0}}; mac(&s,3); operand(&s,'A',2,0,0,"xyzw"); operand(&s,'C',3,0,0,"xyzw");
    out_reg(&s,NV2A_O_D0,0xF,0); final(&s);
    nv2a_vsh_run((const uint32_t(*)[4])&s,0,1,I,C,O);
    assert(feq(O[NV2A_O_D0][0],11)&&feq(O[NV2A_O_D0][1],21)&&feq(O[NV2A_O_D0][2],31)&&feq(O[NV2A_O_D0][3],41));
}
static void test_mad_and_swizzle_negate(void)
{   /* oT0 = (-v0.yyyy) * c0 + c0.  One instruction has ONE shared const, so B and
     * C both address c0 (a real NV2A constraint): (-2)*3 + 3 = -3. */
    clear(); I[0][1]=2; C[0][0]=3; C[0][1]=3; C[0][2]=3; C[0][3]=3;
    slot s={{0}}; mac(&s,4);
    operand(&s,'A',2,0,1,"yyyy"); operand(&s,'B',3,0,0,"xyzw"); operand(&s,'C',3,0,0,"xyzw");
    out_reg(&s,NV2A_O_T0,0xF,0); final(&s);
    nv2a_vsh_run((const uint32_t(*)[4])&s,0,1,I,C,O);
    for (unsigned k=0;k<4;++k) assert(feq(O[NV2A_O_T0][k], (-2.0f)*3.0f + 3.0f)); /* -3 */
}
static void test_dp4_masked(void)
{   /* oPos.x = dot(v0, c0) only x written */
    clear(); I[0][0]=1; I[0][1]=2; I[0][2]=3; I[0][3]=4; C[0][0]=1; C[0][1]=1; C[0][2]=1; C[0][3]=1;
    slot s={{0}}; mac(&s,7); operand(&s,'A',2,0,0,"xyzw"); operand(&s,'B',3,0,0,"xyzw");
    out_reg(&s,NV2A_O_POS,0x8,0); final(&s);
    nv2a_vsh_run((const uint32_t(*)[4])&s,0,1,I,C,O);
    assert(feq(O[NV2A_O_POS][0],10));            /* dot = 1+2+3+4, broadcast, x written */
    assert(feq(O[NV2A_O_POS][1],0));             /* y unwritten -> default 0 */
}
static void test_ilu_rcp_to_temp_and_out(void)
{   /* r1 = 1/c0.x ; oFog = r1 (ILU external) */
    clear(); C[0][0]=4;
    slot s={{0}}; ilu(&s,2); operand(&s,'C',3,0,0,"xyzw"); put(&s,3,16,4,0xF); /* ILU temp mask r1 */
    out_reg(&s,NV2A_O_FOG,0xF,1); final(&s);
    nv2a_vsh_run((const uint32_t(*)[4])&s,0,1,I,C,O);
    assert(feq(O[NV2A_O_FOG][0],0.25f));
}
static void test_a0_relative_const(void)
{   /* slot0: arl a0 = floor(v0.x)=2 ; slot1: mov oPos = c[a0.x + 0] = c2 */
    clear(); I[0][0]=2.7f; C[2][0]=5; C[2][1]=6; C[2][2]=7; C[2][3]=8;
    slot s0={{0}}; mac(&s0,13); operand(&s0,'A',2,0,0,"xyzw");
    slot s1={{0}}; mac(&s1,1); operand(&s1,'A',3,0,0,"xyzw"); a0relative(&s1); out_reg(&s1,NV2A_O_POS,0xF,0); final(&s1);
    slot prog[2]={s0,s1};
    nv2a_vsh_run((const uint32_t(*)[4])prog,0,2,I,C,O);
    assert(feq(O[NV2A_O_POS][0],5)&&feq(O[NV2A_O_POS][3],8));
}
static void test_matrix_transform_identity(void)
{   /* four dp4 slots: oPos.{x,y,z,w} = dot(v0, c{0,1,2,3}); identity matrix -> oPos == v0 */
    clear();
    I[0][0]=7; I[0][1]=-3; I[0][2]=2; I[0][3]=1;
    for (unsigned i=0;i<4;++i) C[i][i]=1.0f; /* identity rows */
    slot prog[4];
    const unsigned mask[4]={0x8,0x4,0x2,0x1};
    for (unsigned i=0;i<4;++i){ slot s={{0}}; mac(&s,7);
        operand(&s,'A',2,0,0,"xyzw"); operand(&s,'B',3,i,0,"xyzw"); out_reg(&s,NV2A_O_POS,mask[i],0);
        if(i==3){final(&s);} prog[i]=s; }
    nv2a_vsh_run((const uint32_t(*)[4])prog,0,4,I,C,O);
    assert(feq(O[NV2A_O_POS][0],7)&&feq(O[NV2A_O_POS][1],-3)&&feq(O[NV2A_O_POS][2],2)&&feq(O[NV2A_O_POS][3],1));
}
static void test_final_stops(void)
{   /* second slot would overwrite oPos, but the first is FINAL and must stop execution */
    clear(); I[0][0]=1; C[0][0]=42;
    slot s0={{0}}; mac(&s0,1); operand(&s0,'A',2,0,0,"xyzw"); out_reg(&s0,NV2A_O_POS,0xF,0); final(&s0);
    slot s1={{0}}; mac(&s1,1); operand(&s1,'A',3,0,0,"xyzw"); out_reg(&s1,NV2A_O_POS,0xF,0);
    slot prog[2]={s0,s1};
    unsigned ran = nv2a_vsh_run((const uint32_t(*)[4])prog,0,2,I,C,O);
    assert(ran==1 && feq(O[NV2A_O_POS][0],1)); /* c0 (42) never reached */
}

int main(void)
{
    test_mov_passthrough();
    test_add_reads_a_and_c();
    test_mad_and_swizzle_negate();
    test_dp4_masked();
    test_ilu_rcp_to_temp_and_out();
    test_a0_relative_const();
    test_matrix_transform_identity();
    test_final_stops();
    printf("nv2a_vsh_test: all assertions passed\n");
    return 0;
}
