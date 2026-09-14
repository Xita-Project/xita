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
    fprintf(f,"completion=%s\nNo title/menu, audio routing, real-time DSP scheduling or Xbox timing claim.\n",ok?"PASS":"FAIL");
    h2_dsp_destroy(s);fclose(f);sceKernelExitProcess(ok?0:1);return ok?0:1;
}
