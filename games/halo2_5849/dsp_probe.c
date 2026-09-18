/* Private owned-DSP execution probe; no game bytes in this source. The package
 * contains privately prepared owned code and must never be distributed. */
#include "dsp_asset.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdint.h>
int main(void)
{
    sceIoMkdir("ux0:data",0777);sceIoMkdir("ux0:data/xita-halo2",0777);
    FILE*f=fopen("ux0:data/xita-halo2/dsp-probe.txt","w");if(!f)return 2;
    h2_dsp_status status;uint64_t start=sceKernelGetProcessTimeWide();
    h2_dsp_engine*s=h2_dsp_asset_open("app0:halo2-dsp.bin",&status);
    int ok=s&&h2_dsp_zero_frame(s);
    if(s)h2_dsp_snapshot(s,&status);
    fprintf(f,"original_owned_DSP_init=%s\nframes=%llu instructions=%llu cycles=%llu transfers=%llu pc=%04X command=%u effects=%u scratch_bytes=%u elapsed_us=%llu\n",
        ok?"PASS":"FAIL",(unsigned long long)status.frames,(unsigned long long)status.instructions,
        (unsigned long long)status.cycles,(unsigned long long)status.transfers,status.pc,status.command,
        status.effect_count,status.scratch_bytes,(unsigned long long)(sceKernelGetProcessTimeWide()-start));
    fprintf(f,"canonical_state_fingerprint=%016llX\n",(unsigned long long)status.state_fingerprint);
    if(status.fault)fprintf(f,"fault=%s address=%08X value=%08X\n",status.fault,status.fault_address,status.fault_value);
    for(unsigned i=4;i<8&&ok;i++){
        uint32_t data[2];if(!h2_dsp_read_effect(s,i,0x20,data,8)){ok=0;break;}
        fprintf(f,"GetEffectData effect=%u offset=20 bytes=8 data=%08X,%08X\n",i,data[0],data[1]);
    }
    for(unsigned i=11;i<15&&ok;i++){
        uint32_t data[2];if(!h2_dsp_read_effect(s,i,0x20,data,8)){ok=0;break;}
        fprintf(f,"executed_state effect=%u offset=20 bytes=8 data=%08X,%08X\n",i,data[0],data[1]);
    }
    int32_t bins[32][32]={{0}},output[32];uint64_t output_hash=UINT64_C(0xcbf29ce484222325);
    unsigned nonzero=0,completed=0;int32_t peak=0;uint64_t elapsed=0;
    if(ok){
        uint64_t began=sceKernelGetProcessTimeWide();
        for(unsigned frame=0;frame<64&&ok;frame++){
            for(unsigned i=0;i<32;i++)bins[13][i]=frame?0:0x100000;
            ok=h2_dsp_mix_frame(s,bins)&&h2_dsp_read_fx_frame(s,13,output);
            if(!ok)break;
            for(unsigned i=0;i<32;i++){
                int32_t sample=output[i],absolute=sample<0?-sample:sample;
                if(sample)nonzero++;
                if(absolute>peak)peak=absolute;
                for(unsigned j=0;j<4;j++)output_hash=(output_hash^(uint8_t)((uint32_t)sample>>(j*8)))*UINT64_C(0x100000001b3);
            }
            completed++;
        }
        elapsed=sceKernelGetProcessTimeWide()-began;
        h2_dsp_snapshot(s,&status);
    }
    fprintf(f,"signal_frames=%u input=bin13_first_frame_0x100000_then_zero output=FX13_scratch_B100 nonzero_samples=%u peak=%d output_hash=%016llX elapsed_us=%llu canonical_after=%016llX\n",
        completed,nonzero,peak,(unsigned long long)output_hash,(unsigned long long)elapsed,(unsigned long long)status.state_fingerprint);
    if(status.fault)fprintf(f,"signal_fault=%s address=%08X value=%08X\n",status.fault,status.fault_address,status.fault_value);
    ok=ok&&nonzero&&peak;
    fprintf(f,"completion=%s\nSynthetic signal through owned DSP code; no title/menu, sink output, real-time scheduling or Xbox timing claim.\n",ok?"PASS":"FAIL");
    h2_dsp_destroy(s);fclose(f);sceKernelExitProcess(ok?0:1);return ok?0:1;
}
