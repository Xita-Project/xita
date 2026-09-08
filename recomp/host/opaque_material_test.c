/* Real draw recording, with controlled texture uploads and GXM metadata. */
#include <math.h>
#define main texture_preparation_main
#include "texture_preparation_test.c"
#undef main

int main(int argc,char **argv)
{
    int blocked=argc>1;
    if(blocked)setenv(!strcmp(argv[1],"override")?"XV_SHADER_OVERRIDE":"XV_ALPHA_SPECIALIZE",
                     !strcmp(argv[1],"override")?"1":"0",1);
    unsigned tested=0;
    for(unsigned entry=0;entry<XV_PS_TABLE_COUNT;entry++) {
        if(xv_ps_table[entry].ps_key!=0x154066FDu)continue;
        xv_vs_desc_t d={.func_hash=xv_ps_table[entry].vs_fnv};
        for(unsigned f=0;f<8;f++)for(unsigned ref=0;ref<256;ref++)for(unsigned scene=0;scene<8;scene++) {
            memset(&S,0,sizeof S);memset(headers,0,sizeof headers);memset(g_rt,0,sizeof g_rt);
            S.tex_guest[0]=1;S.tex_addr_u[0]=S.tex_addr_v[0]=X_D3DTADDRESS_WRAP;
            S.tex_min[0]=S.tex_mag[0]=X_D3DTEXF_LINEAR;
            missing=scene==1?1:0;uploaded_opaque=scene!=2;
            ((uint32_t *)&textures[0])[0]=scene==3;
            if(scene==4)headers[0][1]=xd3d_backbuffer_data();
            if(scene==5)S.tex_addr_u[0]=X_D3DTADDRESS_BORDER;
            if(scene==6){headers[0][1]=g_rt[0].data=0x5000;g_rt[0].fmt=0;}
            cmd_t off={.fs_kind=FS_TEXMOD,.ps_entry=scene==7?-1:(int)entry,.atest=(1u<<16)|(f<<8)|ref},on=off;
            xv_opaque_material_override(0);record_textures(&off,&d,1);assert(!off.opaque_alpha);
            xv_opaque_material_override(1);record_textures(&on,&d,1);
            float r=ref/255.0f;
            int pass=(f==2&&fabsf(1-r)<.002f)||(f==3&&1<=r)||(f==4&&1>r)
                ||(f==5&&fabsf(1-r)>=.002f)||f==6;
            assert(on.opaque_alpha==(!blocked && !scene && pass));
            on.opaque_alpha=0;assert(!memcmp(&off,&on,sizeof off));tested++;
        }
    }
    xv_opaque_material_override(-1);assert(opaque_material_enabled()==!blocked);
    printf("PASS: %u production recordings; alpha references, cutouts, missing/cube/backbuffer/RT textures, borders, unknown shaders and overrides\n",tested);
}
