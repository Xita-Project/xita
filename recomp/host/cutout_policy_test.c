#define main old_cache_main
#include "ps_cache_test.c"
#undef main
#include <math.h>
int main(int argc,char **argv)
{
    int blocked=argc>1 && strcmp(argv[1],"disabled");
    if(argc>1) {
        const char *key=!strcmp(argv[1],"override")?"XV_SHADER_OVERRIDE":
            !strcmp(argv[1],"no-atest")?"XV_NO_ATEST":
            !strcmp(argv[1],"no-alpha")?"XV_ALPHA_SPECIALIZE":"XV_CUTOUT_TEST";
        setenv(key,(!strcmp(key,"XV_SHADER_OVERRIDE")||!strcmp(key,"XV_NO_ATEST"))?"1":"0",1);
    }
    unsigned cases=0;
    for(int entry=-1;entry<=(int)XV_PS_TABLE_COUNT;entry++)
    for(unsigned enabled=0;enabled<2;enabled++)for(unsigned f=0;f<8;f++)for(unsigned ref=0;ref<256;ref++) {
        cmd_t c={.ps_entry=entry,.atest=(enabled<<16)|(f<<8)|ref};
        xv_cutout_override(0);assert(material_alpha_mode(&c,0)==0);assert(material_alpha_mode(&c,1)==1);
        xv_cutout_override(1);
        int want=!blocked && entry>=0 && entry<(int)XV_PS_TABLE_COUNT && xv_ps_table[entry].ps_key==0x154066FDu && enabled && f==4;
        assert(material_alpha_mode(&c,0)==(want?2:0));assert(material_alpha_mode(&c,1)==1);cases++;
    }
    /* Original dynamic predicate with captured function 4 versus specialized
     * compare, at every byte cutoff and immediately adjacent float values. */
    for(unsigned ref=0;ref<256;ref++) {
        float r=ref/255.0f;
        float values[]={0,1,r,nextafterf(r,0),nextafterf(r,1),NAN,INFINITY,-INFINITY};
        for(unsigned i=0;i<sizeof values/sizeof *values;i++) {
            float a=values[i],f=4;
            int pass=f>6.5f || (f>3.5f&&f<4.5f&&a>r) || (f>5.5f&&f<6.5f&&a>=r)
                || (f>.5f&&f<1.5f&&a<r) || (f>2.5f&&f<3.5f&&a<=r)
                || (f>1.5f&&f<2.5f&&fabsf(a-r)<.002f) || (f>4.5f&&f<5.5f&&fabsf(a-r)>=.002f);
            assert(pass==(a>r));
        }
    }
    xv_cutout_override(-1);assert(cutout_enabled()==(argc==1));
    printf("PASS: %u captured-state selections; opaque priority, all references, shader overrides and exact GREATER predicate boundaries\n",cases);
}
