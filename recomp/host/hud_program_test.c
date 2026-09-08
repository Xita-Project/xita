#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xv_hud.h"

int main(void)
{
    const char *names[] = {"A972FE61", "EB818129", "5D70F0B3", "6C94962B", "1A42D493", "C2D57121", "FFBC5A4D"};
    const uint32_t hashes[] = {0xA972FE61u, 0xA972FE61u, 0x5D70F0B3u, 0x6C94962Bu, 0x1A42D493u, 0xC2D57121u, 0xFFBC5A4Du};
    for (unsigned k = 0; k < sizeof hashes / sizeof hashes[0]; k++) {
        char path[128]; uint32_t def[60], altered[60];
        snprintf(path, sizeof path, "../../shaders/psdefs/%s.bin", names[k]);
        FILE *f = fopen(path, "rb"); assert(f);
        assert(fread(def, sizeof def, 1, f) == 1); fclose(f);
        uint32_t expected = hashes[k];
        assert(xv_hud_program(def) == expected);
        assert(xv_hud_program_for_vs(0x1DAF0284u, def) == (k == 5 ? 0 : expected));
        /* Radar producer/copy/blip have shaders for VS04. The HUD shaders
         * exist only for VS03: routing VS04 menu text there hid its foreground. */
        assert(xv_hud_program_for_vs(0x4469E1F8u, def) == (k >= 3 && k <= 5 ? expected : 0));
        assert(xv_hud_program_for_vs(0xDEADBEEFu, def) == 0);
        unsigned count = def[0xD4 / 4] & 15;
        const unsigned offsets[] = {0, 0x68 / 4, 0x88 / 4, 0xB4 / 4};
        memcpy(altered, def, sizeof def);
        for (unsigned o = 0; o < 4; o++)
            for (unsigned i = count; i < 8; i++) altered[offsets[o] + i] ^= 0x13579BDFu;
        /* Observed weapon/effect changes leave different unused texture modes. */
        altered[0xD8 / 4] = 1u | (1u << 5) | (1u << 10) | (1u << 15);
        for (unsigned i = 0x28 / 4; i < 0x68 / 4; i++) altered[i] ^= 0xCCAA5511u;
        assert(xv_hud_program(altered) == expected);
        if (expected == 0xFFBC5A4Du) {
            /* Current Snipers capture 40BF35E1 differs only in unused t2/t3 modes. */
            memcpy(altered,def,sizeof def); altered[0xD8/4]=0x21;
            assert(xv_hud_program(altered)==expected);
            for (unsigned mode=0; mode<=3; mode++) if (mode!=1) {
                memcpy(altered,def,sizeof def);
                altered[0xD8/4]=(altered[0xD8/4]&~(31u<<5))|(mode<<5);
                assert(!xv_hud_program(altered));
            }
        }
        for (unsigned o = 0; o < 4; o++)
            for (unsigned i = 0; i < count; i++) {
                memcpy(altered, def, sizeof def); altered[offsets[o] + i] ^= 1;
                assert(xv_hud_program(altered) == 0);
            }
        memcpy(altered, def, sizeof def); altered[8] ^= 1;
        assert(xv_hud_program(altered) == 0);
        memcpy(altered, def, sizeof def); altered[0xD4 / 4] ^= 0x1000;
        assert(xv_hud_program(altered) == 0);
        memcpy(altered, def, sizeof def); altered[0xD8 / 4] = 0;
        assert(xv_hud_program(altered) == 0);
        memcpy(altered, def, sizeof def); altered[0xD8 / 4] = 1 | (5u << 5); /* clip */
        assert(xv_hud_program(altered) == 0);
    }
    const uint32_t composites[] = {0x1E073CA3u,0x74D0C65Eu,0x423B087Du,0xB8A3D35Fu,0x8C25D9E4u,0x1F88B236u};
    for (unsigned k=0;k<sizeof composites/sizeof composites[0];k++) {
        char path[128]; uint32_t def[60], altered[60];
        snprintf(path,sizeof path,"../../shaders/psdefs/%08X.bin",composites[k]);
        FILE *f=fopen(path,"rb"); assert(f);
        assert(fread(def,sizeof def,1,f)==1); fclose(f);
        assert(xv_composite_program_for_vs(0xBB2F446Bu,def)==composites[k]);
        assert(!xv_composite_program_for_vs(0x1DAF0284u,def));
        memcpy(altered,def,sizeof def);
        for (unsigned i=0x28/4;i<0x68/4;i++) altered[i]^=0xCA135791u;
        unsigned n=def[0xD4/4]&15;
        const unsigned bases[]={0,0x68/4,0x88/4,0xB4/4};
        for (unsigned b=0;b<4;b++) for (unsigned i=n;i<8;i++) altered[bases[b]+i]^=0x87654321u;
        assert(xv_composite_program_for_vs(0xBB2F446Bu,altered)==composites[k]);
        /* Final output, alpha math, and texture modes all affect this pass. */
        const unsigned active[]={0,8,9,0xD8/4};
        for (unsigned i=0;i<4;i++) {
            memcpy(altered,def,sizeof def); altered[active[i]]^=1;
            assert(!xv_composite_program_for_vs(0xBB2F446Bu,altered));
        }
    }
    assert(!xv_hud_program(NULL));
    assert(!xv_composite_program_for_vs(0xBB2F446Bu,NULL));
    /* VS38's four affine UV transforms mix normalized mask coordinates with
     * pixel coordinates for differently sized linear source textures. */
    float rows[8][4], original[8][4], scaled[8][4];
    for (unsigned t=0;t<4;t++) {
        rows[t*2][0]=1; rows[t*2][1]=0; rows[t*2][2]=0; rows[t*2][3]=.5f;
        rows[t*2+1][0]=0; rows[t*2+1][1]=1; rows[t*2+1][2]=0; rows[t*2+1][3]=-.5f;
    }
    memcpy(original,rows,sizeof rows);
    const uint32_t sizes[4]={0,639u|(479u<<12),127u|(63u<<12),0};
    xv_composite_texture_rows(scaled,rows,sizes);
    assert(!memcmp(original,rows,sizeof rows));
    assert(!memcmp(scaled,rows,2*4*sizeof(float)));
    assert(!memcmp(scaled[6],rows[6],2*4*sizeof(float)));
    assert(scaled[2][0]*640.0f==1.0f && scaled[3][1]*480.0f==1.0f);
    assert(scaled[2][3]*640.0f==.5f && scaled[3][3]*480.0f==-.5f);
    assert(scaled[4][0]*128.0f==1.0f && scaled[5][1]*64.0f==1.0f);
    puts("PASS: HUD/composite routing preserves dynamic constants and unused state; active changes rejected");
}
