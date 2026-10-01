/* Exercise production recording with a controlled upload proof. */
#define main texture_preparation_main
#include "texture_preparation_test.c"
#undef main
int main(int argc,char **argv)
{
    if(argc==1)setenv("XV_NEUTRAL_DETAIL","1",1);
    else if(!strcmp(argv[1],"override")){setenv("XV_NEUTRAL_DETAIL","1",1);setenv("XV_SHADER_OVERRIDE","1",1);}
    else unsetenv("XV_NEUTRAL_DETAIL");
    unsigned tested=0;
    for(unsigned entry=0;entry<XV_PS_TABLE_COUNT;entry++) {
        if(xv_ps_table[entry].ps_key!=0x154066FDu)continue;
        xv_vs_desc_t d={.func_hash=xv_ps_table[entry].vs_fnv};
        for(unsigned scene=0;scene<7;scene++) {
            memset(&S,0,sizeof S);memset(headers,0,sizeof headers);memset(g_rt,0,sizeof g_rt);
            S.tex_guest[1]=2;S.tex_addr_u[1]=S.tex_addr_v[1]=X_D3DTADDRESS_WRAP;
            S.tex_min[1]=S.tex_mag[1]=X_D3DTEXF_LINEAR;
            missing=scene==1?2:0;uploaded_neutral=scene!=2;
            if(scene==3)headers[1][1]=xd3d_backbuffer_data();
            if(scene==4)S.tex_addr_u[1]=X_D3DTADDRESS_BORDER;
            if(scene==5)S.tex_addr_v[1]=X_D3DTADDRESS_BORDER;
            cmd_t c={.fs_kind=FS_TEXMOD,.ps_entry=scene==6?-1:(int)entry};
            record_textures(&c,&d,1);
            assert(c.neutral_detail==(argc==1 && scene==0));tested++;
        }
    }
    printf("PASS: %u neutral-detail recordings; missing uploads, proof failures, previous frame, borders, unknown shaders and opt-in\n",tested);
}
