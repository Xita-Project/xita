#!/usr/bin/env python3
"""Production present/acquire logic: only a still-owned next slot blocks."""
import pathlib,subprocess,tempfile,os
root=pathlib.Path(__file__).resolve().parents[1];s=(root/'runtime/main.c').read_text()
a=s.index('static volatile uint32_t g_frame_requested');b=s.index('void xv_present_drain(void);',a)+len('void xv_present_drain(void);');globals_=s[a:b]
a=s.index('void xv_present(void)');b=s.index('/* Called after draining',a);code=s[a:b]
a=s.index('void xv_benchmark_optimizations(int enabled)');b=s.index('void xv_benchmark_present(void)',a);code+=s[a:b]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <psp2/gxm.h>
#include "runtime/xv_frame_slots.h"
#define XV_LOG(...) ((void)0)
void xv_cpu_guest_poll(void) {}
static unsigned g_update_quiesced;
unsigned xv_update_requested(void) { return 0; }
static unsigned frame,ui,contents[3],waits;
static int upload_override=-99;
void xv_vertex_upload_override(int enabled) { upload_override=enabled; }
static int compare_override=-99;
void xv_vertex_compare_override(int enabled) { compare_override=enabled; }
static int candidate, scan_override=-99, copy_override=-99, bounds_override=-99, references_override=-99;
static int worker_override=-99;
int xv_benchmark_compare_early_visibility(void) { return candidate==8; }
static int basis_override=-99, palette_override=-99;
int xv_benchmark_compare_object_basis(void) { return candidate==6; }
int xv_benchmark_compare_model_palette(void) { return candidate==7; }
void xv_object_basis_override(int enabled) { basis_override=enabled; }
void xv_model_palette_override(int enabled) { palette_override=enabled; }
int xv_benchmark_compare_vertex_worker(void) { return candidate==5; }
void xv_vertex_worker_override(int enabled) { worker_override=enabled; }
int xv_benchmark_compare_vertex_references(void) { return candidate==4; }
void xv_vertex_references_override(int enabled) { references_override=enabled; }
int xv_benchmark_compare_native_bounds(void) { return candidate==3; }
void xv_native_bounds_override(int enabled) { bounds_override=enabled; }
int xv_benchmark_compare_draw_scan(void) { return candidate==1; }
int xv_benchmark_compare_vertex_copy(void) { return candidate==2; }
void xv_d3d_draw_scan_override(int enabled) { scan_override=enabled; }
void xv_vertex_copy_override(int enabled) { copy_override=enabled; }
static int flare_override=-99;
static unsigned flare_barriers;
static uint64_t now=1;
uint64_t sceKernelGetProcessTimeWide(void) { return now; }
uint32_t xv_ui_gxm_mesh_frame(void) { return frame; }
unsigned xv_ui_gxm_published_frame(void) { return ui; }
unsigned xv_ui_gxm_record_frame(void) { return (ui+1)%3; }
unsigned xv_d3d_record_slot(void) { return (frame+1)%3; }
'''
fixture=r'''
static void retire(void)
{
    assert(g_frame_completed!=g_frame_requested);
    uint32_t next=g_frame_completed+1u;unsigned q=next&3u;
    assert(contents[g_packets[q].mesh%3]==g_packets[q].mesh);
    assert(g_packets[q].ui==g_packets[q].mesh%3);
    assert(g_frame_requested-g_frame_completed<=3);
    g_frame_completed=next;now+=100;
}
void xk_sleep_us(uint64_t us) { assert(us==100);waits++;retire(); }
int sceKernelDelayThread(SceUInt us) { assert(us==100);retire();return 0; }
int sceKernelSetEventFlag(SceUID id,unsigned bits) { assert(0);return 0; }
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout) { assert(0);return 0; }
'''
suffix=r'''
void xv_flare_barrier(unsigned reason) { assert(reason==0);flare_barriers++; }
void xv_flare_defer_override(int enabled) { assert(flare_barriers);flare_override=enabled; }
int main(int argc,char **argv)
{
    assert(!xv_pipeline_enabled()); /* Recovery default must stay single-flight. */
    const char *configured=getenv("XV_TRIPLE_BUFFER");
    xv_pipeline_configure();
    assert(xv_pipeline_enabled()==(configured && atoi(configured)!=0));
    int single=argc>1 ? atoi(argv[1])!=0 : 1;
    xv_pipeline_override(argc>1 ? (single?0:1) : -1);
    if(argc==1)single=!xv_pipeline_enabled();
    if(argc>2)g_frame_completed=g_frame_requested=UINT32_MAX-80u;
    uint32_t start=g_frame_requested;
    for(frame=0;frame<200;frame++) {
        ui=frame%3;
        assert(!xv_slot_busy(&g_mesh_owners[ui],g_frame_completed));
        assert(!xv_slot_busy(&g_ui_owners[ui],g_frame_completed));
        contents[ui]=frame;
        xv_present();
        assert(g_frame_requested==start+frame+1u);
        assert(g_frame_requested-g_frame_completed==(single?1u:frame?2u:1u));
        assert(!xv_slot_busy(&g_mesh_owners[(ui+1)%3],g_frame_completed));
    }
    assert(waits==(single?0u:198u));
    xv_present_drain();assert(g_frame_completed==g_frame_requested);
    assert(!xv_early_visibility_enabled());
    for(candidate=0;candidate<CANDIDATE_COUNT;candidate++)for(int mode=-1;mode<=1;mode++) {
        g_early_visibility_override=-1;
        flare_override=scan_override=copy_override=bounds_override=references_override=worker_override=basis_override=palette_override=-99;flare_barriers=0;
        xv_benchmark_optimizations(mode);
        assert(flare_override==(candidate==0?mode:-99) && flare_barriers==(unsigned)(candidate==0));
        assert(scan_override==(candidate==1?mode:-99));
        assert(copy_override==(candidate==2?mode:-99));
        assert(bounds_override==(candidate==3?mode:-99));
        assert(references_override==(candidate==4?mode:-99));
        assert(worker_override==(candidate==5?mode:-99));
        assert(basis_override==(candidate==6?mode:-99));
        assert(palette_override==(candidate==7?mode:-99));
        assert(g_early_visibility_override==(candidate==8?mode:-1));
        assert(xv_early_visibility_enabled()==(candidate==8 && mode==1));
        assert(compare_override==-99 && upload_override==-99 && xv_pipeline_enabled()==!single);
    }
    xv_pipeline_override(-1);
    assert(xv_pipeline_enabled()==(configured && atoi(configured)!=0));
    puts("PASS: production acquire blocks only on the next owned slot, preserves queued packets, supports single-flight comparison and wraps tickets");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-frame-acquire-') as tmp:
    count=9
    p=pathlib.Path(tmp);(p/'test.c').write_text(f'#define CANDIDATE_COUNT {count}\n'+prefix+globals_+fixture+code+suffix)
    sdk=pathlib.Path(os.environ.get('VITASDK',str(pathlib.Path.home()/'vitasdk')))
    subprocess.run(['cc','-std=gnu11','-DXV_RUN_RECOMP','-DXV_NATIVE_OBJECT_BASIS','-DXV_NATIVE_MODEL_PALETTE','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable',
        '-I',str(root),'-I',str(root/'runtime'),'-idirafter',str(sdk/'arm-vita-eabi/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    for value in [None,'0','1','invalid','-1']:
        env=os.environ.copy();env.pop('XV_TRIPLE_BUFFER',None)
        if value is not None:env['XV_TRIPLE_BUFFER']=value
        for args in [[],['0'],['1'],['0','wrap'],['1','wrap']]:
            subprocess.run([str(p/'test'),*args],check=True,env=env)
